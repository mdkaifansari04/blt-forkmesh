// MainWindowSettings: MainWindow feature methods, split out of MainWindow.cpp.
// Settings, firewall, and diagnostics.
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "ControlNode.h"
#include "ForkMeshVersion.h"
#include "LogTimelineChart.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "NodeDiagnostics.h"
#include "QrCode.h"
#include "WorldSpeechBridge.h"

#include <QClipboard>
#include <QColor>
#include <QColorDialog>
#include <QDesktopServices>
#include <QPixmap>
#include <QTextDocument>
#include <QDialog>
#include <QInputDialog>
#include "KebabHeaderView.h"

#include <QDoubleSpinBox>
#include <QSpinBox>

using namespace forkmesh::ui;

namespace {

// Heading row for one settings group: the existing small-caps section label
// with an octicon in front of it. Once the groups are flowed into columns the
// glyph is what makes the page scannable — you find "Startup" or "Pings" by its
// icon instead of reading every header (adhoc #1533).
QHBoxLayout *settingsHeading(QLabel *label, const QString &octicon,
                             QWidget *trailing = nullptr)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    auto *glyph = new QLabel;
    setLabelOcticon(glyph, octicon, 12);
    // #sectionLabel carries a top padding, so its text sits at the bottom of the
    // label box; aligning the glyph to the bottom lines the two up.
    row->addWidget(glyph, 0, Qt::AlignBottom);
    row->addWidget(label, 0, Qt::AlignBottom);
    if (trailing)
        row->addWidget(trailing, 0, Qt::AlignBottom);
    row->addStretch();
    return row;
}

} // namespace

// ----------------------------------------------------------------- settings

QWidget *MainWindow::buildSettingsSection()
{
    auto *page = new QWidget;

    // No "Settings" banner over the tab strip: the left rail already says where
    // you are, and on a small screen that heading cost a row of settings for a
    // word nobody needed (adhoc #1533). The tab strip starts at the top instead.
    auto *profileLabel = new QLabel("PROFILE");
    profileLabel->setObjectName("sectionLabel");

    // Username (the account) and this machine's node name are two different
    // things: a user signs in once and can own many nodes; the machine you're
    // sitting at is just one of them. They used to share a single "Node name"
    // field, which is exactly the conflation being unwound here.
    m_settingsNameEdit = new QLineEdit;
    m_settingsNameEdit->setMaxLength(32);
    m_settingsNameEdit->setPlaceholderText("Username");
    m_settingsNameEdit->setToolTip(
        "Your ForkMesh user account. One user account can own many nodes; "
        "changing this changes who you are, not this machine's node name.");
    // This section is built lazily on first open — long after startup signed us
    // in — so seed the field with the account we're signed in as here; the
    // login/setup flows keep it fresh from then on. Without this the Username
    // sat blank while the top-bar chip showed the signed-in user (adhoc #267).
    m_settingsNameEdit->setText(settingsAccountName());
    connect(m_settingsNameEdit, &QLineEdit::editingFinished, this,
            [this] { onProfileNameChanged(m_settingsNameEdit->text()); });

    m_settingsMachineNodeEdit = new QLineEdit;
    m_settingsMachineNodeEdit->setMaxLength(63);
    m_settingsMachineNodeEdit->setPlaceholderText("Node name (this machine)");
    m_settingsMachineNodeEdit->setToolTip(
        "This machine's node name on the mesh \xE2\x80\x94 how it appears in "
        "rosters and node lists. Not your username: nodes are machines, and "
        "your user account can own many of them.");
    m_settingsMachineNodeEdit->setText(machineNodeName());
    connect(m_settingsMachineNodeEdit, &QLineEdit::editingFinished, this, [this] {
        saveMachineNodeName(m_settingsMachineNodeEdit->text());
        // Reflect the sanitized (or defaulted) value back into the field.
        m_settingsMachineNodeEdit->setText(machineNodeName());
    });

    // Capability tags for Actions: a workflow with `runs-on: ios` only runs on a
    // node carrying that label, so a mesh can dedicate one machine to iOS builds
    // and another to Cloudflare deploys. The node name and platform are always
    // labels; this field only adds to them.
    m_settingsNodeLabelsEdit = new QLineEdit;
    m_settingsNodeLabelsEdit->setMaxLength(200);
    m_settingsNodeLabelsEdit->setPlaceholderText(
        "Extra Actions labels, e.g. ios, xcode, deploy");
    m_settingsNodeLabelsEdit->setToolTip(
        "Extra labels this machine answers to when a workflow declares "
        "\"runs-on:\". Its node name (%1) and platform already count as labels; "
        "add capability tags here to dedicate this node to certain workflows.");
    m_settingsNodeLabelsEdit->setToolTip(
        m_settingsNodeLabelsEdit->toolTip().arg(machineNodeName()));
    m_settingsNodeLabelsEdit->setText(
        QSettings()
            .value(QString::fromLatin1(kActionNodeLabelsSetting))
            .toString());
    connect(m_settingsNodeLabelsEdit, &QLineEdit::editingFinished, this, [this] {
        saveActionNodeLabels(m_settingsNodeLabelsEdit->text());
        m_settingsNodeLabelsEdit->setText(
            QSettings()
                .value(QString::fromLatin1(kActionNodeLabelsSetting))
                .toString());
    });

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
    generateButton->setToolTip("Generate a fresh random avatar for your profile");
    connect(generateButton, &QPushButton::clicked, this, [this] {
        const QByteArray png = forkMeshNodeAvatarPng(
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

    // This is a public payout address only. It is not a wallet-import field and
    // must never persist private key material, a seed, or a recovery phrase.
    m_settingsSolanaEdit = new QLineEdit;
    m_settingsSolanaEdit->setMaxLength(64);
    m_settingsSolanaEdit->setPlaceholderText(
        "Public self-custodial Solana payout address (optional)");
    m_settingsSolanaEdit->setToolTip(
        "Enter one public Solana address only. Never enter a private key, seed, "
        "mnemonic, or recovery phrase; ForkMesh does not store wallet keys.");
    const QString savedAddress = savedSolanaAddress();
    m_settingsSolanaEdit->setText(
        savedAddress.isEmpty() ||
                forkmesh::control::isValidSolanaPublicAddress(savedAddress)
            ? savedAddress
            : QString());
    connect(m_settingsSolanaEdit, &QLineEdit::editingFinished, this, [this] {
        const QString addr = m_settingsSolanaEdit->text().trimmed();
        if (!addr.isEmpty() &&
            !forkmesh::control::isValidSolanaPublicAddress(addr)) {
            const QString previous = savedSolanaAddress();
            m_settingsSolanaEdit->setText(
                forkmesh::control::isValidSolanaPublicAddress(previous)
                    ? previous
                    : QString());
            flashMessage(
                QStringLiteral(
                    "That is not a valid Solana public address. No private key, "
                    "seed, mnemonic, or recovery phrase was saved."),
                true);
            return;
        }
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
    form->addRow("Username", m_settingsNameEdit);
    form->addRow("Node name", m_settingsMachineNodeEdit);
    form->addRow("Node labels", m_settingsNodeLabelsEdit);
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

    // Surface exactly how and where autostart is installed, plus an explicit
    // "Remove auto startup" button (issue #393). The bare checkbox used to hide
    // a failed removal — showing the concrete mechanism/path lets the user see
    // a stale entry and delete it directly.
    m_autostartInfo = new QLabel;
    m_autostartInfo->setObjectName("modeHint");
    m_autostartInfo->setWordWrap(true);
    m_autostartInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_autostartRemoveButton = new QPushButton("Remove auto startup");
    m_autostartRemoveButton->setObjectName("ghostButton");
    m_autostartRemoveButton->setCursor(Qt::PointingHandCursor);
    m_autostartRemoveButton->setToolTip(
        "Delete the login-autostart entry so ForkMesh no longer starts when you "
        "log in to this computer.");
    auto *autostartRemoveRow = new QHBoxLayout;
    autostartRemoveRow->addWidget(m_autostartRemoveButton);
    autostartRemoveRow->addStretch();

    // Reflect the real on-disk state in the label + button after every change,
    // so the UI never claims autostart is off while the entry is still present.
    auto refreshAutostartInfo = [this]() {
        const bool on = isAutostartEnabled();
        m_autostartInfo->setText(
            (on ? QStringLiteral("Installed as a %1:\n%2")
                : QStringLiteral("Not installed. Would be added as a %1 at:\n%2"))
                .arg(autostartMechanismName(), autostartLocation()));
        m_autostartRemoveButton->setEnabled(on);
    };
    refreshAutostartInfo();

    // After enabling/disabling, re-seed the checkbox from disk so it matches
    // reality even if the write or delete failed — the original bug was that
    // unchecking left the entry in place and ForkMesh kept starting (#393).
    auto applyAutostart = [this, refreshAutostartInfo](bool enable) {
        if (!setAutostartEnabled(enable)) {
            QMessageBox::warning(
                this, "Autostart",
                QStringLiteral(
                    "Couldn't %1 login autostart.\n\n%2\n\nThe entry may be "
                    "owned by another user (for example, left by a root "
                    "installer), so you may need to remove it manually.")
                    .arg(enable ? QStringLiteral("enable")
                                : QStringLiteral("disable"),
                         autostartLocation()));
        }
        m_autostartCheck->blockSignals(true);
        m_autostartCheck->setChecked(isAutostartEnabled());
        m_autostartCheck->blockSignals(false);
        refreshAutostartInfo();
    };

    connect(m_autostartCheck, &QCheckBox::toggled, this,
            [applyAutostart](bool enabled) { applyAutostart(enabled); });
    connect(m_autostartRemoveButton, &QPushButton::clicked, this,
            [applyAutostart]() { applyAutostart(false); });

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
    auto *autoRestartCheck = new QCheckBox(
        "Allow automatic app restarts for update/install");
    autoRestartCheck->setChecked(
        QSettings().value(kAutoUpdateRestartSetting, true).toBool());
    autoRestartCheck->setToolTip(
        "When disabled, automatic update checks never relaunch ForkMesh "
        "automatically. Use manual update/restart actions to apply updates.");
    connect(autoRestartCheck, &QCheckBox::toggled, this,
            [](bool enabled) {
                QSettings().setValue(kAutoUpdateRestartSetting, enabled);
            });

    // Boot-scoped KVM workspace. The Qt desktop remains on the host so native
    // display, keychain and notifications keep working; every coding-agent CLI
    // and its subprocesses run in a Lima QEMU/KVM guest. Only the directory
    // ForkMesh was launched from, linked Git metadata required by that checkout,
    // and a dedicated temporary worktree root are mounted writable.
    auto *vmLabel = new QLabel("KVM WORKSPACE");
    vmLabel->setObjectName("sectionLabel");
    auto *vmHint = new QLabel(
        "Run ForkMesh coding work behind a separate Linux kernel. The guest "
        "receives the directory this process was launched from, while host "
        "HOME, desktop sockets, SSH agents and unrelated environment variables "
        "stay outside the VM.");
    vmHint->setObjectName("modeHint");
    vmHint->setWordWrap(true);

    auto *vmCheck =
        new QCheckBox("Run coding agents in an isolated KVM virtual machine");
    vmCheck->setObjectName("kvmWorkspaceCheck");
    vmCheck->setChecked(
        QSettings().value(forkmesh::vm::kEnabledSetting, false).toBool());
    vmCheck->setToolTip(
        "Linux only. Uses Lima's QEMU driver with /dev/kvm and a deterministic "
        "per-directory guest. Changing this setting requires a ForkMesh restart.");

    auto *vmStatus = new QLabel;
    vmStatus->setObjectName("kvmWorkspaceStatus");
    vmStatus->setWordWrap(true);
    vmStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *vmPrepareButton = new QPushButton("Prepare / start VM");
    vmPrepareButton->setObjectName("ghostButton");
    vmPrepareButton->setCursor(Qt::PointingHandCursor);
    vmPrepareButton->setToolTip(
        "Create this directory's minimal Lima QEMU instance, or start it if it "
        "already exists. The first image download can take several minutes.");
    auto *vmShellButton = new QPushButton("Open VM shell…");
    vmShellButton->setObjectName("ghostButton");
    vmShellButton->setCursor(Qt::PointingHandCursor);
    vmShellButton->setToolTip(
        "Open a terminal inside the guest to install and sign in to Claude Code, "
        "Codex, or another configured agent. Host login files are not mounted.");
    auto *vmRestartButton = new QPushButton("Restart ForkMesh now");
    vmRestartButton->setObjectName("primaryButton");
    vmRestartButton->setCursor(Qt::PointingHandCursor);
    vmRestartButton->setToolTip(
        "Relaunch without rebuilding so the saved KVM setting becomes active.");

    auto *vmButtonRow = new QHBoxLayout;
    vmButtonRow->setContentsMargins(0, 0, 0, 0);
    vmButtonRow->addWidget(vmPrepareButton);
    vmButtonRow->addWidget(vmShellButton);
    vmButtonRow->addWidget(vmRestartButton);
    vmButtonRow->addStretch();

    const auto refreshVmStatus =
        [vmCheck, vmStatus, vmPrepareButton, vmShellButton, vmRestartButton]() {
            const bool desired =
                QSettings()
                    .value(forkmesh::vm::kEnabledSetting, false)
                    .toBool();
            const bool restartRequired = desired != forkmesh::vm::active();
            const QString unavailable = forkmesh::vm::availabilityError();
            QString text;
            if (!unavailable.isEmpty()) {
                text = unavailable;
            } else if (desired) {
                text = QStringLiteral(
                           "%1 KVM instance: %2\nWorkspace: %3")
                           .arg(forkmesh::vm::active()
                                    ? QStringLiteral("Active.")
                                    : QStringLiteral("Enabled for next launch."),
                                forkmesh::vm::instanceName(),
                                QDir::toNativeSeparators(
                                    forkmesh::vm::workspaceRoot()));
            } else {
                text = forkmesh::vm::active()
                           ? QStringLiteral(
                                 "Disabled for next launch; this process is "
                                 "still using KVM isolation.")
                           : QStringLiteral(
                                 "Off. Coding agents run directly on this host.");
            }
            if (restartRequired)
                text += QStringLiteral("\nRestart required to apply this change.");
            vmStatus->setText(text);
            vmCheck->blockSignals(true);
            vmCheck->setChecked(desired);
            vmCheck->blockSignals(false);
            // An active-but-broken setup must remain switchable off.
            vmCheck->setEnabled(unavailable.isEmpty() || desired ||
                                forkmesh::vm::active());
            vmPrepareButton->setEnabled(desired && unavailable.isEmpty());
            vmShellButton->setEnabled(desired && unavailable.isEmpty());
            vmRestartButton->setVisible(restartRequired);
        };
    refreshVmStatus();

    auto *vmProcess = new QProcess(page);
    vmProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(vmProcess, &QProcess::finished, page,
            [vmProcess, vmStatus, vmPrepareButton, vmShellButton,
             vmRestartButton, refreshVmStatus](int exitCode,
                                               QProcess::ExitStatus exitStatus) {
                const QString output =
                    QString::fromUtf8(vmProcess->readAll()).trimmed();
                const QString phase =
                    vmProcess->property("forkmeshVmPhase").toString();
                if (phase == QLatin1String("inspect")) {
                    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                        refreshVmStatus();
                        vmStatus->setText(
                            vmStatus->text() +
                            QStringLiteral("\nCould not inspect Lima instances%1%2")
                                .arg(output.isEmpty() ? QStringLiteral(".")
                                                      : QStringLiteral(": "),
                                     output.right(800)));
                        vmShellButton->setEnabled(false);
                        vmRestartButton->setEnabled(true);
                        return;
                    }

                    const QStringList names =
                        output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    if (names.contains(forkmesh::vm::instanceName())) {
                        vmProcess->setProperty("forkmeshVmPhase", "start");
                        vmStatus->setText(
                            QStringLiteral("Starting KVM instance %1…")
                                .arg(forkmesh::vm::instanceName()));
                        vmProcess->start(forkmesh::vm::limactlProgram(),
                                         forkmesh::vm::startArguments());
                    } else {
                        QDir().mkpath(forkmesh::vm::worktreeRoot());
                        vmProcess->setProperty("forkmeshVmPhase", "create");
                        vmStatus->setText(
                            "Creating the QEMU/KVM guest and downloading its "
                            "base image…");
                        vmProcess->start(forkmesh::vm::limactlProgram(),
                                         forkmesh::vm::createArguments());
                    }
                    return;
                }

                if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                    refreshVmStatus();
                    vmStatus->setText(
                        vmStatus->text() +
                        QStringLiteral(
                            "\nVM ready. Open its shell to install/sign in to "
                            "the agent CLIs, then restart ForkMesh if prompted."));
                    vmRestartButton->setEnabled(true);
                    return;
                }

                refreshVmStatus();
                vmStatus->setText(
                    vmStatus->text() +
                    QStringLiteral("\nVM preparation failed%1%2")
                        .arg(output.isEmpty() ? QStringLiteral(".")
                                              : QStringLiteral(": "),
                             output.right(800)));
                vmPrepareButton->setEnabled(
                    QSettings()
                        .value(forkmesh::vm::kEnabledSetting, false)
                        .toBool() &&
                    forkmesh::vm::availabilityError().isEmpty());
                vmShellButton->setEnabled(false);
                vmRestartButton->setEnabled(true);
            });
    connect(vmProcess, &QProcess::errorOccurred, page,
            [vmProcess, vmStatus, vmPrepareButton,
             vmRestartButton](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                vmStatus->setText(
                    QStringLiteral("Could not start limactl: %1")
                        .arg(vmProcess->errorString()));
                vmPrepareButton->setEnabled(
                    QSettings()
                        .value(forkmesh::vm::kEnabledSetting, false)
                        .toBool());
                vmRestartButton->setEnabled(true);
            });

    connect(vmPrepareButton, &QPushButton::clicked, page,
            [vmProcess, vmStatus, vmPrepareButton, vmShellButton,
             vmRestartButton] {
                if (vmProcess->state() != QProcess::NotRunning)
                    return;
                const QString unavailable = forkmesh::vm::availabilityError();
                if (!unavailable.isEmpty()) {
                    vmStatus->setText(unavailable);
                    return;
                }
                QDir().mkpath(forkmesh::vm::worktreeRoot());
                vmProcess->setProperty("forkmeshVmPhase", "inspect");
                vmPrepareButton->setEnabled(false);
                vmShellButton->setEnabled(false);
                vmRestartButton->setEnabled(false);
                vmStatus->setText(QStringLiteral("Checking KVM instance %1…")
                                      .arg(forkmesh::vm::instanceName()));
                vmProcess->start(forkmesh::vm::limactlProgram(),
                                 forkmesh::vm::listArguments());
            });

    connect(vmCheck, &QCheckBox::toggled, page,
            [vmPrepareButton, refreshVmStatus](bool enabled) {
                QSettings settings;
                settings.setValue(forkmesh::vm::kEnabledSetting, enabled);
                settings.sync();
                refreshVmStatus();
                if (enabled && forkmesh::vm::availabilityError().isEmpty())
                    vmPrepareButton->click();
            });
    connect(vmRestartButton, &QPushButton::clicked, this,
            &MainWindow::relaunchForkMesh);
    connect(vmShellButton, &QPushButton::clicked, this, [this] {
        const QString command = forkmesh::vm::interactiveCommand();
        if (command.isEmpty()) {
            flashMessage("limactl is unavailable; prepare the VM first.", true);
            return;
        }
        auto *dialog = new QDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle("ForkMesh KVM workspace");
        dialog->resize(900, 560);
        auto *layout = new QVBoxLayout(dialog);
        auto *notice = new QLabel(
            QStringLiteral(
                "This shell is inside <b>%1</b>. Install and sign in to the "
                "agent CLIs here; provider credentials remain in the guest. "
                "The writable project mount is <code>%2</code>.")
                .arg(forkmesh::vm::instanceName().toHtmlEscaped(),
                     forkmesh::vm::workspaceRoot().toHtmlEscaped()),
            dialog);
        notice->setWordWrap(true);
        layout->addWidget(notice);
        auto *terminal = new TerminalWidget(dialog);
        layout->addWidget(terminal, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
        layout->addWidget(buttons);
        dialog->show();
        terminal->runCommand(command, forkmesh::vm::workspaceRoot(), {}, false);
        terminal->setFocus();
    });

    // There is no "open repositories on tab" preference any more (adhoc #119): a
    // repo opens on its Code overview, and a relaunch restores the tab last
    // viewed. See kRepoLandingTab.

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

    auto *excludeExternalClaudeCheck =
        new QCheckBox("Exclude external Claude Code agents from the Agents tab");
    excludeExternalClaudeCheck->setChecked(
        QSettings().value(kExcludeExternalClaudeSetting, true).toBool());
    excludeExternalClaudeCheck->setToolTip(
        "Don't detect or list `claude` CLI sessions running outside ForkMesh "
        "(started directly in a terminal) in a repository's Agents tab. On by "
        "default so another process's transcripts aren't surfaced unprompted.");
    connect(excludeExternalClaudeCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kExcludeExternalClaudeSetting, enabled);
    });

    auto *publishAgentsToWebCheck =
        new QCheckBox("Relay owner-encrypted agent snapshots");
    publishAgentsToWebCheck->setChecked(
        QSettings().value(kPublishAgentsToWebSetting, false).toBool());
    publishAgentsToWebCheck->setToolTip(
        "When on, agent sessions you start here are hybrid-encrypted to this "
        "owner device before their snapshots reach the relay. The browser and "
        "platform administrators cannot decrypt transcripts or prompts; use "
        "the owner desktop to inspect or steer them. Off by default so no "
        "agent metadata leaves this machine.");
    connect(publishAgentsToWebCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kPublishAgentsToWebSetting, enabled);
    });

    auto *notifyLabel = new QLabel("PINGS");
    notifyLabel->setObjectName("sectionLabel");
    auto *inAppPingsCheck =
        new QCheckBox("Show in-app notification cards above the prompt");
    inAppPingsCheck->setChecked(
        QSettings().value(kInAppNotificationsSetting, true).toBool());
    inAppPingsCheck->setToolTip(
        "Show every in-app ping in a floating stack above the prompt, wherever "
        "you are in ForkMesh. Pings remain available on the Pings page when off.");
    connect(inAppPingsCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kInAppNotificationsSetting, enabled);
    });
    auto *inAppPingDuration = new QComboBox;
    inAppPingDuration->addItem("In-app pings: 5 seconds", 5);
    inAppPingDuration->addItem("In-app pings: 10 seconds", 10);
    inAppPingDuration->addItem("In-app pings: 20 seconds", 20);
    const int savedInAppDuration =
        QSettings().value(kInAppNotificationDurationSetting, 5).toInt();
    const int inAppDurationIndex = inAppPingDuration->findData(savedInAppDuration);
    inAppPingDuration->setCurrentIndex(inAppDurationIndex < 0 ? 0
                                                               : inAppDurationIndex);
    inAppPingDuration->setToolTip(
        "Choose how long each in-app notification card remains visible. Hovering "
        "a card pauses its countdown.");
    connect(inAppPingDuration, &QComboBox::currentIndexChanged, this,
            [inAppPingDuration](int) {
                QSettings().setValue(kInAppNotificationDurationSetting,
                                     inAppPingDuration->currentData().toInt());
            });
    auto *errorLogAlertCheck =
        new QCheckBox("Flash the window when an error is logged");
    errorLogAlertCheck->setChecked(
        QSettings().value(kErrorLogAlertSetting, true).toBool());
    errorLogAlertCheck->setToolTip(
        "Pulse the red window border and show the failure as a card whenever an "
        "error reaches the log, so a background failure isn't missed while the "
        "Log section is closed. The Log keeps every error either way.");
    connect(errorLogAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kErrorLogAlertSetting, enabled);
    });
    auto *systemAlertCheck =
        new QCheckBox("Show a card for ForkMesh system alerts");
    systemAlertCheck->setChecked(
        QSettings().value(kSystemAlertSetting, true).toBool());
    systemAlertCheck->setToolTip(
        "Pop up a card (naming the specific system, e.g. \"Relay needs "
        "attention\") when a mesh system goes down or recovers. The Pings "
        "page keeps logging these either way.");
    connect(systemAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kSystemAlertSetting, enabled);
    });
    auto *agentDoneAlertCheck =
        new QCheckBox("Show a desktop notification when an agent finishes");
    agentDoneAlertCheck->setChecked(
        QSettings().value(kAgentDoneAlertSetting, true).toBool());
    agentDoneAlertCheck->setToolTip(
        "Pop up a native OS notification (naming the agent, e.g. \"Agent #12 "
        "is done!\") when a run finishes while ForkMesh isn't the active "
        "window. The in-app celebration card always shows either way.");
    connect(agentDoneAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAgentDoneAlertSetting, enabled);
    });
    auto *pushAlertCheck =
        new QCheckBox("Show a system ping when a push reaches a mirror");
    pushAlertCheck->setChecked(
        QSettings().value(kPushAlertSetting, false).toBool());
    pushAlertCheck->setToolTip(
        "Pop up a desktop notification with the repo, branch and commit "
        "whenever someone pushes to one of this machine's mirrors.");
    connect(pushAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kPushAlertSetting, enabled);
    });
    auto *actionAlertCombo = new QComboBox;
    actionAlertCombo->addItem("Action finish pings: all runs",
                              QStringLiteral("all"));
    actionAlertCombo->addItem("Action pings: failures only",
                              QStringLiteral("failed"));
    actionAlertCombo->addItem("Action pings: off", QStringLiteral("none"));
    actionAlertCombo->setToolTip(
        "Desktop notifications for .forkmesh/ workflows on run completion: pop "
        "one for every run (except manually stopped runs), only when a run "
        "fails, or never. The in-app Pings page logs every run regardless.");
    {
        const int idx = actionAlertCombo->findData(actionAlertMode());
        actionAlertCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(actionAlertCombo, &QComboBox::currentIndexChanged, this,
            [actionAlertCombo](int) {
                QSettings().setValue(kActionAlertModeSetting,
                                     actionAlertCombo->currentData().toString());
            });
    auto *actionAlertStartedCheck =
        new QCheckBox("Show a system ping when an action starts");
    actionAlertStartedCheck->setChecked(
        QSettings().value(kActionAlertStartedSetting, false).toBool());
    actionAlertStartedCheck->setToolTip(
        "Opt in to a desktop pop-up when an action run enters the running "
        "state. The Pings page still logs the event regardless.");
    connect(actionAlertStartedCheck, &QCheckBox::toggled, this,
            [](bool enabled) {
                QSettings().setValue(kActionAlertStartedSetting, enabled);
            });
    auto *nodeConnectAlertCheck =
        new QCheckBox("Show a system ping when a node connects");
    nodeConnectAlertCheck->setChecked(
        QSettings().value(kNodeConnectAlertSetting, false).toBool());
    nodeConnectAlertCheck->setToolTip(
        "Pop up a desktop notification when another node comes online on this "
        "network.");
    connect(nodeConnectAlertCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kNodeConnectAlertSetting, enabled);
    });
    auto *disbursementAlertCheck =
        new QCheckBox(
            "Show a system ping when your public wallet balance increases");
    disbursementAlertCheck->setChecked(
        QSettings().value(kDisbursementAlertSetting, false).toBool());
    disbursementAlertCheck->setToolTip(
        "Pop up a desktop notification when the read-only balance of your "
        "external self-custodial Solana address increases after a refresh.");
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
        "Show a system ping for new chat messages", kChatMessageAlertSetting,
        "Pop up a desktop notification when a chat message arrives while ForkMesh "
        "isn't the active window.");
    auto *mentionAlertCheck = alertCheck(
        "Show a system ping when you're @mentioned", kMentionAlertSetting,
        "Pop up a desktop notification when your node name is mentioned in chat or "
        "in an issue/pull request.");
    auto *issueAlertCheck = alertCheck(
        "Show a system ping for new issues", kIssueAlertSetting,
        "Pop up a desktop notification when another node files an issue on one of "
        "your repositories.");
    auto *pullAlertCheck = alertCheck(
        "Show a system ping for new pull requests", kPullAlertSetting,
        "Pop up a desktop notification when another node opens a pull request on "
        "one of your repositories.");
    auto *commentAlertCheck = alertCheck(
        "Show a system ping for new issue comments", kCommentAlertSetting,
        "Pop up a desktop notification when someone comments on one of your "
        "issues.");
    auto *mirrorUpdateAlertCheck = alertCheck(
        "Show a system ping when a mirror updates", kMirrorUpdateAlertSetting,
        "Pop up a desktop notification when a peer refreshes the mirror of a repo "
        "you also mirror.");
    auto *coveOpenAlertCheck = alertCheck(
        "Show a system ping when a cove is opened", kCoveOpenAlertSetting,
        "Pop up a desktop notification when someone opens an encrypted cove you "
        "created with notifications enabled.");
    auto *newUserAlertCheck = alertCheck(
        "Show a system ping when a new user joins", kNewUserAlertSetting,
        "Admin: pop up a desktop notification when a new user signs up and needs "
        "email verification.");

    auto *emailNotifyLabel = new QLabel("EMAIL DIGESTS");
    emailNotifyLabel->setObjectName("sectionLabel");
    auto emailPrefCheck = [this](const QString &label, const QString &key,
                                 bool defaultOn, const QString &tip) {
        auto *box = new QCheckBox(label);
        box->setChecked(QSettings().value(key, defaultOn).toBool());
        box->setToolTip(tip);
        connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
            QSettings().setValue(key, on);
            sendNodeHeartbeat();
        });
        return box;
    };
    auto *emailMentionCheck = emailPrefCheck(
        "Email me when someone mentions me", kEmailNotifyMentionSetting, true,
        "Email digest entry for @mentions.");
    auto *emailSubscribedCheck = emailPrefCheck(
        "Email me replies on subscribed threads", kEmailNotifySubscribedSetting,
        true, "Email digest entry for watched issue and pull request replies.");
    auto *emailPullCheck = emailPrefCheck(
        "Email me submitted pull requests", kEmailNotifyPullSubmittedSetting,
        true, "Email digest entry when a pull request is submitted.");
    auto *emailIssueAssignedCheck = emailPrefCheck(
        "Email me issue assignments", kEmailNotifyIssueAssignedSetting, true,
        "Email digest entry when an issue is assigned to this account.");
    auto *emailRepoSharedCheck = emailPrefCheck(
        "Email me shared repositories", kEmailNotifyRepoSharedSetting, true,
        "Email digest entry when a private repository is shared with this account.");
    auto *emailPendingInboxCheck = emailPrefCheck(
        "Email me pending inbox submissions", kEmailNotifyPendingInboxSetting,
        true, "Email digest entry when signed submissions are waiting.");
    auto *emailReleaseCheck = emailPrefCheck(
        "Email me published releases", kEmailNotifyReleasePublishedSetting, true,
        "Email digest entry when a release is published.");
    auto *emailCreditsCheck = emailPrefCheck(
        "Email me when credits refill", kEmailNotifyCreditsRefilledSetting, true,
        "Email digest entry when this machine reports Claude Code credits refilled.");
    auto *emailBountyFundedCheck = emailPrefCheck(
        "Email me funded bounties", kEmailNotifyBountyFundedSetting, true,
        "Email digest entry when a bounty is funded.");
    auto *emailBountyPaidCheck = emailPrefCheck(
        "Email me paid bounties", kEmailNotifyBountyPaidSetting, true,
        "Email digest entry when a bounty is paid.");
    auto *emailGeneralChatCheck = emailPrefCheck(
        "Email me a daily #general count", kEmailNotifyGeneralChatSetting, true,
        "Daily email count of encrypted #general messages; message contents stay "
        "out of email.");
    auto *emailHostOnlineCheck = emailPrefCheck(
        "Email me when a desktop host is reachable", kEmailNotifyHostOnlineSetting,
        false, "Email digest entry when a mirror endpoint has a recent signed check-in.");
    auto *emailHostOfflineCheck = emailPrefCheck(
        "Email me when no mirror endpoint has checked in",
        kEmailNotifyHostOfflineSetting, false,
        "Email digest entry when a repository has no recent signed mirror-endpoint check-in.");

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

    // Hosted World -> local speech-to-text pairing. The exact public origin is
    // safe to persist; the one-use capability and browser-session capability
    // remain memory-only and are revoked when the bridge/client stops.
    auto *worldVoiceLabel = new QLabel("WORLD VOICE BRIDGE");
    worldVoiceLabel->setObjectName("sectionLabel");
    auto *worldVoiceHint = new QLabel(
        "Let one exact ForkMesh World origin control this local microphone and "
        "speech engine. Audio never enters the browser, Worker, relay, or mirror. "
        "Create a short-lived code, paste it into the World voice panel, and "
        "revoke it at any time.");
    worldVoiceHint->setObjectName("statusLine");
    worldVoiceHint->setWordWrap(true);
    m_worldSpeechOriginEdit = new QLineEdit;
    m_worldSpeechOriginEdit->setMaxLength(2048);
    m_worldSpeechOriginEdit->setPlaceholderText("https://forkmesh.com");
    m_worldSpeechOriginEdit->setText(
        QSettings()
            .value(kWorldSpeechOriginSetting,
                   QStringLiteral("https://forkmesh.com"))
            .toString());
    m_worldSpeechOriginEdit->setToolTip(
        "Exact http(s) origin allowed to pair. Paths, wildcards, query strings, "
        "and null origins are rejected.");
    m_worldSpeechPairCodeEdit = new QLineEdit;
    m_worldSpeechPairCodeEdit->setReadOnly(true);
    m_worldSpeechPairCodeEdit->setPlaceholderText(
        "One-time code appears here");
    m_worldSpeechPairCodeEdit->setToolTip(
        "One-use capability. It expires in two minutes and is never saved.");
    m_worldSpeechPairButton =
        new QPushButton(QStringLiteral("Create pairing code"));
    m_worldSpeechPairButton->setObjectName("ghostButton");
    m_worldSpeechPairButton->setCursor(Qt::PointingHandCursor);
    connect(m_worldSpeechPairButton, &QPushButton::clicked, this,
            &MainWindow::createWorldSpeechPairing);
    m_worldSpeechRevokeButton = new QPushButton(QStringLiteral("Revoke"));
    m_worldSpeechRevokeButton->setObjectName("ghostButton");
    m_worldSpeechRevokeButton->setCursor(Qt::PointingHandCursor);
    m_worldSpeechRevokeButton->setEnabled(false);
    connect(m_worldSpeechRevokeButton, &QPushButton::clicked, this,
            &MainWindow::revokeWorldSpeechPairing);
    auto *worldVoiceOriginRow = new QHBoxLayout;
    worldVoiceOriginRow->setSpacing(8);
    worldVoiceOriginRow->addWidget(new QLabel(QStringLiteral("Exact origin")));
    worldVoiceOriginRow->addWidget(m_worldSpeechOriginEdit, 1);
    auto *worldVoicePairRow = new QHBoxLayout;
    worldVoicePairRow->setSpacing(8);
    worldVoicePairRow->addWidget(m_worldSpeechPairCodeEdit, 1);
    worldVoicePairRow->addWidget(m_worldSpeechPairButton);
    worldVoicePairRow->addWidget(m_worldSpeechRevokeButton);
    m_worldSpeechStatusLabel = new QLabel(
        QStringLiteral("Bridge off. No browser can control the microphone."));
    m_worldSpeechStatusLabel->setObjectName("statusLine");
    m_worldSpeechStatusLabel->setWordWrap(true);

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
    // The SOL/USD/INR balance-currency picker is gone with the balance's move
    // under the account avatar (adhoc #96): that line is a tiny always-SOL
    // figure, so there is no display currency left to choose.
    auto *rebuildButtonCheck =
        new QCheckBox("Show a rebuild & restart button in the top bar");
    rebuildButtonCheck->setChecked(
        QSettings().value(kShowRebuildButtonSetting, false).toBool());
    rebuildButtonCheck->setToolTip(
        "Adds a small rebuild & restart button in the top navigation (under the "
        "avatar) for a fast local rebuild and relaunch. Off by default.");
    connect(rebuildButtonCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        QSettings().setValue(kShowRebuildButtonSetting, enabled);
        updateNavRebuildButton();
    });

    // The footer's debug bar normally waits for a click on the version button.
    // This opens it at launch instead (adhoc #1632). With nothing stored the box
    // shows the account's default — ticked for admins, who are the ones watching
    // the resource chart and Worker status dots — and ticking or unticking it
    // both records the choice and applies it to the window right away.
    auto *debugBarStartupCheck =
        new QCheckBox("Show the debug bar at startup");
    m_debugBarStartupCheck = debugBarStartupCheck;
    debugBarStartupCheck->setObjectName(QStringLiteral("debugBarStartupCheck"));
    debugBarStartupCheck->setChecked(
        QSettings().value(kShowDebugBarOnStartupSetting, m_isAdmin).toBool());
    debugBarStartupCheck->setToolTip(
        "Open the debug bar under the status line as soon as the app starts, "
        "with its resource chart, log activity lights and Worker tools. On by "
        "default for admins; the version button in the footer toggles it at any "
        "time.");
    connect(debugBarStartupCheck, &QCheckBox::toggled, this,
            [this](bool enabled) {
                QSettings().setValue(kShowDebugBarOnStartupSetting, enabled);
                // An explicit choice retires the admin default for this run.
                m_debugBarStartupApplied = true;
                if (m_statusVersionButton)
                    m_statusVersionButton->setChecked(enabled);
            });

    // The cloud monitor's on/off switch. It is on by default (adhoc #1632) and,
    // since the tail's events are now log lines rather than a window of their
    // own (adhoc #1613), this is the one place it is switched.
    m_cloudLogMonitorSettingCheck =
        new QCheckBox("Watch the deployed Worker's log");
    m_cloudLogMonitorSettingCheck->setObjectName(
        QStringLiteral("cloudLogMonitorSettingCheck"));
    m_cloudLogMonitorSettingCheck->setChecked(
        QSettings().value(kCloudLogMonitorSetting, true).toBool());
    m_cloudLogMonitorSettingCheck->setToolTip(
        "Keep a background tail of the deployed Worker's live log running. Its "
        "traffic joins this app's own log under the Cloud filter, and its "
        "exceptions and 5xx responses raise the same alert as any other error. "
        "Needs a stored Cloudflare API token; on by default.");
    connect(m_cloudLogMonitorSettingCheck, &QCheckBox::toggled, this,
            [this](bool enabled) {
                QSettings().setValue(kCloudLogMonitorSetting, enabled);
                setCloudLogMonitorEnabled(enabled);
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
    // with a fixed bounty through a one-time funding QR. The removed legacy
    // "wallet" mode put signing authority in the Worker and must never be
    // selectable for new rewards.
    auto *bountyLabel = new QLabel("PR BOUNTIES");
    bountyLabel->setObjectName("sectionLabel");
    auto *autoBountyCheck =
        new QCheckBox("Reward every merged pull request");
    autoBountyCheck->setObjectName(
        QStringLiteral("legacyAutoPrBountyDisabled"));
    QSettings().setValue(kAutoPrBountyEnabledSetting, false);
    autoBountyCheck->setChecked(false);
    autoBountyCheck->setEnabled(false);
    autoBountyCheck->setToolTip(
        "Disabled: the historical implementation required Worker-held escrow "
        "keys. A reviewed non-custodial replacement has not been implemented.");
    auto *bountyHint = new QLabel(
        "Automatic PR bounty funding is unavailable while the historical "
        "Worker-held escrow is retired. Existing records are retained only for "
        "audit and operator-assisted migration; no new funds are accepted.");
    bountyHint->setObjectName("modeHint");
    bountyHint->setWordWrap(true);

    auto *bountyAmount = new QDoubleSpinBox;
    bountyAmount->setRange(1.0, 100000.0);
    bountyAmount->setDecimals(2);
    bountyAmount->setPrefix("$");
    bountyAmount->setValue(
        QSettings().value(kAutoPrBountyAmountSetting, 1.0).toDouble());
    bountyAmount->setEnabled(false);
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
    bountyModeCombo->setObjectName(QStringLiteral("prBountyFundingMode"));
    bountyModeCombo->addItem("Legacy per-PR mode (disabled)",
                             QStringLiteral("perPr"));
    QSettings bountySettings;
    // One-way local migration: never silently reactivate the removed Worker-held
    // signing path from an older preference.
    if (bountySettings
            .value(kAutoPrBountyModeSetting, QStringLiteral("perPr"))
            .toString() != QLatin1String("perPr"))
        bountySettings.setValue(kAutoPrBountyModeSetting,
                                QStringLiteral("perPr"));
    bountyModeCombo->setCurrentIndex(0);
    bountyModeCombo->setEnabled(false);
    bountyModeCombo->setToolTip(
        "Reserved for a future reviewed self-custodial payment flow. It cannot "
        "be selected in this release.");
    auto *legacyWalletNotice = new QLabel(
        "Legacy Worker-held bounty wallets are disabled and migration-only. "
        "Do not send new funds to an old address.");
    legacyWalletNotice->setObjectName(
        QStringLiteral("legacyBountyWalletDisabled"));
    legacyWalletNotice->setWordWrap(true);
    auto *bountyModeRow = new QHBoxLayout;
    bountyModeRow->setSpacing(8);
    bountyModeRow->addWidget(bountyModeCombo);
    bountyModeRow->addStretch();

    const auto syncBountyEnabled = [autoBountyCheck, bountyAmount,
                                    bountyModeCombo] {
        // The Worker-held escrow implementation is frozen. This deliberately
        // remains disabled even if an old settings file had the feature on.
        autoBountyCheck->setChecked(false);
        bountyAmount->setEnabled(false);
        bountyModeCombo->setEnabled(false);
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
    syncBountyEnabled();

    // Per-metric toggles for this node's host stats (CPU/RAM/disk) shown in the
    // Mirror nodes view. Off by default on the desktop so a personal machine
    // doesn't broadcast its load; headless installs are seeded on at startup so
    // hosts stay monitorable (adhoc #23). Re-sampled within ~10s of a change.
    auto *nodeStatsLabel = new QLabel("NODE STATS");
    nodeStatsLabel->setObjectName("sectionLabel");
    auto *nodeStatsHint = new QLabel(
        "Show this machine's resource use to other nodes in the Mirror nodes "
        "view and in public mirror and World server views. Off by default; "
        "servers installed from the Hosts tab report all three. Turning a "
        "metric off leaves it unknown publicly rather than reporting zero.");
    nodeStatsHint->setObjectName("statusLine");
    nodeStatsHint->setWordWrap(true);
    struct NodeStatToggle {
        const char *label;
        const QString &key;
        bool defaultOn;
        const char *tip;
    };
    const NodeStatToggle nodeStatToggles[] = {
        {"Report CPU usage", TelemetrySettings::kReportCpu, false, nullptr},
        {"Report memory usage", TelemetrySettings::kReportMemory, false, nullptr},
        {"Report disk usage", TelemetrySettings::kReportDisk, false, nullptr},
        // On by default, unlike the three gauges: these are the problems nobody
        // can see from the outside (adhoc #27), and they carry findings rather
        // than load figures. The one caveat worth stating is the log summary.
        {"Report self-diagnostics", TelemetrySettings::kReportDiagnostics, true,
         "Run periodic health checks (disk filling up, inode and file-descriptor "
         "pressure, defunct processes, relay link flapping, clock drift, errors "
         "in this node's log) and push the findings to every node list. Includes "
         "a short excerpt of the newest error line from this node's own log."},
    };
    QList<QCheckBox *> nodeStatChecks;
    for (const NodeStatToggle &toggle : nodeStatToggles) {
        auto *check = new QCheckBox(QString::fromUtf8(toggle.label));
        if (toggle.tip)
            check->setToolTip(QString::fromUtf8(toggle.tip));
        check->setChecked(
            QSettings().value(toggle.key, toggle.defaultOn).toBool());
        const QString key = toggle.key;
        connect(check, &QCheckBox::toggled, this, [this, key](bool enabled) {
            QSettings().setValue(key, enabled);
            if (m_backend)
                m_backend->advertiseMirrorsNow();
            // The backend coalesces its freshly sampled self-roster update for
            // 200 ms. Publish after that value reaches m_homeRoster so enabling
            // shows a real reading and disabling promptly removes the formerly
            // public field from each signed catalog record.
            QTimer::singleShot(500, this, [this] {
                for (int i = 0; i < m_repositories.size(); ++i)
                    publishRepository(i, false);
            });
        });
        nodeStatChecks.append(check);
    }

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
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Codex"), kCodexProvider);
    m_defaultAgentProviderCombo->addItem(QStringLiteral("OpenAI API"),
                                         QStringLiteral("openai"));
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Claude API"),
                                         QStringLiteral("claude-api"));
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Claude Code"),
                                         QStringLiteral("claude-code"));
    m_defaultAgentProviderCombo->addItem(QStringLiteral("Cloudflare AI"),
                                         kCloudflareAiProvider);
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

    // Which models the composer's prompt dropdown offers (adhoc #1557). Every
    // installed provider's whole line-up adds up to a long menu, and most people
    // work with a handful of them; unticking the rest keeps the picker short
    // without uninstalling a provider or losing the model's merged history.
    //
    // Unticking is presentation only: the hidden model is still perfectly
    // runnable from the issue-detail picker and from any session already using
    // it, and the currently-selected model stays in the dropdown even when it is
    // unticked, so the composer can always show what it is about to run.
    m_composerModelVisibilityList = new QListWidget;
    m_composerModelVisibilityList->setObjectName("composerModelVisibilityList");
    m_composerModelVisibilityList->setMaximumHeight(180);
    m_composerModelVisibilityList->setSelectionMode(QAbstractItemView::NoSelection);
    m_composerModelVisibilityList->setToolTip(
        "Tick the agents and models the composer's prompt dropdown should list. "
        "Unticking one only hides it from that menu — it stays available "
        "elsewhere, and keeps its merged-success history. New models a provider "
        "adds later appear ticked.");
    refreshComposerModelVisibilityList();
    connect(m_composerModelVisibilityList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
                if (!item)
                    return;
                const QString key = item->data(Qt::UserRole).toString();
                if (key.isEmpty())
                    return;
                QSet<QString> hidden = hiddenComposerModels();
                if (item->checkState() == Qt::Checked)
                    hidden.remove(key);
                else
                    hidden.insert(key);
                saveHiddenComposerModels(hidden);
                refreshQuickAddAgentModelSelector();
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

    // Branch conflicts are no longer fixed automatically (adhoc #446): the
    // agents list flags a conflicting branch with an orange conflict button and
    // the agent is only steered into merging base when that button is clicked,
    // so there's no setting here any more.

    // When a repo's tests or build fail, automatically send the failure back
    // to the agent that last worked on that branch instead of waiting for a
    // manual dispatch. On by default.
    auto *autoFixFailuresCheck =
        new QCheckBox("Auto-fix test and build failures");
    autoFixFailuresCheck->setChecked(
        QSettings().value(kAutoFixFailuresSetting, true).toBool());
    autoFixFailuresCheck->setToolTip(
        "When a repo's tests or build fail, automatically send the failure "
        "back to the agent session that last worked on that branch to fix, "
        "instead of waiting for a manual dispatch. On by default.");
    connect(autoFixFailuresCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoFixFailuresSetting, enabled);
    });

    // Jail agents at launch (adhoc #236): each run gets its own scratch
    // environment (a private per-run tmp/cache) and a memory cap applied
    // before the CLI starts. Off by default.
    auto *jailAgentsCheck =
        new QCheckBox("Jail agents (own environment + memory cap)");
    jailAgentsCheck->setChecked(
        QSettings().value(kAgentJailSetting, false).toBool());
    jailAgentsCheck->setToolTip(
        "Start every agent in its own scratch environment — a private per-run "
        "tmp and cache instead of the shared system ones — with its memory "
        "capped at the limit below. Off by default.");
    connect(jailAgentsCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAgentJailSetting, enabled);
    });

    auto *jailMemoryEdit = new QLineEdit;
    jailMemoryEdit->setPlaceholderText(
        QString::number(kDefaultAgentJailMemoryMb));
    jailMemoryEdit->setText(QString::number(agentJailMemoryMb()));
    jailMemoryEdit->setToolTip(
        "Memory cap in MB applied to each agent when \"Jail agents\" is on.");
    connect(jailMemoryEdit, &QLineEdit::editingFinished, this, [jailMemoryEdit] {
        const int mb =
            qMax(kMinAgentJailMemoryMb, jailMemoryEdit->text().toInt());
        jailMemoryEdit->setText(QString::number(mb));
        QSettings().setValue(kAgentJailMemoryMbSetting, mb);
    });

    // Cap on how many agents run at once (adhoc #433). Anything started past the
    // cap waits in the queue with a clock icon and launches as slots free up, so
    // assigning a batch of issues can't spawn a CLI per issue all at once.
    m_maxRunningAgentsEdit = new QLineEdit;
    m_maxRunningAgentsEdit->setObjectName("maxRunningAgentsEdit");
    m_maxRunningAgentsEdit->setPlaceholderText(
        QString::number(kDefaultMaxRunningAgents));
    m_maxRunningAgentsEdit->setText(QString::number(maxRunningAgents()));
    m_maxRunningAgentsEdit->setToolTip(
        "How many agent sessions may run at the same time. Sessions started "
        "beyond this stay queued and start automatically as running ones "
        "finish. Defaults to 5.");
    connect(m_maxRunningAgentsEdit, &QLineEdit::editingFinished, this,
            [this] {
                setAgentConcurrencyLimit(m_maxRunningAgentsEdit->text().toInt());
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
    m_codexModelEdit->setPlaceholderText("Optional, e.g. gpt-5.5");
    const QString codexModel =
        codexChatGptModelId(QSettings().value(kCodexModelSetting).toString());
    m_codexModelEdit->setText(codexModel);
    QSettings().setValue(kCodexModelSetting, codexModel);
    connect(m_codexModelEdit, &QLineEdit::editingFinished, this, [this] {
        const QString model = codexChatGptModelId(m_codexModelEdit->text());
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
    agentForm->addRow("Composer models", m_composerModelVisibilityList);
    agentForm->addRow("Max running agents", m_maxRunningAgentsEdit);
    agentForm->addRow("OpenAI API key", m_codexApiKeyEdit);
    agentForm->addRow("OpenAI Admin key", m_openAiAdminKeyEdit);
    agentForm->addRow("OpenAI model", m_codexModelEdit);
    agentForm->addRow("Claude API key", m_claudeApiKeyEdit);
    agentForm->addRow("Claude Admin key", m_claudeAdminKeyEdit);
    agentForm->addRow("OpenAI command", m_codexCommandEdit);
    agentForm->addRow("Claude command", m_claudeCommandEdit);

    // Provider credentials are device-local. A host signs in through Claude's
    // own flow on that host; ForkMesh never makes a portable account bundle.
    auto *claudeAccountBtn =
        new QPushButton("Set up Claude Code on this device…");
    claudeAccountBtn->setObjectName("ghostButton");
    claudeAccountBtn->setCursor(Qt::PointingHandCursor);
    claudeAccountBtn->setToolTip(
        "Use Claude's provider-owned login on this device. ForkMesh never "
        "exports, imports, copies, or shares OAuth tokens or API keys.");
    connect(claudeAccountBtn, &QPushButton::clicked, this,
            &MainWindow::showClaudeCodeDeviceSetup);
    auto *claudeAccountRow = new QHBoxLayout;
    claudeAccountRow->addWidget(claudeAccountBtn);
    claudeAccountRow->addStretch();
    agentForm->addRow("Claude Code account", claudeAccountRow);

    agentForm->addRow("Context window", m_agentContextEdit);
    agentForm->addRow("Max output", m_agentMaxOutputEdit);
    agentForm->addRow("Jail memory cap (MB)", jailMemoryEdit);
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
        "When this machine's 5-hour or weekly Claude Code usage window was maxed "
        "out and then resets, email the account on file (requires a verified "
        "email — see the profile section above).");
    connect(emailOnRefillCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kEmailOnCreditsRefillSetting, enabled);
    });
    usageText->addWidget(emailOnRefillCheck);
    auto *usageCalendarReminderCheck = new QCheckBox(
        "Add a calendar reminder and ping me when agent usage resets");
    usageCalendarReminderCheck->setChecked(
        QSettings().value(kUsageLimitCalendarReminderSetting, false).toBool());
    usageCalendarReminderCheck->setToolTip(
        "When Claude Code or Codex usage is exhausted, open a standard calendar "
        "reminder for its reset time and show a ForkMesh system ping when it is "
        "ready again. The calendar app handles alerts while ForkMesh is closed.");
    connect(usageCalendarReminderCheck, &QCheckBox::toggled, this,
            [this](bool enabled) {
                QSettings().setValue(kUsageLimitCalendarReminderSetting, enabled);
                if (enabled)
                    restoreUsageLimitReminders();
                else
                    clearUsageLimitReminders();
            });
    usageText->addWidget(usageCalendarReminderCheck);
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

    // Auto-sync on merge (adhoc #110): several owners were surprised that hitting
    // "Merge" published the merge commit to main with no confirming click. Off by
    // default — the merge lands locally and waits behind the floating "Sync"
    // button. On → merging pushes to the served mirror and notifies peers right
    // away, the old behaviour.
    auto *pullsSyncLabel = new QLabel("PULL REQUESTS");
    pullsSyncLabel->setObjectName("sectionLabel");
    auto *autoSyncMergeCheck =
        new QCheckBox("Immediately sync merges to main");
    autoSyncMergeCheck->setChecked(
        QSettings().value(kAutoSyncOnMergeSetting, false).toBool());
    autoSyncMergeCheck->setToolTip(
        "When on, merging a pull request pushes the new merge commit to main "
        "(the served mirror) and notifies peers the moment you click \"Merge\". "
        "When off (the default), the merge lands locally only — the floating "
        "\"Sync\" button surfaces the pending commit and nothing reaches main "
        "until you click it.");
    connect(autoSyncMergeCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings().setValue(kAutoSyncOnMergeSetting, enabled);
    });

    auto *mirrorSyncLabel = new QLabel("MIRROR SYNC");
    mirrorSyncLabel->setObjectName("sectionLabel");
    auto *mirrorSyncHint = new QLabel(
        QStringLiteral(
            "How often to poll repo mirrors for source updates when a direct push"
            " notification is not received."));
    mirrorSyncHint->setObjectName("statusLine");
    mirrorSyncHint->setWordWrap(true);
    auto *mirrorSyncRow = new QHBoxLayout;
    mirrorSyncRow->setContentsMargins(0, 0, 0, 0);
    auto *mirrorSyncMinutes = new QSpinBox;
    mirrorSyncMinutes->setRange(kMirrorSyncIntervalMinMinutes,
                               kMirrorSyncIntervalMaxMinutes);
    mirrorSyncMinutes->setValue(mirrorSyncIntervalMinutes());
    mirrorSyncMinutes->setSuffix(" minutes");
    mirrorSyncMinutes->setToolTip(
        QStringLiteral(
            "Pull each mirror in the background. Longer values reduce traffic; "
            "shorter values recover faster after offline gaps."));
    connect(mirrorSyncMinutes, QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            [this](int minutes) {
                QSettings().setValue(kMirrorSyncIntervalSetting, minutes);
                restartMirrorSyncTimer();
            });
    mirrorSyncRow->addWidget(mirrorSyncMinutes, 0, Qt::AlignLeft);
    mirrorSyncRow->addStretch();

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
        "and publish it under your node. Paste a GitLab group instead of a "
        "project to import the entire organization, subgroups included. Add an "
        "access token to avoid unauthenticated rate limits (and to reach "
        "private groups) — tokens are stored locally only.");
    importHint->setObjectName("statusLine");
    importHint->setWordWrap(true);

    m_importUrlEdit = new QLineEdit;
    m_importUrlEdit->setPlaceholderText(
        "https://github.com/owner/repo  ·  https://gitlab.com/group/repo  ·  "
        "https://gitlab.com/group");
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
    // Keep the longer explanation available without giving it permanent vertical
    // space. The same info icon treatment is used for other dense settings.
    auto *varsHelpButton = new QPushButton;
    varsHelpButton->setObjectName("inlineHelpButton");
    varsHelpButton->setAccessibleName("About variables and secrets");
    varsHelpButton->setAccessibleDescription(
        "Injected into every action run's environment and redacted from logs. "
        "Add CLOUDFLARE_API_TOKEN here to let the deploy workflow authenticate, "
        "and FORKMESH_RELEASE_SIGNING_KEY_PEM (the Ed25519 private key itself) "
        "to let the release workflow sign published builds.");
    varsHelpButton->setToolTip(
        "<div style='width: 420px; white-space: normal;'>"
        + varsHelpButton->accessibleDescription().toHtmlEscaped() + "</div>");
    varsHelpButton->setCursor(Qt::PointingHandCursor);
    varsHelpButton->setFlat(true);
    varsHelpButton->setFixedSize(24, 24);
    setOcticon(varsHelpButton, "info", 14);
    auto *varsHeading = settingsHeading(varsLabel, "key", varsHelpButton);

    // Each variable gets a full-width card instead of a short, scrollable
    // table. This keeps long names and values readable and removes column
    // resizing from a settings page where the list can grow naturally.
    m_varsList = new QWidget;
    m_varsList->setObjectName("variablesList");
    m_varsListLayout = new QVBoxLayout(m_varsList);
    m_varsListLayout->setContentsMargins(0, 0, 0, 0);
    m_varsListLayout->setSpacing(8);

    auto *varAddButton = new QPushButton("Add\xE2\x80\xA6");
    auto *varExportButton = new QPushButton("Export");
    auto *varImportButton = new QPushButton("Import");
    m_varsRevealButton = new QPushButton("Reveal");
    for (QPushButton *b : {varAddButton, varExportButton, varImportButton,
                           m_varsRevealButton}) {
        b->setObjectName("ghostButton");
        b->setCursor(Qt::PointingHandCursor);
    }
    varExportButton->setToolTip(
        "Export variables and secrets to a clear-text JSON file");
    varImportButton->setToolTip(
        "Import variables and secrets from a ForkMesh JSON file");
    m_varsRevealButton->setToolTip("Show or hide the secret values in clear text");
    connect(varAddButton, &QPushButton::clicked, this,
            [this] { addOrEditVariable(); });
    connect(varExportButton, &QPushButton::clicked, this,
            &MainWindow::exportVariables);
    connect(varImportButton, &QPushButton::clicked, this,
            &MainWindow::importVariables);
    connect(m_varsRevealButton, &QPushButton::clicked, this,
            &MainWindow::toggleVariablesRevealed);
    auto *varButtonRow = new QHBoxLayout;
    varButtonRow->setContentsMargins(0, 0, 0, 0);
    varButtonRow->addWidget(varAddButton);
    varButtonRow->addWidget(varExportButton);
    varButtonRow->addWidget(varImportButton);
    varButtonRow->addWidget(m_varsRevealButton);
    varButtonRow->addStretch();

    auto *leaveButton = new QPushButton("Leave node");
    leaveButton->setObjectName("dangerButton");
    leaveButton->setCursor(Qt::PointingHandCursor);
    // Same action as the profile panel's "Disconnect": mesh only, the account
    // stays signed in. Spelled out so it is never confused with the account
    // logout sitting two buttons along (adhoc #63).
    leaveButton->setToolTip(
        "Disconnect this machine from the mesh and return to the setup "
        "screen. Your ForkMesh account stays signed in on this machine.");
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

    // Log in to a user account with email + password, without leaving the app.
    // A user account can own many nodes, so this signs this machine in to an
    // existing account (universal cross-device access).
    auto *loginButton = new QPushButton("Log in to a user account…");
    loginButton->setObjectName("ghostButton");
    loginButton->setCursor(Qt::PointingHandCursor);
    loginButton->setToolTip(
        "Sign in to an existing ForkMesh user account with your email and "
        "password. Your account can own multiple nodes.");
    setOcticon(loginButton, "sign-in", 16);
    connect(loginButton, &QPushButton::clicked, this,
            [this] { loginToUserAccount(); });

    // Log out clears the signed-in account so you can log back in (as the same
    // or a different account). "of account" is part of the label: this is the
    // only one of the app's three sign-out-ish buttons that drops the account
    // rather than just the mesh session (adhoc #63).
    auto *logoutButton = new QPushButton("Log out of account");
    logoutButton->setObjectName("ghostButton");
    logoutButton->setCursor(Qt::PointingHandCursor);
    logoutButton->setToolTip(
        "Sign this machine out of its ForkMesh user account: revokes the "
        "website session, forgets the account here, and returns to the login "
        "screen. Repositories and settings on this computer are kept.");
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
    footerRow->addWidget(loginButton);
    footerRow->addWidget(logoutButton);
    footerRow->addStretch();
    footerRow->addWidget(uninstallButton);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 14, 24, 16);
    layout->setSpacing(10);

    // Group related settings under tabs rather than one long two-column scroll.
    // Each tab scrolls on its own; the account-action footer below stays pinned
    // so it's reachable from any tab. Every tab carries an octicon beside a
    // small caption, which is what keeps eleven of them on one row of a laptop
    // screen instead of behind scroll arrows (adhoc #1533).
    auto *tabs = new QTabWidget;
    tabs->setObjectName("settingsTabs");
    tabs->setDocumentMode(true);
    tabs->tabBar()->setUsesScrollButtons(true);
    // Wrap a tab's content widget in a frameless, vertically-scrolling page.
    auto addTab = [tabs](QWidget *body, const QString &name,
                         const QString &octicon) {
        auto *scroll = new QScrollArea;
        scroll->setObjectName("settingsTabScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(body);
        const int index = tabs->addTab(scroll, name);
        setTabOcticon(tabs, index, octicon);
    };

    // One tab body: the section cards flow into two or three columns as the
    // window allows, so a page that was a single long scroll fits on a small
    // screen. addSection() hands back the layout each group stacks into.
    auto flowBody = [](int maxColumns = 3) {
        auto *body = new ColumnFlowWidget;
        body->setContentsMargins(2, 12, 2, 12);
        body->setMaximumColumns(maxColumns);
        return body;
    };

    // Profile (adhoc #274): the avatar / power-switch page that used to only be
    // reachable by clicking the avatar button. The panel itself is built once
    // with the full-page profile section and moved in here by
    // syncSettingsProfileTab() whenever this tab is showing, so it is not
    // wrapped in addTab()'s scroll area (it scrolls itself).
    m_settingsProfileHost = new QWidget;
    auto *profileHostLayout = new QHBoxLayout(m_settingsProfileHost);
    profileHostLayout->setContentsMargins(0, 0, 0, 0);
    profileHostLayout->setSpacing(0);
    m_profileSettingsTabIndex = tabs->count();
    tabs->addTab(m_settingsProfileHost, "Profile");
    setTabOcticon(tabs, m_profileSettingsTabIndex, "person");
    connect(tabs, &QTabWidget::currentChanged, this,
            [this](int) { syncSettingsProfileTab(); });

    // Quick Setup: everything a brand-new instance needs, on one page with a
    // single Apply button (adhoc #318).
    addTab(buildQuickSetupTab(), "Quick Setup", "rocket");

    // General: identity, appearance and launch behaviour.
    auto *generalTab = flowBody();

    auto *profileGroup = generalTab->addSection();
    profileGroup->addLayout(settingsHeading(profileLabel, "person"));
    profileGroup->addLayout(form);

    auto *appearanceGroup = generalTab->addSection();
    appearanceGroup->addLayout(settingsHeading(appearanceLabel, "sun"));
    appearanceGroup->addWidget(m_themeCombo, 0, Qt::AlignLeft);
    appearanceGroup->addWidget(rebuildButtonCheck);
    appearanceGroup->addWidget(debugBarStartupCheck);
    appearanceGroup->addWidget(m_cloudLogMonitorSettingCheck);
    appearanceGroup->addWidget(verboseNetLogCheck);

    auto *startupGroup = generalTab->addSection();
    startupGroup->addLayout(settingsHeading(startupLabel, "sign-in"));
    startupGroup->addWidget(m_autostartCheck);
    startupGroup->addWidget(m_autostartInfo);
    startupGroup->addLayout(autostartRemoveRow);
    startupGroup->addWidget(autoUpdateCheck);
    startupGroup->addWidget(autoRestartCheck);

    // The three agent-behaviour toggles used to trail the tab with no heading of
    // their own; in a column flow every card needs one to say what it is.
    auto *agentBehaviourLabel = new QLabel("AGENT BEHAVIOUR");
    agentBehaviourLabel->setObjectName("sectionLabel");
    auto *agentBehaviourGroup = generalTab->addSection();
    agentBehaviourGroup->addLayout(
        settingsHeading(agentBehaviourLabel, "sparkle"));
    agentBehaviourGroup->addWidget(autoSwitchToAgentCheck);
    agentBehaviourGroup->addWidget(excludeExternalClaudeCheck);
    agentBehaviourGroup->addWidget(publishAgentsToWebCheck);

    auto *nodeStatsGroup = generalTab->addSection();
    nodeStatsGroup->addLayout(settingsHeading(nodeStatsLabel, "graph"));
    nodeStatsGroup->addWidget(nodeStatsHint);
    for (QCheckBox *check : std::as_const(nodeStatChecks))
        nodeStatsGroup->addWidget(check);

    auto *bountyGroup = generalTab->addSection();
    bountyGroup->addLayout(settingsHeading(bountyLabel, "credit-card"));
    bountyGroup->addWidget(autoBountyCheck);
    bountyGroup->addWidget(bountyHint);
    bountyGroup->addLayout(bountyAmountRow);
    bountyGroup->addLayout(bountyModeRow);
    bountyGroup->addWidget(legacyWalletNotice);

    auto *vmGroup = generalTab->addSection();
    vmGroup->addLayout(settingsHeading(vmLabel, "server"));
    vmGroup->addWidget(vmHint);
    vmGroup->addWidget(vmCheck);
    vmGroup->addWidget(vmStatus);
    vmGroup->addLayout(vmButtonRow);

    addTab(generalTab, "General", "gear");

    // Repositories: creating/importing repos and where mirrors live.
    auto *reposTab = flowBody();

    auto *repositoriesGroup = reposTab->addSection();
    repositoriesGroup->addLayout(settingsHeading(repositoriesLabel, "repo"));
    repositoriesGroup->addWidget(repositoriesHint);
    repositoriesGroup->addLayout(repoButtonRow);

    auto *importGroup = reposTab->addSection();
    importGroup->addLayout(settingsHeading(importLabel, "download"));
    importGroup->addWidget(importHint);
    importGroup->addLayout(importRow);
    importGroup->addWidget(m_importStatus);
    importGroup->addLayout(importTokenForm);

    auto *storageGroup = reposTab->addSection();
    storageGroup->addLayout(settingsHeading(storageLabel, "file-directory"));
    storageGroup->addLayout(mirrorRow);

    auto *previewCacheGroup = reposTab->addSection();
    previewCacheGroup->addLayout(settingsHeading(previewCacheLabel, "eye"));
    previewCacheGroup->addLayout(previewCacheRow);

    auto *issuesSyncGroup = reposTab->addSection();
    issuesSyncGroup->addLayout(settingsHeading(issuesSyncLabel, "issue-opened"));
    issuesSyncGroup->addWidget(autoSyncIssuesCheck);

    auto *pullsSyncGroup = reposTab->addSection();
    pullsSyncGroup->addLayout(
        settingsHeading(pullsSyncLabel, "git-pull-request"));
    pullsSyncGroup->addWidget(autoSyncMergeCheck);

    auto *mirrorSyncGroup = reposTab->addSection();
    mirrorSyncGroup->addLayout(settingsHeading(mirrorSyncLabel, "sync"));
    mirrorSyncGroup->addWidget(mirrorSyncHint);
    mirrorSyncGroup->addLayout(mirrorSyncRow);

    // "Repos", the same word the left rail uses, so the strip's widest caption
    // isn't spending 60px saying what the icon already says.
    addTab(reposTab, "Repos", "repo");

    // Notifications: every desktop-alert opt-in. Two long checkbox lists, so
    // they sit side by side rather than one after the other.
    auto *notifyTab = flowBody(2);

    auto *pingsGroup = notifyTab->addSection();
    pingsGroup->addLayout(settingsHeading(notifyLabel, "bell"));
    pingsGroup->addWidget(inAppPingsCheck);
    pingsGroup->addWidget(inAppPingDuration, 0, Qt::AlignLeft);
    pingsGroup->addWidget(errorLogAlertCheck);
    pingsGroup->addWidget(systemAlertCheck);
    pingsGroup->addWidget(agentDoneAlertCheck);
    pingsGroup->addWidget(pushAlertCheck);
    pingsGroup->addWidget(actionAlertCombo, 0, Qt::AlignLeft);
    pingsGroup->addWidget(actionAlertStartedCheck, 0, Qt::AlignLeft);
    pingsGroup->addWidget(nodeConnectAlertCheck);
    pingsGroup->addWidget(disbursementAlertCheck);
    pingsGroup->addWidget(chatMessageAlertCheck);
    pingsGroup->addWidget(mentionAlertCheck);
    pingsGroup->addWidget(issueAlertCheck);
    pingsGroup->addWidget(pullAlertCheck);
    pingsGroup->addWidget(commentAlertCheck);
    pingsGroup->addWidget(mirrorUpdateAlertCheck);
    pingsGroup->addWidget(coveOpenAlertCheck);
    pingsGroup->addWidget(newUserAlertCheck);

    auto *emailGroup = notifyTab->addSection();
    emailGroup->addLayout(settingsHeading(emailNotifyLabel, "paper-airplane"));
    emailGroup->addWidget(emailMentionCheck);
    emailGroup->addWidget(emailSubscribedCheck);
    emailGroup->addWidget(emailPullCheck);
    emailGroup->addWidget(emailIssueAssignedCheck);
    emailGroup->addWidget(emailRepoSharedCheck);
    emailGroup->addWidget(emailPendingInboxCheck);
    emailGroup->addWidget(emailReleaseCheck);
    emailGroup->addWidget(emailCreditsCheck);
    emailGroup->addWidget(emailBountyFundedCheck);
    emailGroup->addWidget(emailBountyPaidCheck);
    emailGroup->addWidget(emailGeneralChatCheck);
    emailGroup->addWidget(emailHostOnlineCheck);
    emailGroup->addWidget(emailHostOfflineCheck);

    addTab(notifyTab, "Pings", "bell");

    // Agents & IDE: model keys/commands and editor integration. The agent form
    // is wide, so this one stays at two columns.
    auto *agentsTab = flowBody(2);

    auto *agentsGroup = agentsTab->addSection();
    agentsGroup->addLayout(settingsHeading(agentsLabel, "sparkle"));
    agentsGroup->addWidget(agentsHint);
    agentsGroup->addLayout(agentForm);
    agentsGroup->addWidget(autoStallAgentCheck);
    agentsGroup->addWidget(autoFixFailuresCheck);
    agentsGroup->addWidget(jailAgentsCheck);

    auto *usageGroup = agentsTab->addSection();
    usageGroup->addLayout(settingsHeading(usageLabel, "pie-chart"));
    usageGroup->addWidget(usageHint);
    usageGroup->addLayout(usageText);

    auto *ideGroup = agentsTab->addSection();
    ideGroup->addLayout(settingsHeading(ideLabel, "code"));
    ideGroup->addWidget(ideIntegrationCheck);
    ideGroup->addWidget(ideStatus);

    // "Agents / IDE", not "Agents & IDE": a tab bar eats the ampersand as a
    // mnemonic and painted the caption as "Agents_IDE".
    addTab(agentsTab, "Agents / IDE", "code");

    // Voice: local speech-to-text engine setup, mic device and test — its own
    // tab so it's easy to land on directly (see openVoiceSettings(), adhoc #132).
    auto *voiceTab = flowBody(2);

    auto *voiceGroup = voiceTab->addSection();
    voiceGroup->addLayout(settingsHeading(voiceLabel, "mic"));
    voiceGroup->addWidget(voiceHint);
    voiceGroup->addLayout(voiceRow);
    voiceGroup->addLayout(voiceDeviceRow);
    voiceGroup->addLayout(voiceTestRow);
    voiceGroup->addWidget(m_whisperStatusLabel);

    auto *worldVoiceGroup = voiceTab->addSection();
    worldVoiceGroup->addLayout(settingsHeading(worldVoiceLabel, "broadcast"));
    worldVoiceGroup->addWidget(worldVoiceHint);
    worldVoiceGroup->addLayout(worldVoiceOriginRow);
    worldVoiceGroup->addLayout(worldVoicePairRow);
    worldVoiceGroup->addWidget(m_worldSpeechStatusLabel);

    m_voiceSettingsTabIndex = tabs->count();
    addTab(voiceTab, "Voice", "mic");

    // Secrets: shared action variables. Coves are account-scoped and managed
    // from their repository explorer, not through a global password here.
    auto *secretsTab = new QWidget;
    auto *secretsCol = new QVBoxLayout(secretsTab);
    secretsCol->setContentsMargins(2, 12, 2, 12);
    secretsCol->setSpacing(10);
    secretsCol->addLayout(varsHeading);
    secretsCol->addLayout(varButtonRow);
    secretsCol->addWidget(m_varsList);
    secretsCol->addStretch();
    addTab(secretsTab, "Secrets", "key");

    // MCP: connector token + the config to paste into an external agent, so
    // anything speaking MCP can work this node's issues and PRs (adhoc #16).
    // Built in its own translation unit (MainWindowMcp.cpp).
    addTab(buildMcpConnectorTab(), "MCP", "link");

    // Security: private vulnerability reporting form.
    addTab(buildVulnReportTab(), "Security", "shield-check");

    // Data: where configuration data lives, per-directory breakdown, backup and
    // cleanup. Built in its own translation unit (MainWindowData.cpp).
    addTab(buildDataSection(), "Data", "package");

    m_settingsTabs = tabs;
    // Each settings tab is its own destination on the Back/Forward trail
    // (adhoc #50), so leaving one and coming back lands where you were.
    connect(tabs, &QTabWidget::currentChanged, this,
            [this](int) { scheduleNavRecord(); });
    layout->addWidget(tabs, 1);
    layout->addWidget(m_rebuildStatus);
    layout->addLayout(footerRow);
    reloadVariablesList();
    setSettingsAvatar(QByteArray()); // show the current/generated avatar
    return page;
}

// Fill the Settings → Agents "Composer models" list, which chooses which rows
// the composer's prompt dropdown offers (adhoc #1557) — one checkable row per
// catalog entry. Rebuilt rather than patched so a provider's line-up changing
// under us (a live /v1/models fetch landing, a Codex catalog refresh) shows up
// here too; the tick state comes from the saved hidden set, so nothing is lost
// in the rebuild.
void MainWindow::refreshComposerModelVisibilityList()
{
    if (!m_composerModelVisibilityList)
        return;
    const QSet<QString> hidden = hiddenComposerModels();
    // itemChanged fires per row while filling; the saved set is the input here,
    // so let it not write itself back out.
    const QSignalBlocker block(m_composerModelVisibilityList);
    m_composerModelVisibilityList->clear();
    QSet<QString> seen;
    for (const ComposerModelChoice &choice : composerModelCatalog()) {
        const QString key = composerModelKey(choice.provider, choice.model);
        // Two providers offering the same id would otherwise get two checkboxes
        // for one setting, which could only ever disagree with each other.
        if (seen.contains(key))
            continue;
        seen.insert(key);
        // The dropdown drops the "which CLI runs it" half to stay narrow; this
        // list has the room, and "Sonnet 5" alone doesn't say whose it is.
        auto *item = new QListWidgetItem(
            agentControlIcon(choice.iconIndex),
            choice.agentName.isEmpty() || choice.agentName == choice.label
                ? choice.label
                : QStringLiteral("%1 · %2").arg(choice.label, choice.agentName));
        item->setData(Qt::UserRole, key);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(hidden.contains(key) ? Qt::Unchecked : Qt::Checked);
        item->setToolTip(choice.model.isEmpty()
                             ? choice.tooltip
                             : QStringLiteral("%1 · %2").arg(choice.model,
                                                             choice.agentName));
        m_composerModelVisibilityList->addItem(item);
    }
}

// ------------------------------------------------------------- quick setup

namespace {

// Resolve a stored action variable by exact (case-insensitive) name, with the
// same hygiene as the Cloudflare credential helpers: blank or multi-line
// values are ignored so a pasted credential file can never smuggle extra
// lines into a child environment.
QString quickSetupStoredVariable(const QMap<QString, QString> &vars,
                                 const QString &name)
{
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const QString value = it.value().trimmed();
        if (it.key().compare(name, Qt::CaseInsensitive) == 0 &&
            !value.isEmpty() && !value.contains(QLatin1Char('\n')))
            return value;
    }
    return QString();
}

// The world themes the /world frontend ships (THEME_OPTIONS in world-data.js).
const struct { const char *id; const char *label; } kQuickSetupWorldThemes[] = {
    {"world", "Full daylight"},
    {"rain", "Daylight + rain"},
    {"snow", "Daylight + snow"},
    {"winter", "Daylight + winter"},
    {"cyberpunk", "Local cyberpunk"},
    {"low-light", "Local low light"},
};

} // namespace

// Quick Setup: one page that takes a brand-new instance from zero to
// configured. It gathers the identity fields, the provisioning credentials
// action workflows need (Cloudflare for deploys and tunnels, Vultr for
// creating mirror nodes) and the world's look and naming, then applies them
// all with one button. Credentials land in the shared Variables / Secrets
// store — the one injected into every action run's environment and redacted
// from logs — so the deploy and mirror-provisioning workflows pick them up
// unchanged. Blank or unchanged fields are skipped on apply, so re-applying
// this page never wipes a value that is already set.
QWidget *MainWindow::buildQuickSetupTab()
{
    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    col->setContentsMargins(2, 14, 2, 14);
    col->setSpacing(10);

    auto *headLabel = new QLabel("QUICK SETUP");
    headLabel->setObjectName("sectionLabel");
    auto *hint = new QLabel(
        "Set up this ForkMesh instance from scratch in one pass: who you are, "
        "the credentials workflows use to provision infrastructure, and how "
        "your world looks and is named. Blank fields keep their current "
        "value, so you can apply this page as often as you like.");
    hint->setObjectName("statusLine");
    hint->setWordWrap(true);

    // Prefill every field from the store it ultimately writes to, so the page
    // doubles as a review of what is already configured.
    const QMap<QString, QString> storedVars = ActionStore::variables();

    auto *identityLabel = new QLabel("IDENTITY");
    identityLabel->setObjectName("sectionLabel");

    auto *userEdit = new QLineEdit;
    userEdit->setMaxLength(32);
    userEdit->setPlaceholderText("Username");
    userEdit->setToolTip(
        "Your ForkMesh user account. One user account can own many nodes; "
        "changing this changes who you are, not this machine's node name.");
    userEdit->setText(settingsAccountName());

    auto *nodeEdit = new QLineEdit;
    nodeEdit->setMaxLength(63);
    nodeEdit->setPlaceholderText("Node name (this machine)");
    nodeEdit->setToolTip(
        "This machine's node name on the mesh \xE2\x80\x94 how it appears in "
        "rosters and node lists. Not your username.");
    nodeEdit->setText(machineNodeName());

    auto *identityForm = new QFormLayout;
    identityForm->setLabelAlignment(Qt::AlignLeft);
    identityForm->setSpacing(8);
    identityForm->addRow("Username", userEdit);
    identityForm->addRow("Node name", nodeEdit);

    auto *credsLabel = new QLabel("PROVISIONING CREDENTIALS");
    credsLabel->setObjectName("sectionLabel");
    auto *credsHint = new QLabel(
        "Stored locally in Variables / Secrets, injected into every action "
        "run's environment and redacted from logs. The Cloudflare token "
        "authenticates the deploy workflow and tunnel bootstrap; the Vultr "
        "token lets provisioning workflows create mirror nodes and is also "
        "written to cloudflare_worker/.env.production, so neither this page "
        "nor the Hosts page asks for it twice.");
    credsHint->setObjectName("statusLine");
    credsHint->setWordWrap(true);

    auto *cfTokenEdit = new QLineEdit;
    cfTokenEdit->setEchoMode(QLineEdit::Password);
    cfTokenEdit->setPlaceholderText("Cloudflare API token");
    cfTokenEdit->setToolTip(
        "Saved as the CLOUDFLARE_API_TOKEN variable for the deploy workflow "
        "and the one-click tunnel bootstrap.");
    cfTokenEdit->setText(
        forkmesh::control::cloudflareApiTokenFromVariables(storedVars));

    auto *cfAccountEdit = new QLineEdit;
    cfAccountEdit->setPlaceholderText("Cloudflare account ID (optional)");
    cfAccountEdit->setToolTip(
        "Saved as the CLOUDFLARE_ACCOUNT_ID variable. Needed when the API "
        "token can reach more than one Cloudflare account.");
    cfAccountEdit->setText(
        forkmesh::control::cloudflareAccountIdFromVariables(storedVars));

    auto *vultrTokenEdit = new QLineEdit;
    vultrTokenEdit->setEchoMode(QLineEdit::Password);
    vultrTokenEdit->setPlaceholderText("Vultr API token");
    vultrTokenEdit->setToolTip(
        "Saved as the VULTR_API_KEY variable and in "
        "cloudflare_worker/.env.production, so provisioning workflows and the "
        "Hosts page's one-click mirror both find it without asking again.");
    vultrTokenEdit->setText(
        forkmesh::control::vultrApiKeyFromVariables(storedVars));

    auto *credsForm = new QFormLayout;
    credsForm->setLabelAlignment(Qt::AlignLeft);
    credsForm->setSpacing(8);
    credsForm->addRow("Cloudflare API token", cfTokenEdit);
    credsForm->addRow("Cloudflare account ID", cfAccountEdit);
    credsForm->addRow("Vultr API token", vultrTokenEdit);

    auto *worldLabel = new QLabel("WORLD & APPEARANCE");
    worldLabel->setObjectName("sectionLabel");
    auto *worldHint = new QLabel(
        "How the desktop app and your public world look. World values are "
        "saved as the WORLD_THEME, WORLD_ACCENT_COLOR and WORLD_NAME "
        "variables so deploy workflows can stamp them into the published "
        "world.");
    worldHint->setObjectName("statusLine");
    worldHint->setWordWrap(true);

    auto *appThemeCombo = new QComboBox;
    appThemeCombo->addItem("Follow system", "system");
    appThemeCombo->addItem("Dark", "dark");
    appThemeCombo->addItem("Light", "light");
    appThemeCombo->setToolTip(
        "The desktop app's color theme, or follow the OS setting.");
    {
        const int idx = appThemeCombo->findData(
            QSettings().value(kThemeSetting, "system").toString());
        appThemeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    auto *worldThemeCombo = new QComboBox;
    for (const auto &theme : kQuickSetupWorldThemes)
        worldThemeCombo->addItem(QLatin1String(theme.label),
                                 QLatin1String(theme.id));
    worldThemeCombo->setToolTip(
        "The default weather / lighting theme for your public world page.");
    {
        const int idx = worldThemeCombo->findData(quickSetupStoredVariable(
            storedVars, QStringLiteral("WORLD_THEME")));
        worldThemeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    auto *accentEdit = new QLineEdit;
    accentEdit->setMaxLength(32);
    accentEdit->setPlaceholderText("#58a6ff");
    accentEdit->setToolTip(
        "Accent color for your public world, as a hex value like #ff7847.");
    accentEdit->setText(quickSetupStoredVariable(
        storedVars, QStringLiteral("WORLD_ACCENT_COLOR")));
    auto *accentPickButton = new QPushButton("Pick\xE2\x80\xA6");
    accentPickButton->setObjectName("ghostButton");
    accentPickButton->setCursor(Qt::PointingHandCursor);
    connect(accentPickButton, &QPushButton::clicked, this, [this, accentEdit] {
        const QColor initial(accentEdit->text().trimmed());
        const QColor picked = QColorDialog::getColor(
            initial.isValid() ? initial : QColor(QStringLiteral("#58a6ff")),
            this, QStringLiteral("World accent color"));
        if (picked.isValid())
            accentEdit->setText(picked.name());
    });
    auto *accentRow = new QHBoxLayout;
    accentRow->setContentsMargins(0, 0, 0, 0);
    accentRow->setSpacing(8);
    accentRow->addWidget(accentEdit, 1);
    accentRow->addWidget(accentPickButton);

    auto *worldNameEdit = new QLineEdit;
    worldNameEdit->setMaxLength(80);
    worldNameEdit->setPlaceholderText("World name, e.g. ForkMesh City");
    worldNameEdit->setToolTip("The display name of your public world.");
    worldNameEdit->setText(
        quickSetupStoredVariable(storedVars, QStringLiteral("WORLD_NAME")));

    auto *worldForm = new QFormLayout;
    worldForm->setLabelAlignment(Qt::AlignLeft);
    worldForm->setSpacing(8);
    worldForm->addRow("App theme", appThemeCombo);
    worldForm->addRow("World theme", worldThemeCombo);
    worldForm->addRow("World accent color", accentRow);
    worldForm->addRow("World name", worldNameEdit);

    auto *applyButton = new QPushButton("Apply quick setup");
    applyButton->setObjectName("primaryButton");
    applyButton->setCursor(Qt::PointingHandCursor);
    applyButton->setToolTip(
        "Save every filled-in field to its store in one pass.");
    setOcticon(applyButton, "rocket", 16);

    auto *statusLabel = new QLabel;
    statusLabel->setObjectName("modeHint");
    statusLabel->setWordWrap(true);
    statusLabel->hide();

    connect(applyButton, &QPushButton::clicked, this,
            [this, userEdit, nodeEdit, cfTokenEdit, cfAccountEdit,
             vultrTokenEdit, appThemeCombo, worldThemeCombo, accentEdit,
             worldNameEdit, statusLabel] {
        auto setStatus = [statusLabel](const QString &msg, bool bad) {
            statusLabel->setText(msg);
            statusLabel->setProperty("bad", bad);
            statusLabel->style()->unpolish(statusLabel);
            statusLabel->style()->polish(statusLabel);
            statusLabel->show();
        };

        // Validate up front so a bad field never half-applies the page.
        const QString accent = accentEdit->text().trimmed();
        if (!accent.isEmpty() && !QColor(accent).isValid()) {
            setStatus("World accent color must be a color like #ff7847.", true);
            return;
        }

        QStringList applied;

        const QString wantedUser = userEdit->text().trimmed();
        if (!wantedUser.isEmpty() &&
            wantedUser.compare(settingsAccountName(),
                               Qt::CaseInsensitive) != 0) {
            onProfileNameChanged(wantedUser);
            applied << QStringLiteral("username");
        }
        userEdit->setText(settingsAccountName());

        const QString wantedNode = nodeEdit->text().trimmed();
        if (!wantedNode.isEmpty() && wantedNode != machineNodeName()) {
            saveMachineNodeName(wantedNode);
            if (m_settingsMachineNodeEdit)
                m_settingsMachineNodeEdit->setText(machineNodeName());
            applied << QStringLiteral("node name");
        }
        // Reflect the sanitized (or defaulted) value back into the field.
        nodeEdit->setText(machineNodeName());

        const QString wantedTheme = appThemeCombo->currentData().toString();
        if (wantedTheme !=
            QSettings().value(kThemeSetting, "system").toString()) {
            QSettings().setValue(kThemeSetting, wantedTheme);
            applyTheme();
            if (m_themeCombo) {
                const QSignalBlocker blocker(m_themeCombo);
                const int idx = m_themeCombo->findData(wantedTheme);
                if (idx >= 0)
                    m_themeCombo->setCurrentIndex(idx);
            }
            applied << QStringLiteral("app theme");
        }

        // Credentials and world variables all land in one store update.
        QMap<QString, QString> vars = ActionStore::variables();
        bool varsChanged = false;
        auto putVariable = [&vars, &varsChanged, &applied](
                               const QString &name, const QString &value,
                               const QString &current, const QString &label) {
            if (value.isEmpty() || value == current)
                return;
            vars.insert(name, value);
            varsChanged = true;
            applied << label;
        };
        putVariable(QStringLiteral("CLOUDFLARE_API_TOKEN"),
                    cfTokenEdit->text().trimmed(),
                    forkmesh::control::cloudflareApiTokenFromVariables(vars),
                    QStringLiteral("Cloudflare API token"));
        putVariable(QStringLiteral("CLOUDFLARE_ACCOUNT_ID"),
                    cfAccountEdit->text().trimmed(),
                    forkmesh::control::cloudflareAccountIdFromVariables(vars),
                    QStringLiteral("Cloudflare account ID"));
        putVariable(QStringLiteral("WORLD_THEME"),
                    worldThemeCombo->currentData().toString(),
                    quickSetupStoredVariable(
                        vars, QStringLiteral("WORLD_THEME")),
                    QStringLiteral("world theme"));
        putVariable(QStringLiteral("WORLD_ACCENT_COLOR"),
                    accent.isEmpty() ? QString() : QColor(accent).name(),
                    quickSetupStoredVariable(
                        vars, QStringLiteral("WORLD_ACCENT_COLOR")),
                    QStringLiteral("world accent color"));
        putVariable(QStringLiteral("WORLD_NAME"),
                    worldNameEdit->text().trimmed(),
                    quickSetupStoredVariable(
                        vars, QStringLiteral("WORLD_NAME")),
                    QStringLiteral("world name"));
        if (varsChanged) {
            ActionStore::setVariables(vars);
            reloadVariablesList();
        }

        // The Vultr key goes through the shared helper so this page and the
        // Hosts page's one-click mirror agree on where it lives: the canonical
        // VULTR_API_KEY variable plus cloudflare_worker/.env.production. It
        // runs after the store update above so it reads that fresh map back
        // instead of overwriting it (adhoc #127).
        QString vultrError;
        if (!rememberVultrApiKey(vultrTokenEdit->text().trimmed(), &vultrError)
                 .isEmpty()) {
            applied << QStringLiteral("Vultr API token");
        }

        if (applied.isEmpty()) {
            setStatus("Nothing to apply \xE2\x80\x94 every field already "
                      "matches the stored setup.",
                      false);
            return;
        }
        setStatus(QStringLiteral("Saved: %1.%2")
                      .arg(applied.join(", "),
                           vultrError.isEmpty()
                               ? QString()
                               : QStringLiteral(" ") + vultrError),
                  false);
        logSystem(QStringLiteral("Quick setup applied: %1.")
                      .arg(applied.join(", ")));
    });

    auto *applyRow = new QHBoxLayout;
    applyRow->setContentsMargins(0, 0, 0, 0);
    applyRow->addWidget(applyButton);
    applyRow->addStretch();

    col->addWidget(headLabel);
    col->addWidget(hint);
    col->addSpacing(4);
    col->addWidget(identityLabel);
    col->addLayout(identityForm);
    col->addSpacing(6);
    col->addWidget(credsLabel);
    col->addWidget(credsHint);
    col->addLayout(credsForm);
    col->addSpacing(6);
    col->addWidget(worldLabel);
    col->addWidget(worldHint);
    col->addLayout(worldForm);
    col->addSpacing(6);
    col->addLayout(applyRow);
    col->addWidget(statusLabel);
    col->addStretch();
    return body;
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

void MainWindow::showClaudeCodeDeviceSetup()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Claude Code device login");
    auto *l = new QVBoxLayout(&dialog);

    const bool claudeCodeSignedIn =
        !claudeCodeOAuthToken().isEmpty();
    auto *intro = new QLabel(
        QStringLiteral(
            "<b>This device: %1</b><br><br>"
            "ForkMesh does not export, import, copy, upload, or serialize Claude "
            "Code OAuth credentials. To use Claude Code here, open a terminal on "
            "this device, run <code>claude</code>, and complete Claude's own "
            "provider login. Repeat that provider-owned login separately on each "
            "device you control; never send a credential file or token through a "
            "ForkMesh workspace, chat, host deployment, clipboard, or account "
            "bundle.<br><br>"
            "<b>Shared agent boundary:</b> optional web/team synchronization "
            "contains only owner-sealed task, session-state, transcript, and "
            "artifact metadata. The relay, organization administrators, teammates, "
            "and agent worktree files do not receive provider credentials. A "
            "locally launched provider client may use device-local authentication "
            "inside its process boundary, but Claude and Codex logins remain isolated "
            "to the owner device that runs the agent.")
            .arg(claudeCodeSignedIn
                     ? QStringLiteral("Claude Code login detected")
                     : QStringLiteral("Claude Code is not signed in")));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                   Qt::LinksAccessibleByMouse);
    l->addWidget(intro);

    auto *closeBtn = new QPushButton("Close");
    closeBtn->setObjectName("primaryButton");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(closeBtn);
    l->addLayout(row);

    dialog.exec();
}

QStringList MainWindow::headlessClaudeAuth(const QStringList &args)
{
    const QString sub = args.value(0).toLower();

    if (sub.isEmpty() || sub == QLatin1String("status")) {
        return {
            claudeCodeOAuthToken().isEmpty()
                ? QStringLiteral("Claude Code login: not detected on this device")
                : QStringLiteral("Claude Code login: detected on this device"),
            QStringLiteral(
                "Provider credentials are owner-device-only and are never "
                "exported, copied, serialized, uploaded, or placed in an agent "
                "workspace."),
            QStringLiteral(
                "To sign in, run `claude` on this device and complete Claude's "
                "provider-owned login flow."),
        };
    }

    if (sub == QLatin1String("export") ||
        sub == QLatin1String("import")) {
        return {
            QStringLiteral(
                "Refused: ForkMesh does not export or import Claude OAuth "
                "tokens, API keys, credential files, or account bundles."),
            QStringLiteral(
                "Run `claude` and complete the provider-owned login separately "
                "on this device."),
        };
    }

    return {QStringLiteral("usage: claude-auth status"),
            QStringLiteral(
                "Claude login is device-local; credential transfer is unsupported.")};
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
    return forkMeshNodeAvatarPng(seed);
}

QByteArray MainWindow::effectiveUserAvatar()
{
    if (!m_userAvatar.isEmpty())
        return m_userAvatar;
    QString seed = topBarUserName();
    if (seed.isEmpty())
        seed = m_profileIdentity.publicKey();
    if (seed.isEmpty())
        seed = QStringLiteral("forkmesh-user");
    return forkMeshAvatarPng(seed);
}

void MainWindow::pushAccountAvatar()
{
    // Only persist an avatar the user actually chose — never the generated
    // identicon effectiveUserAvatar() falls back to. Needs a login session
    // token to authenticate the write.
    if (m_accountSessionToken.isEmpty() || m_userAvatar.isEmpty())
        return;
    postAccountSync(
        QStringLiteral("profile"),
        QJsonObject{
            {QStringLiteral("sessionToken"), m_accountSessionToken},
            {QStringLiteral("avatarPng"),
             QString::fromLatin1(m_userAvatar.toBase64())},
        },
        nullptr);
}

void MainWindow::adoptWebAccountAvatar(const QByteArray &png)
{
    // Take the picture the account is showing on the website. The public user
    // directory refreshChatUserDirectory() polls already carries every
    // account's avatarPng, so following a change made on the web costs no extra
    // request. Guarded on being signed in: a local profile that merely shares
    // the name must never have its picture replaced from the directory.
    if (!m_accountAuthenticated || png.isEmpty() || png == m_userAvatar)
        return;
    m_userAvatar = png;
    QSettings().setValue(kAvatarSetting, png);
    updateAvatarButton();
    updateUserAvatarButton();
    updateChatIdentity();
}

void MainWindow::updateAvatarButton()
{
    // The top-bar node avatar button is gone (only the user avatar remains);
    // this still refreshes the issue composer's avatar/name whenever the
    // node or user avatar changes.
    refreshIssueComposerAvatar();
}

void MainWindow::updateUserAvatarButton()
{
    if (!m_userAvatarNavButton)
        return;
    // Circular, like the website renders an account's picture (adhoc #19).
    // 24px, so the rail's Account item reads at the same visual weight as its
    // 20px octicon siblings (adhoc #117).
    const QPixmap pm = roundedAvatar(effectiveUserAvatar(), 24, 0.5);
    if (!pm.isNull())
        m_userAvatarNavButton->setIcon(QIcon(pm));
    m_userAvatarNavButton->setIconSize(QSize(24, 24));
    m_userAvatarNavButton->setText(QString());
}

void MainWindow::refreshIssueComposerAvatar()
{
    if (m_issueComposerAvatar) {
        // Show the user's avatar next to the comment composer so it matches the
        // "Commenting as <user>" label (and the identity a comment is filed under)
        // rather than the node's badge.
        const QPixmap pm = roundedAvatar(effectiveUserAvatar(), 36);
        if (pm.isNull()) {
            m_issueComposerAvatar->setPixmap(QPixmap());
            m_issueComposerAvatar->setText("FM");
        } else {
            m_issueComposerAvatar->setText(QString());
            m_issueComposerAvatar->setPixmap(pm);
        }
    }

    // The "Commenting as" label is built once with the composer, so it can go
    // stale once the account name resolves after login. Keep it in sync
    // whenever the avatar (and thus the identity) refreshes.
    if (m_issueComposerTitle) {
        m_issueComposerTitle->setText(
            QStringLiteral("Commenting as <b>%1</b>")
                .arg(topBarUserName().toHtmlEscaped()));
    }
}

QWidget *MainWindow::makeComposerIdentity(QLabel **outAvatar, const QString &verb)
{
    // Freshly built each time a composer is shown (composer widgets are rebuilt
    // on navigation), so it always reflects the current avatar and username.
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *avatar = new QLabel("FM");
    avatar->setObjectName("issueAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(28, 28);
    avatar->setScaledContents(true);
    // The composer posts under the user identity (topBarUserName), so show the
    // matching user avatar here instead of the node badge.
    const QPixmap pm = roundedAvatar(effectiveUserAvatar(), 28);
    if (!pm.isNull()) {
        avatar->setText(QString());
        avatar->setPixmap(pm);
    }

    auto *name = new QLabel;
    name->setObjectName("issueCommentTitle");
    name->setTextFormat(Qt::RichText);
    const QString user = topBarUserName().toHtmlEscaped();
    if (verb.trimmed().isEmpty())
        name->setText(QStringLiteral("<b>%1</b>").arg(user));
    else
        name->setText(QStringLiteral("%1 as <b>%2</b>")
                          .arg(verb.trimmed().toHtmlEscaped(), user));

    layout->addWidget(avatar, 0, Qt::AlignVCenter);
    layout->addWidget(name, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    if (outAvatar)
        *outAvatar = avatar;
    return row;
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
    const qreal dpr = iconDevicePixelRatio();
    QPixmap rounded = crispIconPixmap(side, dpr);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, side, side, 14, 14);
    painter.setClipPath(clip);
    QPixmap scaled = pixmap.scaled(rounded.width(), rounded.height(),
                                   Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    painter.drawPixmap(0, 0, scaled);
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
    // Restart immediately even with agents in progress: agent sessions are
    // resumable (the `claude` process is relaunched and re-attached on startup),
    // so a mid-run restart no longer loses work and there's no reason to make the
    // user wait for the fleet to go idle (adhoc #176). This supersedes the old
    // queue-behind-agents gate (adhoc #75/#91/#104/#111/#116/#134/#143).
    m_rebuildRestartQueued = false;
    startRestartCautionFlash();
    if (m_rebuildQueuePollTimer)
        m_rebuildQueuePollTimer->stop();
    setRestartSpinHourglass(false);
    // Incremental rebuild + relaunch (no cache wipe) for fast iteration. Reuses
    // the Settings rebuild button/status as the progress target.
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("quick rebuild & restart started"));
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    const QString clientDir = workingClientDir();
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

void MainWindow::maybeStartQueuedRebuild()
{
    // A manual rebuild & restart is waiting for agents to finish. Once the last
    // one goes idle, run it — but not while a rebuild is already underway (the
    // rebuild button is disabled for its duration).
    if (!m_rebuildRestartQueued) {
        if (m_rebuildQueuePollTimer)
            m_rebuildQueuePollTimer->stop();
        return;
    }
    if (anyAgentRunning())
        return;
    if (m_rebuildButton && !m_rebuildButton->isEnabled())
        return;
    logRestart(QStringLiteral("running actions finished; starting queued rebuild"));
    quickRebuildRestart();
}

void MainWindow::rebuildAndRelaunch()
{
    startRestartCautionFlash();
    beginRestartLog();
    showUpdateLog();
    logRestart(QStringLiteral("clean rebuild & restart started"));
    if (m_settingsNameEdit)
        saveProfileName(m_settingsNameEdit->text());
    if (m_settingsMachineNodeEdit)
        saveMachineNodeName(m_settingsMachineNodeEdit->text());
    if (m_settingsNodeLabelsEdit)
        saveActionNodeLabels(m_settingsNodeLabelsEdit->text());
    m_buildButton = m_rebuildButton;
    m_buildStatusLabel = m_rebuildStatus;
    m_rebuildButton->setEnabled(false);

    const QString clientDir = workingClientDir();
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
    // On by default for headless nodes (no operator is around to click "Update,
    // rebuild & restart" on a VM), off by default on desktop. main.cpp seeds the
    // value on first headless launch, but we also default to m_headless here so a
    // node still auto-updates if that seed never persisted (e.g. an unwritable
    // config dir). An explicit operator opt-out writes false and is respected.
    if (!QSettings().value(kAutoUpdateSetting, m_headless).toBool())
        return;
    if (!QSettings().value(kAutoUpdateRestartSetting, true).toBool())
        return;
    if (m_autoUpdateChecking)
        return; // a check from an earlier tick is still in flight
    if (m_rebuildButton && !m_rebuildButton->isEnabled())
        return; // a rebuild (manual or auto) is already running
    if (anyAgentRunning())
        return; // never yank an in-progress agent session out from under itself

    const QString clientDir = runningClientDir();
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
                // Prefer the release's prebuilt artifact: rebuilding from
                // source next to the live node is what killed the fleet on
                // v0.6.2 (OOM/disk during the build, and no supervisor to
                // restart a node that dies mid-update).
                if (tryPrebuiltAutoUpdate(clientDir, latestTag,
                                          tagCommit.trimmed()))
                    return;
                updateRebuildRestart();
            });
    fetch->start(QStringLiteral("git"), {"fetch", "--quiet", "--tags"});
}

void MainWindow::attachBackend(ChatBackend *backend)
{
    if (m_backend)
        leaveSession();
    m_backend = backend;
    m_lastChatDisplayName.clear();
    m_lastChatAvatar.clear();
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
    // Through a lambda rather than straight at &MainWindow::logSystem: the
    // origin arguments are defaults, which a pointer-to-member connection
    // cannot supply. This relays the backend's messages, so the origin they
    // record is this line — which is the truth about how they reached the log.
    connect(backend, &ChatBackend::systemMessage, this,
            [this](const QString &text) { logSystem(text); });
    connect(backend, &ChatBackend::channelsChanged, this, &MainWindow::setChannels);
    connect(backend, &ChatBackend::privateChannelJoined, this,
            [this](const QString &channel) {
                m_privateChannels.insert(channel);
                persistPrivateChannels();
                refreshChannelList();
            });
    connect(backend, &ChatBackend::rosterChanged, this, &MainWindow::setRoster);
    connect(backend, &ChatBackend::latencySampled, this,
            &MainWindow::onRelayLatencySampled);
    connect(backend, &ChatBackend::mirrorUpdated, this, &MainWindow::onPeerMirrorUpdated);
    connect(backend, &ChatBackend::mirrorSynced, this, &MainWindow::onPeerMirrorSynced);
    connect(backend, &ChatBackend::mirrorRefreshRequested, this,
            &MainWindow::onMirrorRefreshRequested);
    connect(backend, &ChatBackend::coveOpened, this, &MainWindow::onCoveOpened);
    connect(backend, &ChatBackend::coveInvited, this, &MainWindow::onCoveInvited);
    connect(backend, &ChatBackend::networkDiagnosticsChanged, this, [this] {
        if (m_sectionStack &&
            m_sectionStack->currentIndex() == kNetworkDiagnosticsSectionIndex)
            refreshNetworkDiagnostics();
    });
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
    refreshChatUserDirectory();
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
    if (m_solanaEdit && !solana.trimmed().isEmpty()) {
        if (forkmesh::control::isValidSolanaPublicAddress(solana)) {
            m_solanaEdit->setText(solana.trimmed());
        } else {
            // Do not echo the rejected value: it may be private wallet material
            // accidentally supplied where only a public address is allowed.
            m_solanaEdit->clear();
            logSystem(QStringLiteral(
                "Ignored an invalid headless payout address; only a public "
                "Solana address is accepted."));
        }
    }
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
    lines << QStringLiteral("Repository signals: bounded HTTPS sync");

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
    // Leaving the mesh used to dump the user back on the setup screen; that
    // screen is gone (adhoc #115), so stay in the app — the status line already
    // reports the disconnect and the top-bar pill reappears if the account went
    // with it.
    updateSignInButton();
    m_userName.clear();

    m_homeRoster.clear();
    m_peerLastSeenMs.clear();
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

// Each entry: substring to look for (lower-case) -> {accent, badge}. First
// match wins, so order from most specific to most general. Kept at namespace
// scope (not local to networkLogStyleFor) so accentForBadge() below can also
// look a badge's colour up by name for the quick-filter chips.
struct Rule {
    const char *needle;
    const char *accent;
    const char *badge;
};

// Red is reserved for ERROR so the log reads "red == something failed" at a
// glance; every other category (including HOST, previously pink) gets a
// distinct non-red accent.
const Rule kNetworkLogRules[] = {
        // The deployed Worker's own live tail, merged into this log rather than
        // shown in a window of its own (adhoc #1613). Matched first: a hit line
        // carries a whole URL, and almost any rule below could claim one of
        // those ("/api/sync", "/api/issues", …). networkLogStyleFor() short-
        // circuits on the same prefix ahead of the error scan; the entry here is
        // what gives the CLOUD chip its colour.
        {"cloud hit: ", "#f6821f", "CLOUD"},
        // App start/stop/rebuild-restart markers — keep above "fork" so
        // "ForkMesh" in the start line doesn't get tagged FORK.
        {"session started", "#f2cc60", "SESSION"},
        {"session ended", "#f2cc60", "SESSION"},
        {"quick update started", "#f2cc60", "SESSION"},
        {"rebuild & restart started", "#f2cc60", "SESSION"},
        {"restarting now", "#f2cc60", "SESSION"},
        // Firewall messages mention "ForkMesh's ports" — keep above "fork" so
        // they don't get swept into the FORK badge by that substring.
        {"firewall", "#f2cc60", "NETWORK"},
        // The footer strip's ✓ / ✕ outcome lines name the kind of work they
        // summarise ("git", "net", "sync" …), so they must be classified before
        // the per-kind rules below claim them — and their own chip makes them
        // filterable as a group (adhoc #419).
        //
        // The ✕ half gets its own badge (and an orange accent — red stays
        // reserved for outright failures): work that ran on the GUI thread is
        // what freezes the window, so "how much of this session was *not*
        // backgrounded" has to be countable on its own rather than buried in the
        // same tally as the healthy ✓ runs.
        //
        // Both halves are matched on the "… backgrounded" phrase the outcome
        // line always carries, not on the word "background" — otherwise every
        // line that merely *mentions* background work (a startup trace
        // scheduling some, a daemon note) lands in the same tally as a retired
        // ticket and inflates the count the split exists to make trustworthy.
        // Those mentions get their own dim badge below (adhoc #1594): still
        // filterable as background chatter, but never counted as a task run.
        {" not backgrounded", "#f0883e", "BGBLOCK"},
        {" backgrounded", "#8b949e", "BGTASK"},
        {"background ", "#6e7681", "BGNOTE"},
        {"pull request", "#3fb950", "PULL"},
        {"pull #", "#3fb950", "PULL"},
        {"merged", "#a371f7", "MERGE"},
        {"bounty", "#d29922", "BOUNTY"},
        {"escrow", "#d29922", "BOUNTY"},
        {"solana", "#d29922", "WALLET"},
        {"funded", "#d29922", "BOUNTY"},
        {"mirror", "#39c5cf", "MIRROR"},
        {"sync", "#39c5cf", "SYNC"},
        // Account heartbeat pings hit "https://forkmesh.com/..." like every
        // other request, so without this they'd fall into the generic FORK
        // bucket below purely from the domain name.
        {"account", "#8b949e", "ACCOUNT"},
        // Split the fork lifecycle the same way pull requests split into
        // PULL (opened) vs MERGE (closed) — "forked into" (done) above the
        // generic "fork" (in progress / location) so they read distinctly.
        {"forked into", "#3fb950", "FORKED"},
        // These all mention the "forkmesh/forkmesh" repo name, so without a
        // dedicated rule above the generic "fork" match below they'd all be
        // swept into an uninformative green FORK badge. Give each its own
        // label so the log reads as what actually happened.
        {"actions: ", "#f0883e", "ACTIONS"},
        {"integrity pin", "#79c0ff", "PIN"},
        {"host: ", "#76e3ea", "HOST"},
        {"publish", "#58a6ff", "PUBLISH"},
        {"push", "#58a6ff", "GIT"},
        {"git:", "#58a6ff", "GIT"},
        {"commit", "#58a6ff", "GIT"},
        {"patch", "#58a6ff", "GIT"},
        {"fork", "#3fb950", "FORK"},
        // Ad-hoc agent starts ("Started a X agent on your prompt...") mention no
        // issue at all, so keep this above the generic "issue" match below —
        // otherwise a prompt-only run would misleadingly badge as ISSUE.
        {"on your prompt", "#bc8cff", "PROMPT"},
        {"issue", "#bc8cff", "ISSUE"},
        {"admin", "#db6d28", "ADMIN"},
        {"identity", "#79c0ff", "IDENTITY"},
        {"encryption", "#79c0ff", "CRYPTO"},
        // Split the generic NODE bucket the same way: a peer joining, raw
        // mainnode traffic and a connection-status change are different
        // events and shouldn't all read as the same green "NODE" badge.
        {"node connected", "#3fb950", "PEER"},
        {"network: ", "#f2cc60", "NETWORK"},
        {"status: ", "#56d364", "STATUS"},
        {"connected", "#3fb950", "NODE"},
        {"peer", "#3fb950", "NODE"},
        {"node", "#3fb950", "NODE"},
        {"copied", "#8b949e", "CLIP"},
        {"saved", "#3fb950", "SAVE"},
};

// Badge/accent for a recorded UI freeze. Amber, matching the footer's stall
// alert icon, and looked up from one place so the log entry and the quick-filter
// chip always agree.
const char *const kStallBadge = "STALL";
const char *const kStallAccent = "#d29922";

// Did this line report a request the server answered successfully with a
// payload that says so? A verbose request line reads
// "net GET 200 [body: …] <url> · <event>"; a failed one reads "net GET ERR 429
// …", and a 2xx carrying {"ok":false,…} is a failure the status code alone does
// not show. `withoutBody` is the line with the peeked snippet removed and
// `body` is that snippet, both lower-cased.
bool networkLogRequestSucceeded(const QString &withoutBody, const QString &body)
{
    if (!withoutBody.startsWith(QLatin1String("net ")))
        return false;
    const QStringList parts =
        withoutBody.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3)
        return false;
    bool numeric = false;
    const int code = parts.at(2).toInt(&numeric);
    if (!numeric || code < 200 || code >= 400)
        return false;
    QString compact = body;
    compact.remove(QLatin1Char(' '));
    return compact.contains(QLatin1String("\"ok\":true"));
}

// The inverse: did this line report a reply that failed? The finished() logger
// writes a literal "ERR" marker in the status slot ("net GET ERR 503 …"), which
// is the authoritative signal and the only one left for a status code the
// server sent no reason phrase and no body with. Before adhoc #1613 these lines
// were caught by the word-match below purely because Qt's boilerplate error
// string happened to start with "Error transferring"; dropping that boilerplate
// would otherwise have painted a bare "net GET ERR 503 <url> · release fetch"
// as a plain REPO line. `withoutBody` is lower-cased, with the peeked snippet
// removed so a body of its own can't fake the marker.
bool networkLogRequestFailed(const QString &withoutBody)
{
    if (!withoutBody.startsWith(QLatin1String("net ")))
        return false;
    const QStringList parts =
        withoutBody.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.size() >= 3 && parts.at(2) == QLatin1String("err");
}

NetworkLogStyle networkLogStyleFor(const QString &message)
{
    const QString lower = message.toLower();
    // A watchdog-recorded UI stall gets its own badge so freezes stand out in the
    // log (and can be filtered to). Checked ahead of the error precedence below:
    // the entry names the blocking operation, whose breadcrumb can itself contain
    // "failed"/"unable" and would otherwise mis-badge the stall as ERROR.
    if (lower.contains(QLatin1String("ui stalled")))
        return {QString::fromLatin1(kStallAccent), QString::fromLatin1(kStallBadge)};
    // A Worker hit the tail reported as healthy is a CLOUD line whatever it
    // names (adhoc #1613): the request's own URL can carry the error vocabulary
    // the scan below looks for — /api/error_log is a real route — and a 200 on
    // it is not this app failing. The monitor's own failures use a different
    // prefix and fall through to that scan, which is what alerts on them.
    if (lower.startsWith(QLatin1String("cloud hit: ")))
        return {QStringLiteral("#f6821f"), QStringLiteral("CLOUD")};
    // A successful "Git: merged <base> into <branch> in its linked worktree."
    // line embeds the branch name verbatim, and agent branches are slugged
    // from issue titles — so a branch like "…-am-unable-continue-agent" trips
    // the "unable" error keyword below and mis-badges a successful merge as
    // ERROR (adhoc #1620). Checked ahead of the error precedence, same as the
    // stall case above.
    if (lower.startsWith(QLatin1String("git: merged ")) &&
        lower.contains(QLatin1String(" in its linked worktree")))
        return {QStringLiteral("#a371f7"), QStringLiteral("MERGE")};
    // A footer outcome line quotes the command it summarises, and a git crumb
    // routinely names files the error scan below matches on — a diff scoped to
    // this repo's own tests lists test_client_error_reporting.py, so a healthy
    // ✓ run was painted red (adhoc #1620). The line's own marker already states
    // the outcome, so classify it from that rather than from the crumb it
    // carries: ✓ / ✕ is exactly the distinction the two badges exist to draw,
    // and a genuine failure is not what these lines report in the first place.
    if (lower.startsWith(QLatin1String("background "))) {
        if (lower.contains(QLatin1String(" not backgrounded")))
            return {QStringLiteral("#f0883e"), QStringLiteral("BGBLOCK")};
        if (lower.contains(QLatin1String(" backgrounded")))
            return {QStringLiteral("#8b949e"), QStringLiteral("BGTASK")};
    }
    // Category should reflect *what drove the request*, not the payload the
    // server happened to return. The verbose "net" log line embeds a peeked
    // response snippet as "[body: …]", and a repository object always carries
    // fields like "solana" and "lastSync" — so matching the rules against the
    // body mis-badged a catalog publish as WALLET (from the "solana" JSON key)
    // or SYNC (from "lastSync"). Drop the bracketed body before classifying so
    // the badge comes from the verb/URL/event instead (adhoc #182).
    QString forRules = lower;
    QString body;
    const int bodyStart = forRules.indexOf(QLatin1String("[body:"));
    if (bodyStart >= 0) {
        // The body snippet can itself contain ']' (JSON arrays), so cut to the
        // last ']' — the closing bracket we appended, since the trailing URL
        // and event text don't contain one.
        const int bodyEnd = forRules.lastIndexOf(QLatin1Char(']'));
        if (bodyEnd > bodyStart) {
            body = forRules.mid(bodyStart, bodyEnd - bodyStart + 1);
            forRules.remove(bodyStart, bodyEnd - bodyStart + 1);
        }
    }
    // Errors / failures take precedence over any category — red is reserved
    // for these so it always means "something failed." The test reads the
    // server's own words too, because a failed reply's explanation is often
    // only in that peeked body — but not when the request succeeded and the
    // body itself reports ok: a recovered ping quotes the outage it closes
    // ("Last failure: mirror10 has not supplied…"), which painted a healthy
    // "net GET 200 [body: {"ok":true,…}]" line red (adhoc #1546).
    if (networkLogRequestFailed(forRules))
        return {QStringLiteral("#f85149"), QStringLiteral("ERROR")};
    const QString &forErrors =
        networkLogRequestSucceeded(forRules, body) ? forRules : lower;
    if (forErrors.contains("fail") || forErrors.contains("error") ||
        forErrors.contains("could not") || forErrors.contains("couldn't") ||
        forErrors.contains("no live") || forErrors.contains("denied") ||
        forErrors.contains("blocks ") || forErrors.contains("unable")) {
        return {QStringLiteral("#f85149"), QStringLiteral("ERROR")};
    }
    for (const Rule &r : kNetworkLogRules) {
        if (forRules.contains(QLatin1String(r.needle)))
            return {QString::fromLatin1(r.accent), QString::fromLatin1(r.badge)};
    }
    return {QStringLiteral("#6e7681"), QStringLiteral("INFO")};
}

// Direct badge-name -> accent lookup (as opposed to networkLogStyleFor's
// substring match against a message), used to colour the quick-filter chips
// the same as the log entries they filter.
QString accentForBadge(const QString &badge)
{
    if (badge == QLatin1String("ERROR"))
        return QStringLiteral("#f85149");
    if (badge == QLatin1String(kStallBadge))
        return QString::fromLatin1(kStallAccent);
    if (badge == QLatin1String("INFO"))
        return QStringLiteral("#6e7681");
    for (const Rule &r : kNetworkLogRules) {
        if (badge == QLatin1String(r.badge))
            return QString::fromLatin1(r.accent);
    }
    return QStringLiteral("#8b949e");
}

// Stored format: "yyyy-MM-dd HH:mm:ss  message  [path:line]". Parses leniently
// so any legacy/odd line still renders (as a plain message with no timestamp),
// and lines predating the source tail simply report no origin.
void parseStoredLogLine(const QString &storedLine, QString &date, QString &time,
                         QString &message, QString *sourcePath = nullptr,
                         int *sourceLine = nullptr)
{
    message = storedLine;
    if (storedLine.size() >= 21 && storedLine.at(10) == QLatin1Char(' ')) {
        date = storedLine.left(10);
        time = storedLine.mid(11, 8);
        message = storedLine.mid(21);
    }
    QString body;
    if (forkmesh::splitLogSource(message, &body, sourcePath, sourceLine))
        message = body;
}

QString formatDayDividerHtml(const QString &date, bool dark)
{
    const QString dividerLabelColor =
        dark ? QStringLiteral("#8b949e") : QStringLiteral("#656d76");
    const QString dividerDashColor =
        dark ? QStringLiteral("#484f58") : QStringLiteral("#afb8c1");
    const QString pretty =
        QDate::fromString(date, QStringLiteral("yyyy-MM-dd"))
            .toString(QStringLiteral("dddd, d MMMM yyyy"));
    return QString::fromUtf8(
               "<span style='color:%1'>"
               "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80&nbsp;</span>"
               "<span style='color:%2; font-weight:600'>%3</span>"
               "<span style='color:%4'>&nbsp;"
               "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80</span>")
        .arg(dividerDashColor, dividerLabelColor,
             (pretty.isEmpty() ? date : pretty).toHtmlEscaped(), dividerDashColor);
}

// Wraps http(s) URLs in the (already HTML-escaped) message with <a> tags so
// they render as clickable links that open in the system browser (adhoc #42),
// without disturbing the surrounding escaped text.
QString linkifyEscapedMessage(const QString &escaped)
{
    static const QRegularExpression urlRe(
        QStringLiteral("https?://(?:[^\\s&<]|&(?:amp|lt|gt|quot|#39);)+"));
    QString html;
    int lastEnd = 0;
    auto it = urlRe.globalMatch(escaped);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        // Trailing punctuation commonly follows a URL in log prose ("...stats.")
        // and shouldn't be swallowed into the link itself.
        QString url = m.captured(0);
        int len = url.size();
        while (len > 0 &&
               QStringLiteral(".,;:!?)").contains(url.at(len - 1))) {
            --len;
        }
        url.truncate(len);
        if (url.isEmpty())
            continue;
        html += escaped.mid(lastEnd, m.capturedStart(0) - lastEnd);
        html += QStringLiteral("<a href='%1' style='color:inherit'>%1</a>").arg(url);
        lastEnd = m.capturedStart(0) + len;
    }
    html += escaped.mid(lastEnd);
    return html;
}

// The host of the first http(s) URL in a (raw, unescaped) log message, or an
// empty string when the entry references no network source. Used to fetch and
// show that site's favicon at the front of the entry.
QString firstUrlHost(const QString &message)
{
    static const QRegularExpression hostRe(
        QStringLiteral("https?://([^/\\s:?#]+)"));
    const QRegularExpressionMatch m = hostRe.match(message);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString formatLogLineHtml(const QString &time, const QString &message, bool dark,
                          const QString &iconHtml = QString(),
                          const QString &sourcePath = QString(),
                          int sourceLine = 0)
{
    const QString messageColor =
        dark ? QStringLiteral("#adbac7") : QStringLiteral("#1f2328");
    const QString timeColor =
        dark ? QStringLiteral("#6e7681") : QStringLiteral("#656d76");
    const NetworkLogStyle style = networkLogStyleFor(message);
    // The site favicon (when the entry hit a network source) leads the line so
    // requests read at a glance as "who they went to".
    QString html = iconHtml;
    // Padding a plain string collapses to one space in HTML, which is what left
    // this column ragged. Pad in non-breaking spaces instead, wide enough for
    // the longest badge ("IDENTITY"), so every message starts in the same column
    // of this monospaced view (adhoc #1559).
    const QString paddedBadge =
        style.badge.toHtmlEscaped() +
        QStringLiteral("&nbsp;").repeated(qMax(0, 8 - style.badge.size()));
    if (!time.isEmpty())
        html += QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;")
                    .arg(timeColor, time);
    html += QStringLiteral(
                "<span style='color:%1; font-weight:700'>%2</span>&nbsp;&nbsp;"
                "<span style='color:%3'>%4</span>")
                .arg(style.accent, paddedBadge,
                     messageColor,
                     forkmesh::colorizeBackgroundMarker(
                         linkifyEscapedMessage(message.toHtmlEscaped())));
    // Closing the entry: the file and line that logged it (adhoc #1587), dim
    // enough to stay out of the way of the message and clickable — it opens
    // that file in the Files explorer at that line.
    html += logSourceAnchorHtml(sourcePath, sourceLine, dark);
    return html;
}
} // namespace

// Register (or refresh) the document image resource behind a "favicon://<host>"
// reference so `view`'s <img> tags resolve. Uses the cached site favicon when
// available, otherwise the hardcoded mark for the host (or its letter badge) so
// the icon column is never blank.
void MainWindow::registerLogFaviconResource(const QString &host, QTextEdit *view)
{
    if (!view || host.isEmpty())
        return;
    QPixmap pix;
    if (m_faviconCache.contains(host)) {
        const qreal dpr = iconDevicePixelRatio();
        const int px = qMax(1, qRound(16 * dpr));
        pix = m_faviconCache.value(host)
                  .scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pix.setDevicePixelRatio(dpr);
    } else if (hasBuiltinFavicon(host)) {
        pix = builtinFavicon(host, 16);
    } else {
        // Never leave the 14px box empty: the host's letter badge stands in
        // until (or in place of) a fetched icon, so every network line in the
        // log reads with an icon (adhoc #436).
        pix = letterFavicon(host, 16);
    }
    view->document()->addResource(
        QTextDocument::ImageResource,
        QUrl(QStringLiteral("favicon://") + host), pix);
}

// Called when a favicon finishes downloading: swap the real icon in for the
// stand-in and mark both log views dirty so already-rendered entries repaint
// with it.
void MainWindow::refreshLogFavicon(const QString &host)
{
    if (host.isEmpty() || !m_faviconCache.contains(host))
        return;
    for (QTextEdit *view :
         {static_cast<QTextEdit *>(m_settingsLog), m_footerUpdateLog}) {
        if (!view)
            continue;
        registerLogFaviconResource(host, view);
        QTextDocument *doc = view->document();
        doc->markContentsDirty(0, doc->characterCount());
    }
}

// The leading <img> for a log entry that hit a network source (empty for lines
// with no URL). Ensures the favicon resource is registered on `view` and kicks
// off a fetch on first sighting of a host.
QString MainWindow::logFaviconTag(const QString &message, QTextEdit *view)
{
    if (!view)
        return QString();
    const QString host = firstUrlHost(message);
    // An entry that hit no network source still reserves the column, so its
    // text lines up with the requests around it (adhoc #1559).
    if (host.isEmpty())
        return logIconSpacerTag(view, 14);
    // Stand-in now, real icon once fetched.
    registerLogFaviconResource(host, view);
    fetchFaviconForHost(host); // no-op if already cached / in flight / builtin
    return QStringLiteral("<img src='favicon://%1' width='14' height='14' "
                          "style='vertical-align:middle'>&nbsp;")
        .arg(host);
}

void MainWindow::appendNetworkLogLine(const QString &storedLine)
{
    if (!m_settingsLog)
        return;

    // The badge accents read on either canvas, but the timestamp, day divider
    // and message body need per-theme greys/text so the log isn't grey text
    // washed out on the light (#ffffff) background. Dark keeps its lighter ink
    // on the near-black canvas; light uses GitHub's near-black body text.
    const bool dark = currentThemeIsDark();

    QString date, time, message, sourcePath;
    int sourceLine = 0;
    parseStoredLogLine(storedLine, date, time, message, &sourcePath, &sourceLine);

    // Day divider whenever the calendar date changes from the previous line.
    if (!date.isEmpty() && date != m_lastLogRenderDate) {
        m_lastLogRenderDate = date;
        m_settingsLog->append(formatDayDividerHtml(date, dark));
    }

    m_settingsLog->append(formatLogLineHtml(
        time, message, dark,
        logPromptIconTag(m_settingsLog, storedLine) +
            logFaviconTag(message, m_settingsLog),
        sourcePath, sourceLine));
}

// One rendered entry for the pop-out window: the same markup the Log page uses,
// against that window's own document (icon resources are per-document).
QString MainWindow::popoutLogLineHtml(const QString &storedLine,
                                      QString &runningDate)
{
    if (!m_logPopoutView)
        return QString();
    const bool dark = currentThemeIsDark();
    QString date, time, message, sourcePath;
    int sourceLine = 0;
    parseStoredLogLine(storedLine, date, time, message, &sourcePath, &sourceLine);
    QString html;
    if (!date.isEmpty() && date != runningDate) {
        runningDate = date;
        html += QStringLiteral("<div>%1</div>")
                    .arg(formatDayDividerHtml(date, dark));
    }
    html += QStringLiteral("<div>%1</div>")
                .arg(formatLogLineHtml(
                    time, message, dark,
                    logPromptIconTag(m_logPopoutView, storedLine) +
                        logFaviconTag(message, m_logPopoutView),
                    sourcePath, sourceLine));
    return html;
}

// The whole retained log in a window of its own (adhoc #1559). The Log page
// renders one 300-line segment at a time and honours the active category chip;
// this deliberately does neither — every buffered line, every category, in one
// scrollback you can park on a second screen beside the app. New entries append
// live, so it stays a view of the log rather than a snapshot of it.
void MainWindow::showNetworkLogPopout()
{
    if (m_logPopout) {
        m_logPopout->show();
        m_logPopout->raise();
        m_logPopout->activateWindow();
        return;
    }

    auto *dialog = new QDialog(this);
    m_logPopout = dialog;
    dialog->setObjectName(QStringLiteral("networkLogPopout"));
    dialog->setWindowTitle(QStringLiteral("ForkMesh log — everything"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::Window);
    dialog->resize(1180, 760);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *status = new QLabel;
    status->setObjectName(QStringLiteral("modeHint"));
    m_logPopoutStatus = status;
    layout->addWidget(status);

    auto *view = new QTextBrowser(dialog);
    m_logPopoutView = view;
    view->setObjectName(QStringLiteral("networkLogPopoutView"));
    view->setReadOnly(true);
    // Navigation is handled here rather than by the browser: the origin link
    // closing each entry (adhoc #1587) is ours to act on, and left to itself
    // QTextBrowser would try to *load* "fmlogsrc:…" over this document.
    // Everything else still opens in the system browser, as it did.
    view->setOpenLinks(false);
    connect(view, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const QString href = url.toString();
        QString sourcePath;
        int sourceLine = 0;
        if (logSourceAnchorTarget(href, &sourcePath, &sourceLine)) {
            revealLogSourceInExplorer(sourcePath, sourceLine);
            return;
        }
        // The leading plus does here what it does on the Log page (adhoc #114);
        // handling links ourselves is what finally makes it work in this window.
        const QString promptLine = logPromptAnchorLine(href);
        if (!promptLine.isEmpty()) {
            appendTextToActivePrompt(promptLine);
            return;
        }
        if (!url.scheme().startsWith(QLatin1String("http")))
            return;
        QDesktopServices::openUrl(url);
    });
    view->setLineWrapMode(QTextEdit::NoWrap);
    // 20,000 entries is a large document; skipping the undo stack keeps what it
    // costs down to the text itself.
    view->document()->setUndoRedoEnabled(false);
    layout->addWidget(view, 1);

    auto *copyButton = new QPushButton(QStringLiteral("Copy all"));
    copyButton->setObjectName(QStringLiteral("logPopoutCopyButton"));
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setToolTip(QStringLiteral("Copy every retained line as plain text"));
    connect(copyButton, &QPushButton::clicked, this, [this, copyButton] {
        QApplication::clipboard()->setText(m_networkLog.join(QLatin1Char('\n')));
        copyButton->setText(QStringLiteral("Copied!"));
    });
    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setObjectName(QStringLiteral("primaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(copyButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    dialog->show();
    dialog->raise();

    // Fill it in batches rather than as one 20,000-line insert: a single parse
    // of that much rich text blocks the event loop long enough for the stall
    // watchdog to record the very freeze this window exists to help read.
    // Batching means the event loop runs mid-fill, so the buffer is copied first
    // (the strings are shared, so this costs pointers) and lines logged while it
    // fills are held back rather than landing ahead of older ones.
    const QStringList history = m_networkLog;
    m_logPopoutFilling = true;
    constexpr int kBatch = 500;
    QString runningDate;
    const int total = history.size();
    for (int index = 0; index < total; index += kBatch) {
        if (!m_logPopoutView) { // closed while it was still filling
            m_logPopoutFilling = false;
            m_logPopoutPending.clear();
            return;
        }
        QString html;
        const int end = qMin(index + kBatch, total);
        for (int line = index; line < end; ++line)
            html += popoutLogLineHtml(history.at(line), runningDate);
        QTextCursor cursor(view->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml(html);
        status->setText(QStringLiteral("Loading the full log — %1 of %2 lines…")
                            .arg(end)
                            .arg(total));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    m_logPopoutFilling = false;
    if (!m_logPopoutView) {
        m_logPopoutPending.clear();
        return;
    }
    m_logPopoutDate = runningDate;
    const QStringList pending = m_logPopoutPending;
    m_logPopoutPending.clear();
    for (const QString &line : pending)
        appendNetworkLogPopoutLine(line);
    view->moveCursor(QTextCursor::End);
    updateNetworkLogPopoutStatus();
}

// "14122 lines · every category · live" — the pop-out's own header.
void MainWindow::updateNetworkLogPopoutStatus()
{
    if (!m_logPopoutStatus)
        return;
    const int total = m_networkLog.size();
    m_logPopoutStatus->setText(QStringLiteral("%1 line%2 · every category · live")
                                   .arg(total)
                                   .arg(total == 1 ? QString()
                                                   : QStringLiteral("s")));
}

// Mirror a freshly logged line into the pop-out window, if one is open.
void MainWindow::appendNetworkLogPopoutLine(const QString &storedLine)
{
    if (!m_logPopoutView)
        return;
    if (m_logPopoutFilling) {
        // Still rendering the history: queue the line so it lands after the
        // older entries it follows instead of ahead of them.
        m_logPopoutPending.append(storedLine);
        return;
    }
    const QString html = popoutLogLineHtml(storedLine, m_logPopoutDate);
    QScrollBar *bar = m_logPopoutView->verticalScrollBar();
    const bool atBottom = !bar || bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(m_logPopoutView->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
    // Follow the tail only when the reader was already at it.
    if (atBottom)
        m_logPopoutView->moveCursor(QTextCursor::End);
    updateNetworkLogPopoutStatus();
}

// Loads the next older page of matching lines when the user scrolls to the
// top of the network log, so history beyond the initial segment (adhoc #15)
// is reachable by scrolling back instead of being capped at whatever first
// rendered.
void MainWindow::loadOlderNetworkLogSegment()
{
    if (!m_settingsLog || m_logRenderFrom <= 0 || m_logViewMutating)
        return;
    m_logViewMutating = true;

    QStringList segment; // oldest -> newest
    int idx = m_logRenderFrom;
    while (idx > 0 && segment.size() < kNetworkLogSegmentSize) {
        --idx;
        const QString &line = m_networkLog.at(idx);
        if (!m_logFilter.isEmpty() && logBadgeFor(line) != m_logFilter)
            continue;
        segment.prepend(line);
    }
    m_logRenderFrom = idx;
    if (segment.isEmpty()) {
        m_logViewMutating = false;
        return;
    }

    const bool dark = currentThemeIsDark();
    // Seed empty (not m_lastLogRenderDate, which tracks the log's true bottom)
    // so this segment's own first line gets its own divider — the line that
    // was previously topmost already has one from when it was first rendered.
    QString runningDate;
    QString html;
    for (const QString &storedLine : std::as_const(segment)) {
        QString date, time, message, sourcePath;
        int sourceLine = 0;
        parseStoredLogLine(storedLine, date, time, message, &sourcePath,
                           &sourceLine);
        if (!date.isEmpty() && date != runningDate) {
            runningDate = date;
            html += QStringLiteral("<div>%1</div>").arg(formatDayDividerHtml(date, dark));
        }
        html += QStringLiteral("<div>%1</div>")
                    .arg(formatLogLineHtml(
                        time, message, dark,
                        logPromptIconTag(m_settingsLog, storedLine) +
                            logFaviconTag(message, m_settingsLog),
                        sourcePath, sourceLine));
    }

    QScrollBar *sb = m_settingsLog->verticalScrollBar();
    const int oldMax = sb ? sb->maximum() : 0;
    const int oldVal = sb ? sb->value() : 0;

    QTextCursor cursor(m_settingsLog->document());
    cursor.movePosition(QTextCursor::Start);
    cursor.insertHtml(html);

    // Keep the viewport anchored on the content the user was already looking
    // at instead of jumping to the very top (or bottom) of the now-longer log.
    if (sb)
        sb->setValue(oldVal + (sb->maximum() - oldMax));
    m_logViewMutating = false;
}

void MainWindow::onNetworkLogScrolled(int value)
{
    QScrollBar *sb = m_settingsLog ? m_settingsLog->verticalScrollBar() : nullptr;
    if (sb && value <= sb->minimum())
        loadOlderNetworkLogSegment();
}

// Stored format: "yyyy-MM-dd HH:mm:ss  message  [path:line]" — the badge comes
// from the message alone. The trailing origin is dropped first: a line logged
// from MainWindowIssues.cpp would otherwise badge as ISSUE whatever it says.
static QString storedLogMessage(const QString &storedLine)
{
    return forkmesh::logMessageBody(
        (storedLine.size() >= 21 && storedLine.at(10) == QLatin1Char(' '))
            ? storedLine.mid(21)
            : storedLine);
}

QString MainWindow::logBadgeFor(const QString &storedLine) const
{
    return networkLogStyleFor(storedLogMessage(storedLine)).badge;
}

QString MainWindow::logAccentFor(const QString &storedLine) const
{
    return networkLogStyleFor(storedLogMessage(storedLine)).accent;
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

    auto addChip = [this](const QString &label, const QString &category,
                          const QString &tip = QString()) {
        // The same icon-over-caption tile the repo tabs and the activity rail
        // use, with the count on its corner badge (adhoc #1633): the row used to
        // be wide text pills that only a fraction of the taxonomy fit into, and
        // the app already has one shape for "glyph, small word under it, how
        // many". Tab form so the selected filter carries the checked underline.
        auto *chip = new forkmesh::ui::VerticalIconButton(
            label, forkmesh::ui::VerticalIconButton::Tab);
        chip->setObjectName("logFilterChip");
        // The same glyph the debug strip flies for this category (adhoc #1559),
        // so the two rows read as one legend instead of two vocabularies.
        const bool seen =
            category.isEmpty() || m_logFilterCounts.value(category) > 0;
        const QString glyph =
            category.isEmpty()
                ? QStringLiteral("list-unordered")
                : forkmesh::ui::LogActivityLights::iconForBadge(category);
        // Remembered so updateLogFilterChipCounts() can refresh just the badge
        // on each chip instead of tearing the whole row down per log line.
        chip->setProperty("logChipCategory", category);
        chip->setCheckable(true);
        chip->setChecked(m_logFilter == category);
        chip->setToolTip(!tip.isEmpty() ? tip
                         : category.isEmpty()
                             ? QStringLiteral("Show every event")
                             : QStringLiteral("Show only %1 events").arg(label));
        // Tint each chip with the same accent its badge uses in the log body
        // (adhoc #15) so the filter row reads as the log's own legend instead
        // of a flat, uniformly grey button row. A category the buffer has never
        // recorded stays the strip's neutral grey, exactly as its light does.
        const QString accent =
            !seen ? QStringLiteral("#6e7681")
                  : category.isEmpty() ? QStringLiteral("#8b949e")
                                       : accentForBadge(category);
        chip->setOcticonName(glyph);
        chip->setAccentColor(QColor(accent));
        chip->setBadgeCount(logFilterChipCount(category));
        m_logFilterGroup->addButton(chip);
        m_logFilterRow->addWidget(chip);
        connect(chip, &QPushButton::clicked, this, [this, category] {
            m_logFilter = category;
            rebuildNetworkLogView();
            refreshLogTimelineChart();
        });
        // The Worker tail's chip filters like the rest of the row and doubles as
        // the monitor's light: updateCloudLogFilterChip() keeps its accent, its
        // error badge and its tooltip on the background tail's live state
        // (adhoc #1613).
        if (category == QLatin1String("CLOUD")) {
            m_cloudLogFilterChip = chip;
            updateCloudLogFilterChip();
        }
    };

    addChip(QStringLiteral("All"), QString());
    // The whole taxonomy, busiest first: the same content in the same order as
    // the debug strip's lights (adhoc #1559), so the two rows can be read
    // against each other. Categories the buffer has never recorded keep a chip
    // too (grey, no count) — the row is the complete legend, and holding the
    // empty ones in place stops the busy chips jumping around as counts change.
    // Ties fall back to the canonical taxonomy order, exactly as the lights do.
    QStringList badges = forkmesh::ui::LogActivityLights::badges();
    std::stable_sort(badges.begin(), badges.end(),
                     [this](const QString &left, const QString &right) {
                         return m_logFilterCounts.value(left) >
                                m_logFilterCounts.value(right);
                     });
    // The categories whose names don't explain themselves: the diagnostic people
    // go looking for after the window felt frozen, and the three background
    // categories — the two halves of the ✓ / ✕ split plus the mentions that are
    // neither.
    static const QHash<QString, QString> tips = {
        {QString::fromLatin1(kStallBadge),
         QStringLiteral("Show only recorded UI stalls — moments the window "
                        "froze, with the operation that blocked it")},
        {QStringLiteral("BGTASK"),
         QStringLiteral("Show only work that was backgrounded — finished off the "
                        "GUI thread, so the window stayed responsive")},
        {QStringLiteral("BGBLOCK"),
         QStringLiteral("Show only work that was not backgrounded — it ran on the "
                        "GUI thread and blocked the window while it did")},
        {QStringLiteral("BGNOTE"),
         QStringLiteral("Show only lines that mention background work without "
                        "reporting a finished run — scheduling notes and the "
                        "like, kept out of the two tallies above")},
    };
    for (const QString &badge : std::as_const(badges))
        addChip(badge, badge, tips.value(badge));
    m_logFilterRow->addStretch();
}

// How many buffered events a chip covers (adhoc #64) so the row doubles as a
// tally of what the log actually contains. Zero — the pinned STALL chip on a
// healthy session — hides the badge rather than showing a "0", which would read
// as a broken counter.
int MainWindow::logFilterChipCount(const QString &category) const
{
    return category.isEmpty() ? m_networkLog.size()
                              : m_logFilterCounts.value(category);
}

// Repaint the counts in place. logSystem() runs on every network event, so a
// full rebuildLogFilterButtons() per line (two dozen buttons destroyed and
// recreated) would be wasteful — and would drop the chip the user is hovering.
void MainWindow::updateLogFilterChipCounts()
{
    if (!m_logFilterRow)
        return;
    for (int i = 0; i < m_logFilterRow->count(); ++i) {
        QLayoutItem *item = m_logFilterRow->itemAt(i);
        // dynamic_cast, not qobject_cast: VerticalIconButton is a header-only
        // widget with no Q_OBJECT, so qobject_cast would happily "succeed" on
        // any QPushButton.
        auto *chip = item ? dynamic_cast<forkmesh::ui::VerticalIconButton *>(
                                item->widget())
                          : nullptr;
        if (!chip)
            continue;
        chip->setBadgeCount(
            logFilterChipCount(chip->property("logChipCategory").toString()));
    }
}

// The CLOUD chip filters the log like every other chip in the row; what it also
// has to say is whether the Worker tail feeding that category is actually
// running (adhoc #1613). Lit in Cloudflare's orange while it is, grey while it
// is not — never from the stored preference alone, which is what let the old
// checkbox read "on" for a token-less or crashed monitor. The Worker's own
// failures are ERROR lines, so they are counted onto the red alert badge here
// rather than into the CLOUD tally. Called from every point the monitor's
// counters or running state move.
void MainWindow::updateCloudLogFilterChip()
{
    if (!m_cloudLogFilterChip)
        return;
    const bool running = cloudLogMonitorRunning();
    m_cloudLogFilterChip->setAccentColor(QColor(
        running ? QStringLiteral("#f6821f") : QStringLiteral("#6e7681")));
    m_cloudLogFilterChip->setAlertBadgeCount(m_cloudLogMonitorErrors);
    m_cloudLogFilterChip->setToolTip(
        running ? QStringLiteral(
                      "Show only the deployed Worker's traffic \xC2\xB7 "
                      "monitoring live: %1 event%2, %3 error%4. Errors are "
                      "logged as errors and raise an alert.")
                      .arg(m_cloudLogMonitorEvents)
                      .arg(m_cloudLogMonitorEvents == 1 ? QString()
                                                        : QStringLiteral("s"))
                      .arg(m_cloudLogMonitorErrors)
                      .arg(m_cloudLogMonitorErrors == 1 ? QString()
                                                        : QStringLiteral("s"))
        : !m_cloudLogMonitorIdleReason.isEmpty()
                ? QStringLiteral(
                      "Show only the deployed Worker's traffic \xC2\xB7 not "
                      "monitoring: %1")
                      .arg(m_cloudLogMonitorIdleReason)
                : QStringLiteral(
                      "Show only the deployed Worker's traffic \xC2\xB7 not "
                      "monitoring (Settings > Watch the Cloudflare Worker log)"));
}

#ifdef FORKMESH_WINDOW_TESTS
QPushButton *MainWindow::testLogFilterChip(const QString &category) const
{
    if (!m_logFilterRow)
        return nullptr;
    for (int i = 0; i < m_logFilterRow->count(); ++i) {
        QLayoutItem *item = m_logFilterRow->itemAt(i);
        auto *chip = item ? dynamic_cast<forkmesh::ui::VerticalIconButton *>(
                                item->widget())
                          : nullptr;
        if (chip && chip->property("logChipCategory").toString() == category)
            return chip;
    }
    return nullptr;
}

QStringList MainWindow::testLogFilterChipLabels() const
{
    QStringList labels;
    if (!m_logFilterRow)
        return labels;
    for (int i = 0; i < m_logFilterRow->count(); ++i) {
        auto *chip = dynamic_cast<forkmesh::ui::VerticalIconButton *>(
            m_logFilterRow->itemAt(i)->widget());
        if (!chip)
            continue;
        // The count moved off the caption and onto the tile's corner badge
        // (adhoc #1633). Read both back so these assertions still describe the
        // whole of what a chip shows.
        labels << (chip->badgeCount() > 0
                       ? QStringLiteral("%1 %2")
                             .arg(chip->text())
                             .arg(chip->badgeCount())
                       : chip->text());
    }
    return labels;
}

void MainWindow::testResetNetworkLog()
{
    m_networkLog.clear();
    m_logFilterCounts.clear(); // the chip counts describe the buffer we just emptied
    m_networkLogDiskLines = 0;
    QFile::remove(networkLogPath());
    rebuildLogFilterButtons();
    rebuildNetworkLogView();
    refreshLogTimelineChart();
}
#endif

void MainWindow::rebuildNetworkLogView()
{
    if (!m_settingsLog)
        return;
    // Also guards the scrollbar's valueChanged (see m_logViewMutating) against
    // reacting to the clear()/appendHtml calls below.
    m_logViewMutating = true;
    m_settingsLog->clear();
    m_lastLogRenderDate.clear();

    // Render only the newest segment up front; older history loads lazily as
    // the user scrolls to the top (see loadOlderNetworkLogSegment).
    QStringList segment; // oldest -> newest
    int idx = m_networkLog.size();
    while (idx > 0 && segment.size() < kNetworkLogSegmentSize) {
        --idx;
        const QString &line = m_networkLog.at(idx);
        if (!m_logFilter.isEmpty() && logBadgeFor(line) != m_logFilter)
            continue;
        segment.prepend(line);
    }
    m_logRenderFrom = idx;
    for (const QString &line : std::as_const(segment))
        appendNetworkLogLine(line);
    // A filter that matches nothing (the pinned STALL chip on a healthy session,
    // most often) would otherwise render as a blank pane that reads like a bug.
    // Say so instead, and remember it so the next matching line replaces the
    // notice rather than appending underneath it.
    m_logFilterEmptyNotice = segment.isEmpty() && !m_logFilter.isEmpty();
    if (m_logFilterEmptyNotice) {
        const QString muted = currentThemeIsDark() ? QStringLiteral("#8b949e")
                                                   : QStringLiteral("#656d76");
        m_settingsLog->append(
            QStringLiteral("<span style='color:%1'>No %2 events recorded.</span>")
                .arg(muted, m_logFilter.toHtmlEscaped()));
    }
    m_logViewMutating = false;
}

void MainWindow::openFullLogForCategory(const QString &category)
{
    const QString trimmed = category.trimmed();
    if (trimmed.isEmpty())
        return;

    m_logFilter = trimmed;
    showSection(4);
    if (m_logNavButton)
        m_logNavButton->setChecked(true);
    if (m_logFilterRow)
        rebuildLogFilterButtons();
    if (!m_settingsLog)
        return;

    m_networkLogViewStale = false;
    rebuildNetworkLogView();
    refreshLogTimelineChart();
    m_settingsLog->moveCursor(QTextCursor::End);
}

void MainWindow::openFullLogAtFooterLine(const QString &rawLine)
{
    // Drop any active category filter first so the clicked entry is guaranteed to
    // be in the rendered segment (a filtered view might omit it), and reflect that
    // in the chips.
    const bool hadFilter = !m_logFilter.isEmpty();
    if (hadFilter) {
        m_logFilter.clear();
        rebuildLogFilterButtons();
    }
    // Open the Log section. On its first visit showSection() renders the deferred
    // history and clears m_networkLogViewStale; force a rebuild here only when it
    // didn't (already visited, or we just cleared a filter) so the full tail —
    // which contains this line — is on screen to scroll to.
    showSection(4);
    if (m_logNavButton)
        m_logNavButton->setChecked(true);
    if ((hadFilter || m_networkLogViewStale) && m_settingsLog) {
        m_networkLogViewStale = false;
        rebuildNetworkLogView();
    }
    if (!m_settingsLog)
        return;

    // The footer stores the full dated line ("yyyy-MM-dd HH:mm:ss  message  [
    // path:line]"); the Log view renders the timestamp and the origin as their
    // own pieces, so match on the message body between them.
    QString message = rawLine.trimmed();
    if (message.size() >= 21 && message.at(10) == QLatin1Char(' '))
        message = message.mid(21);
    message = forkmesh::logMessageBody(message);
    if (message.isEmpty())
        return;

    // Search backward from the end so the newest occurrence (the one the footer
    // was showing) wins when a message repeats, then bring it into view. The
    // match stays selected so the clicked entry is easy to spot.
    m_settingsLog->moveCursor(QTextCursor::End);
    if (m_settingsLog->find(message, QTextDocument::FindBackward))
        m_settingsLog->ensureCursorVisible();
    else
        m_settingsLog->moveCursor(QTextCursor::End);
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

void MainWindow::logCapturedMessage(QtMsgType type, const QString &text,
                                    const QString &sourceFile, int sourceLine)
{
    QString line = text.trimmed();
    if (line.isEmpty())
        return;
    // Qt's own warnings read as plain statements ("QProcess: Destroyed while
    // process ("git") is still running."), so without a severity word nothing
    // in the entry says it wasn't ordinary progress. Say it — "Error" also
    // earns the red ERROR badge from networkLogStyleFor().
    switch (type) {
    case QtWarningMsg:
        line = QStringLiteral("Warning: ") + line;
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        line = QStringLiteral("Error: ") + line;
        break;
    default:
        break;
    }
    // A qInfo()/qWarning() belongs to whoever emitted it, not to this relay —
    // so when Qt kept the caller's context (QT_MESSAGELOGCONTEXT builds), the
    // entry names that call site. Without it, the default arguments name this
    // line, which is at least where the message entered the app log.
    if (!sourceFile.isEmpty() && sourceLine > 0) {
        logSystemFrom(line,
                      forkmesh::logSourceRelativePath(
                          sourceFile.toUtf8().constData()),
                      sourceLine);
        return;
    }
    logSystem(line);
}

void MainWindow::logSystem(const QString &text, const char *sourceFile,
                           int sourceLine)
{
    logSystemFrom(text, forkmesh::logSourceRelativePath(sourceFile), sourceLine);
}

void MainWindow::logSystemFrom(const QString &text, const QString &sourcePath,
                               int sourceLine)
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
    // The origin is appended to the stored line, not to `plain`: everything
    // that reads an entry by its words — the badge rules, the repeat-suppressed
    // error alert, the node's self-check tally — must see the message the
    // caller wrote and not a path that happens to contain "issue" or "node".
    const QString line =
        time + "  " + plain + forkmesh::logSourceSuffix(sourcePath, sourceLine);
    // Feed the node's self-check (adhoc #27): error lines here are what a
    // headless node would otherwise only ever tell a terminal nobody reads, and
    // the running tally is pushed to every node list with the heartbeat.
    NodeDiagnostics::hostCollector().noteLogLine(plain);
    m_networkLog.append(line);
    bool chipsChanged = false;
    QStringList droppedTimelineLines;
    while (m_networkLog.size() > kNetworkLogLimit) {
        // The counts describe the buffered history, so a line ageing out of it
        // gives its category's chip back a tally point (and retires the chip
        // entirely once it was the last line of its kind).
        const QString droppedLine = m_networkLog.first();
        const QString droppedBadge = logBadgeFor(droppedLine);
        if (--m_logFilterCounts[droppedBadge] <= 0) {
            m_logFilterCounts.remove(droppedBadge);
            chipsChanged = true;
        }
        droppedTimelineLines.append(droppedLine);
        m_networkLog.removeFirst();
        // m_logRenderFrom indexes into m_networkLog; trimming the front shifts
        // every index down by one, so keep it pointed at the same line.
        if (m_logRenderFrom > 0)
            --m_logRenderFrom;
    }

    // A category we haven't seen yet earns its own quick-filter chip.
    const QString badge = networkLogStyleFor(plain).badge;
    if (++m_logFilterCounts[badge] == 1)
        chipsChanged = true;
    if (chipsChanged)
        rebuildLogFilterButtons(); // no-ops until the log section is built
    else
        updateLogFilterChipCounts(); // just repaint the numbers
    // Only render the line if it passes the active filter. The first line to
    // pass while the "No X events recorded." notice is up rebuilds the view so
    // the notice goes away instead of sitting above the entry.
    if (m_logFilter.isEmpty() || m_logFilter == badge) {
        if (m_logFilterEmptyNotice)
            rebuildNetworkLogView();
        else
            appendNetworkLogLine(line);
    }
    // The pop-out shows everything, so it takes the line whatever the page's
    // own filter is doing.
    appendNetworkLogPopoutLine(line);
    // The retained log is normally at its 20,000-line cap, so every append also
    // evicts one old line. Rebuilding and reclassifying the full visible slice
    // here made routine logging take seconds, and logging the resulting stall
    // recursively triggered another rebuild. Remove only the evicted entries,
    // then append the new one; a full rebuild remains reserved for range/filter
    // changes where it is actually needed.
    if (m_logTimelineChart) {
        for (const QString &droppedLine : std::as_const(droppedTimelineLines)) {
            const QDateTime timestamp = QDateTime::fromString(
                droppedLine.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (timestamp.isValid())
                m_logTimelineChart->removeEntry(timestamp.toMSecsSinceEpoch(),
                                                logBadgeFor(droppedLine));
        }
    }
    appendLogTimelineEntry(line);

    // Mirror the newest event onto the always-on footer log line so the latest
    // activity is visible at the bottom of the app even when the Log tab is closed.
    // Pass the full dated line (not just the message) so the bottom strip shows the
    // same timestamped log line as the Log view. The one exception is the
    // deployed Worker's own traffic (adhoc #1613): it belongs in the log, but a
    // busy Worker would hold the footer permanently, hiding what this app is
    // doing behind hits it merely observed. Its failures are ERROR lines and
    // still show, like any other failure.
    if (badge != QLatin1String("CLOUD"))
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

    // Last, once the line is safely recorded: a failure that only reached the
    // log used to be invisible unless the Log section happened to be open.
    // Announce every ERROR-badged line from this one choke point, so it doesn't
    // matter which subsystem recorded it.
    if (badge == QLatin1String("ERROR"))
        alertOnLoggedError(plain);
}

// A toast is one blob of text; the Pings page has a Title column and a Detail
// column beside it (adhoc #1629). Split the blob at its first line break — a
// git failure's first line is its summary and the rest is the transcript — and,
// failing that, at the last word before the title stops reading as a heading.
static constexpr int kPingTitleChars = 90;

static QPair<QString, QString> splitAlertForPing(const QString &text)
{
    const int newline = text.indexOf(QLatin1Char('\n'));
    QString head = (newline >= 0 ? text.left(newline) : text).simplified();
    QString rest = (newline >= 0 ? text.mid(newline + 1) : QString()).simplified();
    if (head.size() > kPingTitleChars) {
        const int space = head.lastIndexOf(QLatin1Char(' '), kPingTitleChars);
        const int cut = space > kPingTitleChars / 2 ? space : kPingTitleChars;
        const QString tail = head.mid(cut).simplified();
        rest = rest.isEmpty() ? tail
                              : tail + QLatin1Char(' ') + rest;
        head = head.left(cut).trimmed() + QString::fromUtf8("\xE2\x80\xA6");
    }
    return {head, rest};
}

// Identical error text repeating inside this window alerts once. A retry loop
// hammering the same failure should flash the window once, not once per
// attempt; the log itself still records every occurrence.
static constexpr qint64 kLoggedErrorAlertRepeatMs = 15000;
// A failing subsystem can emit distinct error lines (different URLs, different
// repos) faster than anyone can read them, and each card holds the screen for
// kToastErrorSeconds. Past this many in a window, the window keeps flashing but
// the cards give way to a single "open the Log" notice.
static constexpr qint64 kLoggedErrorBurstWindowMs = 30000;
static constexpr int kLoggedErrorBurstCards = 5;

// Runs for every ERROR-badged line reaching logSystem. Two halves: the window
// flash always fires, while the toast is skipped when the caller came through
// flashMessage and is already putting this exact text on screen.
void MainWindow::alertOnLoggedError(const QString &message)
{
    // A headless node has no window to flash and nobody to read a card; the
    // line is in the log and in the node's self-check tally either way.
    if (m_headless)
        return;
    // Nothing is on screen yet — early startup logs before the UI is built.
    if (!m_topMessage)
        return;
    // A repaint or animation on the alert path logging its own failure must not
    // re-enter this and alert about the alert.
    if (m_inLoggedErrorAlert)
        return;
    if (!QSettings().value(kErrorLogAlertSetting, true).toBool())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (message == m_lastLoggedErrorText &&
        now - m_lastLoggedErrorAtMs < kLoggedErrorAlertRepeatMs)
        return;
    m_lastLoggedErrorText = message;
    m_lastLoggedErrorAtMs = now;

    if (now - m_loggedErrorBurstStartMs > kLoggedErrorBurstWindowMs) {
        m_loggedErrorBurstStartMs = now;
        m_loggedErrorBurstCount = 0;
        m_loggedErrorBurstNoticeShown = false;
    }
    const bool burst = ++m_loggedErrorBurstCount > kLoggedErrorBurstCards;

    m_inLoggedErrorAlert = true;
    // The red edge pulse (adhoc #77), plus the platform's own window/taskbar
    // attention flash for when ForkMesh isn't the focused window.
    flashErrorBorder();
    QApplication::alert(this, 0);
    if (!m_topMessageOwnsLoggedError && (!burst || !m_loggedErrorBurstNoticeShown)) {
        // A failure supersedes any in-flight progress pill, exactly as it would
        // had the caller reported it through flashMessage. The card links into
        // the Log's ERROR filter, where the full text and everything around it is.
        m_loadStatusShowing = false;
        if (burst)
            m_loggedErrorBurstNoticeShown = true;
        const QString text =
            burst ? QStringLiteral("Errors are arriving faster than they can be "
                                   "shown. Open the Log for the full list.")
                  : message;
        // This card bypasses flashMessage (the line is already in the log), so
        // file its ping here — an error the operator was shown belongs on the
        // Pings page like any other (adhoc #1629). Its cloud state is decided
        // rather than computed: a logged failure is not reported from here, so
        // it would otherwise sit at "Syncing…" waiting for a report that is
        // never sent.
        const QPair<QString, QString> split = splitAlertForPing(text);
        AppNotification item;
        item.title = split.first;
        item.body = split.second;
        item.warning = true;
        item.kind = QStringLiteral("error");
        item.quiet = true; // the card below is the toast for it
        item.syncDecided = true;
        item.sync = forkmesh::PingSync::LocalOnly;
        item.syncReason =
            QStringLiteral("a logged failure — the record is this page and the "
                           "Log, not the cloud");
        recordNotification(item);
        showTopMessage(text, true, QStringLiteral("fm:log:errors"), 0,
                       QStringLiteral("error"));
    }
    m_inLoggedErrorAlert = false;
}

// Auto-dismiss windows for the top toast. Every toast counts down visibly so the
// notification area never flashes a message away unannounced. Success
// confirmations clear quickly; errors linger far longer (but still show a
// countdown) so a failure can be read and copied before it leaves — its full text
// is also preserved in the network log regardless.
static constexpr int kToastSuccessSeconds = 5;
static constexpr int kToastErrorSeconds = 20;
static constexpr int kPromptBubbleSeconds = 8;
// A finished agent is the result the user has been waiting on, and its summary
// is a paragraph rather than a line — it holds the screen for as long as an
// error does so there is time to read it and click through to the transcript.
static constexpr int kAgentDoneToastSeconds = 20;

// How long the bubble takes to glide off the right edge once its countdown
// finishes. It stays fully opaque throughout — the exit is the motion, not a fade
// (adhoc #226), so the message is legible right up to the moment it leaves.
static constexpr int kToastSlideOutMs = 320;
// Cards enter from just below their final position; the rise itself and the
// stack's shuffle-up live in MainWindowInternal.h (kToastEntryRise /
// kToastEntryMs / kToastShiftMs) because the layout code shares them.

// Cap the visible notification backlog. A runaway retry loop firing messages
// faster than they can be read should not grow the queue without bound; the
// oldest queued message is dropped once the cap is hit.
static constexpr int kToastQueueLimit = 20;

QStringList promptAttachedImagePaths(const QString &prompt)
{
    QStringList paths;
    const QString prefix = QStringLiteral("Attached image:");
    for (const QString &line : prompt.split(QLatin1Char('\n'))) {
        if (!line.trimmed().startsWith(prefix, Qt::CaseInsensitive))
            continue;
        const QString path = line.trimmed().mid(prefix.size()).trimmed();
        if (!path.isEmpty() && !paths.contains(path))
            paths << path;
    }
    return paths;
}

QString promptTextWithoutImages(const QString &prompt)
{
    QStringList lines;
    const QString prefix = QStringLiteral("Attached image:");
    for (const QString &line : prompt.split(QLatin1Char('\n'))) {
        if (line.trimmed().startsWith(prefix, Qt::CaseInsensitive))
            continue;
        lines << line;
    }
    while (!lines.isEmpty() && lines.constFirst().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n')).trimmed();
}

QString topMessageKindLabel(const QString &kind)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("action"), QStringLiteral("Action")},
        {QStringLiteral("chat"), QStringLiteral("Chat")},
        {QStringLiteral("comment"), QStringLiteral("Comment")},
        {QStringLiteral("desktop"), QStringLiteral("System")},
        {QStringLiteral("discussion"), QStringLiteral("Discussion")},
        {QStringLiteral("error"), QStringLiteral("Error")},
        {QStringLiteral("issue"), QStringLiteral("Issue")},
        {QStringLiteral("mention"), QStringLiteral("Mention")},
        {QStringLiteral("mirror"), QStringLiteral("Mirror")},
        {QStringLiteral("pull"), QStringLiteral("Pull request")},
        {QStringLiteral("prompt"), QStringLiteral("Prompt")},
        {QStringLiteral("repo"), QStringLiteral("Repository")},
        {kAgentDoneToastKind, QStringLiteral("Agent done")},
    };
    return labels.value(kind, kind.trimmed().isEmpty()
                                  ? QStringLiteral("System")
                                  : kind.simplified());
}

// How long a card holds the screen when the caller doesn't say. Shared by the
// active toast and the queue so a celebration parked behind another card keeps
// its longer reading window when its turn comes.
static int topMessageSecondsFor(const QString &kind, bool error)
{
    if (kind == kAgentDoneToastKind)
        return kAgentDoneToastSeconds;
    return error ? kToastErrorSeconds : kToastSuccessSeconds;
}

// The agent behind an "agent finished" card, recovered from the card's own
// click target. Kept as a free function on (kind, href) rather than reading the
// active toast's members so a card still waiting in the queue can name its
// agent too — the queue carries nothing else about the run.
static int agentDoneSessionIdFor(const QString &kind, const QString &href)
{
    if (kind != kAgentDoneToastKind)
        return -1;
    const QString prefix = QStringLiteral("fm:agent:");
    if (!href.startsWith(prefix))
        return -1;
    bool ok = false;
    const int id = href.mid(prefix.size()).toInt(&ok);
    return ok ? id : -1;
}

void setTopMessageAction(QPushButton *button, int agentSessionId)
{
    if (!button)
        return;
    if (agentSessionId > 0) {
        button->setText(QStringLiteral("View agent"));
        button->setToolTip(QStringLiteral("Open the agent handling this prompt"));
        setOcticon(button, "person", 11);
    } else {
        button->setText(QStringLiteral("Send to prompt"));
        button->setToolTip(
            QStringLiteral("Add this notification to the footer prompt"));
        setOcticon(button, "paper-airplane", 11);
    }
}

// Park a message behind the toast that is currently counting down, dropping the
// oldest once the queue is full. The queue is rendered beneath the active toast
// so every pending message remains visible and can be read before its turn.
void MainWindow::queueTopMessage(const QString &text, bool error,
                                 const QString &clickHref,
                                 const QString &kind, int durationSeconds,
                                 int actionRunId)
{
    const QString trimmed = text.simplified();
    if (trimmed.isEmpty())
        return;
    const int entryDuration = durationSeconds > 0
                                  ? durationSeconds
                                  : topMessageSecondsFor(kind, error);
    m_topMessageQueue.append(
        {m_nextTopMessageQueueId++, trimmed, error, clickHref, entryDuration,
         kind, actionRunId, m_pendingToastAvatar});
    while (m_topMessageQueue.size() > kToastQueueLimit)
        m_topMessageQueue.removeFirst();
    renderTopMessageQueue();
    // Glide the active bubble up above the new card instead of teleporting it,
    // so a burst of arrivals reads as the stack sliding up one row at a time.
    positionTopMessageBubble(true);
    renderTopMessageCountdown();
}

// Unlike the previous "+N more" counter, queued notifications stay on screen
// in their arrival order. The active toast sits immediately above this list, so
// each incoming card pushes older notifications upward instead of covering them.
void MainWindow::renderTopMessageQueue()
{
    if (!m_topMessageQueueLayout || !m_topMessageQueueContent ||
        !m_topMessageQueueScroll)
        return;

    while (QLayoutItem *item = m_topMessageQueueLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    for (const auto &entry : m_topMessageQueue) {
        auto *card = new QFrame(m_topMessageQueueContent);
        card->setObjectName("topMessageQueueCard");
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *column = new QVBoxLayout(card);
        column->setContentsMargins(kToastPadLeft, kToastPadTop, kToastPadRight,
                                   kToastPadBottom);
        column->setSpacing(kToastRowSpacing);

        // The kind badge shares the caption row with the countdown and the
        // buttons, exactly like the active toast, so a queued card is two lines
        // instead of three and the whole column stays compact.
        auto *typeBadge = new QLabel(topMessageKindLabel(entry.kind), card);
        typeBadge->setObjectName("topMessageQueueTypeBadge");
        typeBadge->setFocusPolicy(Qt::NoFocus);

        auto *label = new QLabel(card);
        label->setObjectName("topMessageQueueText");
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const QString color = entry.error ? QStringLiteral("#f85149")
                                          : QStringLiteral("#3fb950");
        const QString glyph = entry.error ? QString::fromUtf8("\xE2\x9C\x95")
                                          : QString::fromUtf8("\xE2\x9C\x93");
        label->setText(QStringLiteral("<span style='color:%1'>%2 %3</span>")
                           .arg(color, glyph, entry.text.toHtmlEscaped()));
        if (entry.avatar.isNull()) {
            column->addWidget(label);
        } else {
            // A waiting chat card keeps the sender it arrived with, in the same
            // left lane the active toast uses, so the stack reads as one list.
            auto *messageRow = new QWidget(card);
            messageRow->setObjectName("topMessageQueueContentRow");
            auto *messageLayout = new QHBoxLayout(messageRow);
            messageLayout->setContentsMargins(0, 0, 0, 0);
            messageLayout->setSpacing(kToastAvatarGap);
            auto *face = new QLabel(messageRow);
            face->setObjectName("topMessageQueueAvatar");
            face->setFocusPolicy(Qt::NoFocus);
            face->setFixedSize(kToastAvatarPx, kToastAvatarPx);
            face->setScaledContents(false);
            face->setAlignment(Qt::AlignCenter);
            face->setPixmap(entry.avatar);
            messageLayout->addWidget(face, 0, Qt::AlignTop);
            messageLayout->addWidget(label, 1);
            column->addWidget(messageRow);
        }

        auto *actions = new QWidget(card);
        actions->setObjectName("topMessageQueueActions");
        actions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(0, 0, 0, 0);
        actionRow->setSpacing(4);
        actionRow->addWidget(typeBadge);
        // "queued · 5s" instead of a sentence: the caption row has to hold the
        // buttons too, and the tooltip carries the explanation.
        auto *timer = new QLabel(
            QString::fromUtf8("queued \xC2\xB7 %1s").arg(entry.durationSeconds),
            actions);
        timer->setObjectName("topMessageQueueMeta");
        timer->setFocusPolicy(Qt::NoFocus);
        timer->setToolTip(QStringLiteral("Counts down for %1s once it reaches "
                                         "the top of the stack")
                              .arg(entry.durationSeconds));
        actionRow->addWidget(timer);
        actionRow->addStretch(1);

        auto *copy = new QPushButton(QStringLiteral("Copy"), actions);
        copy->setObjectName("ghostButton");
        copy->setCursor(Qt::PointingHandCursor);
        copy->setToolTip(QStringLiteral("Copy this bubble's text"));
        copy->setFocusPolicy(Qt::NoFocus);
        setOcticon(copy, "copy", 12);
        connect(copy, &QPushButton::clicked, this, [text = entry.text] {
            QGuiApplication::clipboard()->setText(text);
        });
        actionRow->addWidget(copy);

        // A finished run's card offers the run itself rather than the prompt
        // composer, exactly like the active toast does — a celebration waiting
        // its turn is still the result the user has been waiting on, and the
        // click should land in the transcript without waiting for the countdown.
        const int queuedSessionId =
            agentDoneSessionIdFor(entry.kind, entry.clickHref);
        auto *sendToPrompt = new QPushButton(actions);
        sendToPrompt->setObjectName("topMessageAction");
        sendToPrompt->setCursor(Qt::PointingHandCursor);
        sendToPrompt->setFocusPolicy(Qt::NoFocus);
        setTopMessageAction(sendToPrompt, queuedSessionId);
        connect(sendToPrompt, &QPushButton::clicked, this,
                [this, text = entry.text, id = entry.id, queuedSessionId] {
                    if (queuedSessionId > 0) {
                        // Opening the agent answers the card, so retire it
                        // instead of leaving it queued behind the transcript.
                        dismissQueuedTopMessage(id);
                        switchToAgentsTab(queuedSessionId);
                        return;
                    }
                    appendTopMessageToPrompt(text);
                });
        actionRow->addWidget(sendToPrompt);

        auto *close = new QPushButton(actions);
        close->setObjectName("ghostButton");
        setOcticon(close, "x", 12);
        close->setCursor(Qt::PointingHandCursor);
        close->setToolTip(QStringLiteral("Dismiss"));
        close->setFocusPolicy(Qt::NoFocus);
        connect(close, &QPushButton::clicked, this, [this, id = entry.id] {
            dismissQueuedTopMessage(id);
        });
        actionRow->addWidget(close);

        column->addWidget(actions);
        m_topMessageQueueLayout->addWidget(card);
    }

    m_topMessageQueueScroll->setVisible(!m_topMessageQueue.isEmpty());
}

// The active bubble and every queued card hand their exact text to the same
// footer composer, keeping the prompt hand-off identical whichever alert is
// currently in front.
void MainWindow::appendTopMessageToPrompt(const QString &text)
{
    if (!m_issueQuickAdd || text.isEmpty())
        return;
    QString draft = m_issueQuickAdd->toPlainText();
    if (!draft.trimmed().isEmpty()) {
        if (!draft.endsWith(QStringLiteral("\n\n"))) {
            if (draft.endsWith(QLatin1Char('\n')))
                draft += QLatin1Char('\n');
            else
                draft += QStringLiteral("\n\n");
        }
    } else {
        draft.clear();
    }
    draft += text;
    m_issueQuickAdd->setPlainText(draft);
    m_issueQuickAdd->moveCursor(QTextCursor::End);
    m_issueQuickAdd->setFocus();
}

// Queued cards are independently dismissible. Use their stable id rather than
// their current position because other cards may arrive while the stack is open.
void MainWindow::dismissQueuedTopMessage(quint64 id)
{
    for (auto it = m_topMessageQueue.begin(); it != m_topMessageQueue.end(); ++it) {
        if (it->id != id)
            continue;
        m_topMessageQueue.erase(it);
        renderTopMessageQueue();
        positionTopMessageBubble(true); // the stack closes the gap smoothly
        renderTopMessageCountdown();
        return;
    }
}

// True while a toast is on screen with its countdown still running: a new
// background event must queue instead of stomping what is being read.
bool MainWindow::topMessageBusy() const
{
    return m_topMessage && m_topMessage->isVisible() &&
           ((m_topMessageTimer && m_topMessageTimer->isActive()) ||
            m_topMessageSlidingOut || m_topMessageEntering);
}

// The session an "agent finished" card belongs to, read back from its own click
// target. Carrying it in the href rather than in a member is what lets the card
// survive the queue: a celebration parked behind another toast is replayed
// through showTopMessage() with nothing but its text, kind and href, and this
// recovers the agent from that alone.
int MainWindow::topMessageAgentDoneSessionId() const
{
    return agentDoneSessionIdFor(m_topMessageKind, m_topMessageHref);
}

// The headline over a finished run's summary: whose run it was, and the figures
// worth knowing at a glance (issue, run time, spend). Composed here rather than
// baked into the toast text so a card that waited in the queue still shows the
// session's final numbers.
QString MainWindow::agentDoneHeadlineHtml(int sessionId)
{
    const AgentSession *session = findAgentSession(sessionId);
    const QString party = QString::fromUtf8("\xF0\x9F\x8E\x89");   // 🎉
    const QString sparkle = QString::fromUtf8("\xE2\x9C\xA8");     // ✨
    const QString dot = QString::fromUtf8(" \xC2\xB7 ");           // ·
    QStringList facts;
    if (session) {
        if (session->issueNumber > 0)
            facts << QStringLiteral("#%1").arg(session->issueNumber);
        if (!session->name.isEmpty())
            facts << session->name.toHtmlEscaped();
        const qint64 ran = session->finishedAtMs > session->startedAtMs &&
                                   session->startedAtMs > 0
                               ? session->finishedAtMs - session->startedAtMs
                               : session->durationMs;
        if (ran > 0)
            facts << formatDuration(ran);
        if (session->costUsd > 0)
            facts << QStringLiteral("$%1").arg(session->costUsd, 0, 'f', 2);
    }
    // The headline itself is the link to the transcript, so the summary below it
    // stays plain, readable prose instead of a paragraph of underlined blue.
    const QString heading =
        QStringLiteral("%1 <a href='fm:agent:%2' "
                       "style='color:#3fb950;text-decoration:none'>"
                       "<b>Agent #%2 is done!</b></a> %3")
            .arg(party)
            .arg(sessionId)
            .arg(sparkle);
    if (facts.isEmpty())
        return QStringLiteral("<span style='color:#3fb950'>%1</span>").arg(heading);
    return QStringLiteral("<span style='color:#3fb950'>%1</span>"
                          "<span style='color:#8b949e'>%2%3</span>")
        .arg(heading, dot, facts.join(dot));
}

#ifdef FORKMESH_WINDOW_TESTS
// Out of line because MainWindow.h only forward-declares QLabel.
QString MainWindow::testTopMessageAgentHeadline() const
{
    if (!m_topMessageAgentRow || !m_topMessageAgentHeadline || !m_topMessageBody ||
        !m_topMessageAgentRow->isVisibleTo(m_topMessageBody))
        return QString();
    return m_topMessageAgentHeadline->text();
}

bool MainWindow::testTopMessageAgentIconShown() const
{
    return m_topMessageAgentIcon && !m_topMessageAgentIcon->pixmap().isNull();
}

bool MainWindow::testTopMessageAvatarShown() const
{
    return m_topMessageAvatar && m_topMessageContainer &&
           m_topMessageAvatar->isVisibleTo(m_topMessageContainer) &&
           !m_topMessageAvatar->pixmap().isNull();
}
#endif

// (Re)paint the prompt-anchored bubble from m_topMessageRaw. Every message is
// shown whole — it wraps across the bubble's full width and the bubble grows to
// fit, so no notification is ever cut off behind an ellipsis.
void MainWindow::renderTopMessage()
{
    if (!m_topMessage)
        return;
    // Whoever this card is about, in the lane down its left edge (adhoc #1612).
    // Only a chat ping fills it; everything else keeps the full bubble width.
    if (m_topMessageAvatar) {
        m_topMessageAvatar->setPixmap(m_topMessageAvatarPixmap);
        m_topMessageAvatar->setVisible(!m_topMessageAvatarPixmap.isNull());
    }
    // Green for success, red for failure.
    const QString fg = m_topMessageError ? "#f85149" : "#3fb950";
    const QString glyph = m_topMessageError ? QString::fromUtf8("\xE2\x9C\x95")  // ✕
                                            : QString::fromUtf8("\xE2\x9C\x93"); // ✓
    // A finished agent gets the celebration treatment: its own list icon and a
    // headline naming the run, with the summary it signed off with underneath.
    const int doneSessionId = topMessageAgentDoneSessionId();
    if (m_topMessageAgentRow) {
        const bool celebrating = doneSessionId > 0;
        if (celebrating && m_topMessageAgentIcon && m_topMessageAgentHeadline) {
            // The very glyph this session wears in the agents list, so the card
            // is unmistakably that agent's rather than a generic green tick.
            const AgentSession *session = findAgentSession(doneSessionId);
            const QIcon icon =
                session ? agentStatusOcticon(*session, kToastAgentIconPx)
                        : themedOcticon(QStringLiteral("check-circle"),
                                        QColor("#3fb950"), kToastAgentIconPx);
            m_topMessageAgentIcon->setPixmap(
                icon.pixmap(kToastAgentIconPx, kToastAgentIconPx));
            m_topMessageAgentHeadline->setText(agentDoneHeadlineHtml(doneSessionId));
        }
        m_topMessageAgentRow->setVisible(celebrating);
    }
    // When a click target is set, the message text itself becomes a link so e.g.
    // an "agent is waiting for you" bubble jumps straight to that agent.
    QString visiblePrompt = promptTextWithoutImages(m_topMessageRaw);
    if (visiblePrompt.isEmpty() && m_topMessageIsPromptBubble &&
        !m_topMessagePromptImagePaths.isEmpty())
        visiblePrompt = QStringLiteral("Image attachment");
    QString body = (m_topMessageIsPromptBubble ? visiblePrompt : m_topMessageRaw)
                       .toHtmlEscaped();
    body.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    // A celebration's link lives on its headline instead (see
    // agentDoneHeadlineHtml) — the summary underneath is a paragraph of the
    // agent's own prose and reads far better left unlinked.
    if (!m_topMessageHref.isEmpty() && doneSessionId <= 0)
        body = QStringLiteral(
                   "<a href='%1' style='color:%2;text-decoration:underline'>%3</a>")
                   .arg(m_topMessageHref.toHtmlEscaped(), fg, body);
    if (m_topMessageIsPromptBubble) {
        if (m_topMessagePromptHeader) {
            m_topMessagePromptHeader->setText(
                QStringLiteral("<span style='color:%1'><b>↗ Prompt sent</b></span>")
                    .arg(fg));
            m_topMessagePromptHeader->show();
        }
        updateTopMessagePromptLiveStatus(m_topMessagePromptStatus);
        m_topMessageBaseHtml = QStringLiteral("<span style='color:%1'>%2</span>")
                                   .arg(fg, body);
    } else if (doneSessionId > 0) {
        if (m_topMessagePromptHeader)
            m_topMessagePromptHeader->hide();
        if (m_topMessagePromptStatusLabel)
            m_topMessagePromptStatusLabel->hide();
        // No status glyph: the headline row above already carries the agent's
        // icon, and the summary keeps the ordinary text colour so several lines
        // of prose stay comfortable to read.
        m_topMessageBaseHtml = body;
    } else {
        if (m_topMessagePromptHeader)
            m_topMessagePromptHeader->hide();
        if (m_topMessagePromptStatusLabel)
            m_topMessagePromptStatusLabel->hide();
        m_topMessageBaseHtml = QStringLiteral("<span style='color:%1'>%2 %3</span>")
                                   .arg(fg, glyph, body);
    }
    m_topMessage->setText(m_topMessageBaseHtml);
    renderTopMessagePromptImages();
    if (m_topMessageTypeBadge) {
        m_topMessageTypeBadge->setText(
            m_topMessageIsPromptBubble ? QStringLiteral("Prompt")
                                       : topMessageKindLabel(m_topMessageKind));
        m_topMessageTypeBadge->show();
    }
}

// Refresh just the prompt bubble's status line without touching the header,
// message body, or image thumbnails — called both from renderTopMessage() and,
// as the agent streams, live from applyTranscriptEvent() (adhoc #1570). Elided
// to a single short line so a long tool command or file path never wraps the
// bubble onto a second line.
void MainWindow::updateTopMessagePromptLiveStatus(const QString &line)
{
    if (!m_topMessagePromptStatusLabel)
        return;
    m_topMessagePromptStatus = line;
    const QString fg = m_topMessageError ? "#f85149" : "#3fb950";
    const int maxWidth = m_topMessagePromptStatusLabel->width() > 0
                             ? m_topMessagePromptStatusLabel->width()
                             : 480;
    const QString elided = m_topMessagePromptStatusLabel->fontMetrics().elidedText(
        line, Qt::ElideRight, maxWidth);
    m_topMessagePromptStatusLabel->setText(
        elided.isEmpty() ? QString()
                          : QStringLiteral("<span style='color:%1'>%2</span>")
                                .arg(fg, elided.toHtmlEscaped()));
    m_topMessagePromptStatusLabel->setVisible(!line.isEmpty());
}

void MainWindow::renderTopMessagePromptImages()
{
    if (!m_topMessagePromptImages)
        return;
    auto *row = qobject_cast<QHBoxLayout *>(m_topMessagePromptImages->layout());
    if (!row)
        return;
    while (QLayoutItem *item = row->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    for (const QString &path : std::as_const(m_topMessagePromptImagePaths)) {
        auto *thumbnail = new QPushButton(m_topMessagePromptImages);
        thumbnail->setObjectName("topMessagePromptImage");
        // A confirmation only has to prove which images went with the prompt;
        // a caption-sized chip does that without doubling the card's height
        // (click it for the full-size view).
        thumbnail->setFixedSize(46, 46);
        thumbnail->setIconSize(QSize(40, 40));
        thumbnail->setCursor(Qt::PointingHandCursor);
        thumbnail->setToolTip(QStringLiteral("View %1")
                                  .arg(QFileInfo(path).fileName()));
        const QPixmap pixmap(path);
        if (!pixmap.isNull()) {
            thumbnail->setIcon(QIcon(pixmap));
        } else {
            thumbnail->setText(QStringLiteral("Image"));
        }
        connect(thumbnail, &QPushButton::clicked, this,
                [this, path] { showQuickAddImageDetail(path); });
        row->addWidget(thumbnail);
    }
    row->addStretch(1);
    m_topMessagePromptImages->setVisible(
        m_topMessageIsPromptBubble && !m_topMessagePromptImagePaths.isEmpty());
}

// Exact height the bubble's stacked lines occupy at a given width. Measured
// through the layout's heightForWidth, which asks every wrapped label how tall
// it really is at that width. The container's own sizeHint cannot answer this —
// a word-wrapped QLabel reports its unwrapped metrics there, which sized the
// bubble to a wildly wrong height and left the message floating in empty bands
// (adhoc #1444).
int MainWindow::topMessageBodyHeight(int textWidth) const
{
    if (!m_topMessageBody)
        return 0;
    QLayout *layout = m_topMessageBody->layout();
    if (!layout)
        return 0;
    layout->activate();
    const int hfw = layout->hasHeightForWidth()
                        ? layout->heightForWidth(textWidth)
                        : -1;
    // No wrapped line to measure (a plain one-line pill, say): the layout's own
    // sizeHint is exact for those.
    return hfw > 0 ? hfw : layout->sizeHint().height();
}

// The widget the stack hangs above. Normally the composer frame; when the
// composer is collapsed to its round avatar the host is that button, and the
// stack has to clear it too rather than sitting on top of it. A popped-out
// composer lives in its own top-level window — it is not in this window's
// hierarchy, so mapTo() would walk off the end of it — and then there is nothing
// down there to avoid.
QWidget *MainWindow::topMessagePromptAnchor() const
{
    for (QWidget *candidate : {static_cast<QWidget *>(m_promptWrapper),
                               static_cast<QWidget *>(m_promptOverlayHost)}) {
        if (candidate && candidate->isVisible() && isAncestorOf(candidate))
            return candidate;
    }
    return nullptr;
}

// Highest y the stack may reach. Below the window chrome, and no further up than
// kToastStackHeightShare of the band between the content area and the prompt, so
// a burst of tall cards stays in the lower band beside the composer instead of
// climbing over the toolbars a page keeps along its top edge (adhoc #1621).
int MainWindow::topMessageStackCeiling(int promptTop, int margin) const
{
    int contentTop = margin;
    if (m_globalOverlayHost && isAncestorOf(m_globalOverlayHost))
        contentTop = qMax(contentTop,
                          m_globalOverlayHost->mapTo(this, QPoint()).y() + margin);
    const int band = qMax(0, promptTop - contentTop);
    int ceiling = promptTop - qRound(band * kToastStackHeightShare);
    // A window too short for that share to hold one readable card keeps the card:
    // reaching a little higher beats a stack nothing can be read in.
    ceiling = qMin(ceiling, promptTop - kToastMinStackHeight);
    // 40px is the shortest bubble the layout below will produce, so the ceiling
    // can never be pushed past the point where even that would not fit.
    return qBound(margin, ceiling, qMax(margin, promptTop - 40));
}

// Calculate a readable floating-bubble rectangle directly above the prompt.
// The prompt can be reparented into another page (for example Git's commit
// view), so its current geometry — not the window bottom — is the anchor.
QRect MainWindow::topMessageBubbleRect()
{
    if (!m_topMessageContainer)
        return {};
    const int margin = 16;
    const int available = qMax(120, width() - 2 * margin);
    const int desired = 460;
    const int bubbleWidth = qMin(available, desired);
    m_topMessageContainer->setFixedWidth(bubbleWidth);
    // Floor: the composer's top edge, less the gap the stack keeps clear of it.
    // With no composer on screen the window's own bottom edge stands in.
    m_topMessagePromptFloor = height();
    if (QWidget *anchor = topMessagePromptAnchor())
        m_topMessagePromptFloor = anchor->mapTo(this, QPoint()).y();
    const int promptTop = m_topMessagePromptFloor - kToastPromptGap;
    // Ceiling: the stack grows upward from the floor until it reaches this, and
    // only beyond it does the text scroll.
    const int ceiling = topMessageStackCeiling(promptTop, margin);
    const int roomForBubble = qMax(40, promptTop - ceiling);
    int maxBubbleHeight = qBound(40, roomForBubble, qMax(40, height() - 2 * margin));
    // Cards are waiting behind this one: leave them their share of the stack
    // rather than letting one long failure fill it and hide the column.
    if (!m_topMessageQueue.isEmpty())
        maxBubbleHeight =
            qBound(qMin(kToastMinActiveCard, maxBubbleHeight),
                   qRound(roomForBubble * kToastActiveCardShare), maxBubbleHeight);
    auto *column = m_topMessageContainer->layout();
    int bubbleHeight = 0;
    if (column && m_topMessage && m_topMessageScroll) {
        const QMargins pad = column->contentsMargins();
        // Chrome is everything the text does not get: the bubble's padding plus
        // the countdown/actions row stacked underneath it.
        int chrome = pad.top() + pad.bottom();
        if (m_topMessageActions &&
            m_topMessageActions->isVisibleTo(m_topMessageContainer))
            chrome += m_topMessageActions->sizeHint().height() + column->spacing();
        const int roomForText = qMax(18, maxBubbleHeight - chrome);
        // A chat card's avatar lane takes its width from the message beside it,
        // so the text has to be wrapped inside what is left (adhoc #1612).
        const bool wearingAvatar =
            m_topMessageAvatar &&
            m_topMessageAvatar->isVisibleTo(m_topMessageContainer);
        const int avatarLane =
            wearingAvatar ? kToastAvatarPx + kToastAvatarGap : 0;
        int textWidth =
            qMax(40, bubbleWidth - pad.left() - pad.right() - avatarLane);
        int textHeight = 0;
        if (m_topMessageBody) {
            m_topMessageBody->setFixedWidth(textWidth);
            textHeight = topMessageBodyHeight(textWidth);
        }
        if (textHeight <= 0)
            textHeight = m_topMessage->heightForWidth(textWidth);
        if (textHeight <= 0)
            textHeight = m_topMessage->sizeHint().height();
        // Only a message too tall for the window scrolls, and then the scroll bar
        // takes width from the text. Re-wrap inside what is left so the ends of
        // those lines cannot hide behind the bar.
        if (m_topMessageBody && textHeight > roomForText) {
            QScrollBar *bar = m_topMessageScroll->verticalScrollBar();
            const int barWidth = bar ? bar->sizeHint().width() : 0;
            if (barWidth > 0 && textWidth - barWidth > 40) {
                textWidth -= barWidth;
                m_topMessageBody->setFixedWidth(textWidth);
                textHeight = qMax(textHeight, topMessageBodyHeight(textWidth));
            }
        }
        textHeight = qBound(18, textHeight, roomForText);
        m_topMessageScroll->setFixedHeight(textHeight);
        column->activate();
        // A one-line chat message is shorter than the face beside it; the row
        // is as tall as the taller of the two, or the avatar would be clipped.
        bubbleHeight =
            qMax(textHeight, wearingAvatar ? kToastAvatarPx : 0) + chrome;
    }
    if (bubbleHeight <= 0)
        bubbleHeight = m_topMessageContainer->sizeHint().height();
    bubbleHeight = qBound(40, bubbleHeight, maxBubbleHeight);
    const int x = qMax(margin, width() - bubbleWidth - margin);
    int queueHeight = 0;
    if (m_topMessageQueueScroll && m_topMessageQueueContent &&
        m_topMessageQueueLayout && !m_topMessageQueue.isEmpty()) {
        m_topMessageQueueContent->setFixedWidth(bubbleWidth);
        m_topMessageQueueLayout->activate();
        // Same measurement as the active bubble: ask the cards how tall they
        // wrap at this width instead of trusting a sizeHint that ignores it.
        int contentHeight =
            m_topMessageQueueLayout->hasHeightForWidth()
                ? m_topMessageQueueLayout->heightForWidth(bubbleWidth)
                : 0;
        if (contentHeight <= 0)
            contentHeight = m_topMessageQueueLayout->minimumSize().height();
        m_topMessageQueueContent->setFixedHeight(qMax(0, contentHeight));
        const int queueRoom =
            qMax(0, roomForBubble - bubbleHeight - kToastStackGap);
        queueHeight = qMin(contentHeight, queueRoom);
        m_topMessageQueueScroll->setFixedWidth(bubbleWidth);
        m_topMessageQueueScroll->setFixedHeight(qMax(0, queueHeight));
        m_topMessageQueueScroll->setVisible(queueHeight > 0);
    } else if (m_topMessageQueueScroll) {
        m_topMessageQueueScroll->hide();
    }
    const int y = qMax(ceiling, promptTop - bubbleHeight - queueHeight -
                                    (queueHeight > 0 ? kToastStackGap : 0));
    return QRect(x, y, bubbleWidth, bubbleHeight);
}

// How far a card may be dropped below its anchor to rise back into it. The whole
// point of the motion is that it happens in the gap the anchor keeps above the
// prompt, so a card that would be pushed past the composer's top edge rises from
// wherever is left instead — no frame of the entry ever covers the prompt.
int MainWindow::topMessageEntryRise(const QRect &target) const
{
    if (m_topMessagePromptFloor < 0)
        return kToastEntryRise;
    return qBound(0, m_topMessagePromptFloor - 1 - target.bottom(), kToastEntryRise);
}

// Park the queued column immediately under the active toast. Animated, it rises
// into its new place: a card lands at the bottom of the column and the ones
// above it glide up, which is what makes a burst read as one moving stack.
void MainWindow::placeTopMessageQueue(const QRect &bubble, bool animate)
{
    if (!m_topMessageQueueScroll)
        return;
    if (m_topMessageQueueFlight)
        m_topMessageQueueFlight->stop();
    if (!m_topMessageQueueScroll->isVisible())
        return;
    const QRect target(bubble.left(), bubble.bottom() + kToastStackGap,
                       m_topMessageQueueScroll->width(),
                       m_topMessageQueueScroll->height());
    const QRect current = m_topMessageQueueScroll->geometry();
    // A column that was already on screen glides up from where it stood; the
    // first card rises out of the gap the prompt anchor reserves beneath it.
    const bool hadColumn = m_topMessageQueue.size() > 1 && current.isValid();
    const QRect source =
        hadColumn ? current : target.translated(0, topMessageEntryRise(target));
    if (animate && m_topMessageQueueFlight && source != target) {
        m_topMessageQueueScroll->setGeometry(source);
        m_topMessageQueueFlight->setStartValue(source);
        m_topMessageQueueFlight->setEndValue(target);
        m_topMessageQueueFlight->start();
    } else {
        m_topMessageQueueScroll->setGeometry(target);
    }
    if (auto *bar = m_topMessageQueueScroll->verticalScrollBar())
        bar->setValue(bar->maximum()); // keep the newest queued card visible
    m_topMessageQueueScroll->raise();
}

// Size and anchor the floating bubble. Called whenever its content changes and
// when the window moves or resizes. animate=true is for a change in stack depth:
// the toast slides to its new anchor rather than jumping there.
void MainWindow::positionTopMessageBubble(bool animate)
{
    if (!m_topMessageContainer)
        return;
    if (m_topMessageSlidingOut)
        return; // the exit animation owns the geometry until it lands
    const QRect bubble = topMessageBubbleRect();
    if (m_topMessageEntering) {
        // A card arrived while this one was still rising. Re-aim the same motion
        // at the new anchor: dropping the request instead would leave the toast
        // sitting on top of the card that just joined the stack.
        if (m_topMessageFlight)
            m_topMessageFlight->setEndValue(bubble);
        placeTopMessageQueue(bubble, animate);
        m_topMessageContainer->raise();
        return;
    }
    const QRect current = m_topMessageContainer->geometry();
    // Only a pure move is worth animating; a resize (the message changed, or the
    // window did) has to land immediately or the bubble would visibly stretch.
    if (animate && m_topMessageFlight && m_topMessageContainer->isVisible() &&
        current.size() == bubble.size() && current != bubble) {
        m_topMessageShifting = true;
        m_topMessageFlight->stop();
        m_topMessageFlight->setDuration(kToastShiftMs);
        m_topMessageFlight->setEasingCurve(QEasingCurve::OutCubic);
        m_topMessageFlight->setStartValue(current);
        m_topMessageFlight->setEndValue(bubble);
        m_topMessageFlight->start();
    } else {
        if (m_topMessageShifting && m_topMessageFlight)
            m_topMessageFlight->stop(); // don't let a shift finish over this
        m_topMessageShifting = false;
        m_topMessageContainer->setGeometry(bubble);
    }
    placeTopMessageQueue(bubble, animate);
    m_topMessageContainer->raise();
}

// Show the bubble by sliding it up into its anchor. Every arrival uses this, so a
// prompt confirmation and a background notification enter identically.
void MainWindow::animateTopMessageEntry(const QRect &target)
{
    if (!m_topMessageContainer)
        return;
    if (m_topMessageFlight)
        m_topMessageFlight->stop();
    m_topMessageSlidingOut = false;
    m_topMessageEntering = false;
    m_topMessageShifting = false;
    const QRect source = target.translated(0, topMessageEntryRise(target));
    m_topMessageContainer->setGeometry(source);
    m_topMessageContainer->show();
    m_topMessageContainer->raise();
    placeTopMessageQueue(target, false);
    if (m_topMessageFlight && source != target) {
        m_topMessageEntering = true;
        m_topMessageFlight->setDuration(kToastEntryMs);
        m_topMessageFlight->setEasingCurve(QEasingCurve::OutCubic);
        m_topMessageFlight->setStartValue(source);
        m_topMessageFlight->setEndValue(target);
        m_topMessageFlight->start();
    }
}

// A hovered bubble should remain completely stable: stop the visible seconds
// countdown, then continue it on leave. Nothing else moves while it is up — the
// bubble only travels once the countdown has run out (slideTopMessageOut).
void MainWindow::setTopMessagePaused(bool paused)
{
    if (m_topMessageHovering == paused)
        return;
    m_topMessageHovering = paused;
    if (paused) {
        if (m_topMessageTimer && m_topMessageTimer->isActive())
            m_topMessageTimer->stop();
    } else if (m_topMessageContainer && m_topMessageContainer->isVisible() &&
               !m_loadStatusShowing && !m_topMessageSlidingOut &&
               !m_topMessageEntering &&
               m_topMessageSecondsLeft > 0) {
        if (m_topMessageTimer)
            m_topMessageTimer->start(1000);
    }
    renderTopMessageCountdown();
}

// The countdown reached zero: instead of dimming the text away, keep it fully
// opaque and glide the whole bubble off the right edge (the parent clips it), then
// hand over to the next queued message. Called only from the countdown tick — an
// explicit dismiss (✕) still closes immediately.
void MainWindow::slideTopMessageOut()
{
    if (m_topMessageTimer)
        m_topMessageTimer->stop();
    m_topMessageSecondsLeft = 0;
    if (!m_topMessageContainer || !m_topMessageContainer->isVisible() ||
        !m_topMessageFlight) {
        advanceTopMessageQueue();
        return;
    }
    if (m_topMessageSlidingOut)
        return; // already on its way out
    m_topMessageEntering = false;
    m_topMessageShifting = false;
    m_topMessageSlidingOut = true;
    // Drop the countdown as it leaves, so the last thing on screen is the message
    // itself rather than a stale "0s".
    if (m_topMessageMeta)
        m_topMessageMeta->clear();
    const QRect from = m_topMessageContainer->geometry();
    const QRect to(width() + 12, from.y(), from.width(), from.height());
    m_topMessageFlight->stop();
    m_topMessageFlight->setDuration(kToastSlideOutMs);
    m_topMessageFlight->setEasingCurve(QEasingCurve::InCubic);
    m_topMessageFlight->setStartValue(from);
    m_topMessageFlight->setEndValue(to);
    m_topMessageFlight->start();
}

// After a footer send clears the editor, leave a copy of the exact prompt in a
// bubble above the editor. Keeping the entire card above the prompt means it
// never obscures either a draft or the send controls on any page.
void MainWindow::showPromptBubble(const QString &prompt, int agentSessionId,
                                  const QString &status,
                                  const QStringList &images)
{
    const QString sent = prompt.trimmed();
    if (sent.isEmpty() || !m_topMessage || !m_topMessageContainer)
        return;
    m_loadStatusShowing = false;
    m_topMessageHref.clear();
    m_topMessageKind = QStringLiteral("prompt");
    m_topMessageAgentSessionId = agentSessionId;
    m_topMessageActionRunId = -1;
    m_topMessagePromptStatus = status.trimmed();
    m_topMessagePromptImagePaths = images;
    if (m_topMessagePromptImagePaths.isEmpty())
        m_topMessagePromptImagePaths = promptAttachedImagePaths(sent);
    m_topMessageError = false;
    m_topMessageIsPromptBubble = true;
    // A prompt confirmation is the user's own words, not a message from anyone:
    // clear any face the card it replaces was wearing.
    m_topMessageAvatarPixmap = QPixmap();
    m_topMessageRaw = sent;
    m_topMessageHovering = false;
    m_topMessageEntering = false;
    if (m_topMessageTimer)
        m_topMessageTimer->stop();
    renderTopMessage();
    m_topMessage->show(); // a previous dismiss hid the label
    if (m_topMessageCopy)
        m_topMessageCopy->show();
    if (m_topMessageSendToPrompt) {
        setTopMessageAction(m_topMessageSendToPrompt, m_topMessageAgentSessionId);
        m_topMessageSendToPrompt->show();
    }
    if (m_topMessageActionOutput)
        m_topMessageActionOutput->hide();
    if (m_topMessageClose)
        m_topMessageClose->show();
    m_topMessageSecondsLeft = kPromptBubbleSeconds; // the row is sized with its countdown in place
    renderTopMessageCountdown();
    if (m_topMessageActions)
        m_topMessageActions->show();

    // The confirmation rises into the same anchor a notification uses, so the two
    // kinds of card never enter differently.
    animateTopMessageEntry(topMessageBubbleRect());

    if (!m_topMessageTimer) {
        m_topMessageTimer = new QTimer(this);
        connect(m_topMessageTimer, &QTimer::timeout, this, [this] {
            if (!m_topMessage)
                return;
            if (--m_topMessageSecondsLeft <= 0) {
                slideTopMessageOut();
                return;
            }
            renderTopMessageCountdown();
        });
    }
    if (!m_topMessageEntering)
        m_topMessageTimer->start(1000);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Keep the floating prompt bubble anchored as the footer moves.
    if (m_topMessageContainer && m_topMessageContainer->isVisible())
        positionTopMessageBubble();
    // The notification and restart borders hug the window edges.
    if (m_errorBorderOverlay && m_errorBorderOverlay->isVisible())
        m_errorBorderOverlay->setGeometry(rect());
    if (m_celebrationBorderOverlay && m_celebrationBorderOverlay->isVisible())
        m_celebrationBorderOverlay->setGeometry(rect());
    if (m_restartCautionBorderOverlay &&
        m_restartCautionBorderOverlay->isVisible())
        m_restartCautionBorderOverlay->setGeometry(rect());
}

void MainWindow::flashMessage(const QString &text, bool error,
                              const QString &clickHref, int durationSeconds,
                              const QString &kind, int actionRunId,
                              const char *sourceFile, int sourceLine)
{
    // A real result supersedes any in-flight progress pill (showLoadStatus).
    m_loadStatusShowing = false;
    // Always keep a copy in the network log for history. An error-classified
    // line still flashes the window from there (alertOnLoggedError), but this
    // toast is the one that shows it — say so, so the hook doesn't queue a
    // second card with the same text.
    m_topMessageOwnsLoggedError = true;
    // The toast's caller, not this line: see the declaration.
    logSystem(text, sourceFile, sourceLine);
    m_topMessageOwnsLoggedError = false;
    // Every toast is an alert this app raised, so every toast is filed on the
    // Pings page — a card that slid off screen after five seconds used to be the
    // whole record of it (adhoc #1629). m_pingToastId is set when this toast is
    // the one a ping already filed asked for (recordNotification →
    // flashNotification → here), so the same event is never filed twice.
    qint64 pingId = m_pingToastId;
    // An empty text is a dismissal (flashMessage("") clears the bubble), not an
    // event: there is nothing to file.
    if (pingId == 0 && !text.simplified().isEmpty()) {
        const QPair<QString, QString> split = splitAlertForPing(text);
        AppNotification item;
        item.title = split.first;
        item.body = split.second;
        item.warning = error;
        item.kind = kind.isEmpty() ? QStringLiteral("desktop") : kind;
        item.runId = actionRunId;
        // The toast is already on screen; filing it must not raise a second one.
        item.quiet = true;
        pingId = recordNotification(item);
    }
    // An error toast is the whole record of the failure on this machine; report
    // it so it also becomes an operational record and a ping (adhoc #1538).
    // Here rather than in showTopMessage: a headless node has no toast widget at
    // all — its failures are the ones nobody can see — and the logged-error hook
    // that paints its own cards through showTopMessage must not report a line
    // that was already reported from wherever it originally failed.
    if (error)
        reportUserVisibleError(QStringLiteral("toast"), QString(), text,
                               QString(), pingId);
    showTopMessage(text, error, clickHref, durationSeconds, kind, actionRunId);
}

void MainWindow::showTopMessage(const QString &text, bool error,
                                const QString &clickHref, int durationSeconds,
                                const QString &kind, int actionRunId)
{
    if (!m_topMessage)
        return;

    const QString trimmed = text.simplified();
    if (trimmed.isEmpty()) {
        dismissTopMessage();
        return;
    }
    // Keep every notification visible. A new arrival becomes the bottom card in
    // the queue and moves the active toast up, rather than replacing a message
    // that may still be being read.
    if (topMessageBusy()) {
        queueTopMessage(trimmed, error, clickHref, kind, durationSeconds,
                        actionRunId);
        return;
    }
    // Whose face this card wears, from the ping that raised it (adhoc #1612).
    // renderTopMessage below puts it on screen.
    m_topMessageAvatarPixmap = m_pendingToastAvatar;
    // Carry an optional click target so the whole toast can act as a link (e.g. an
    // "agent is waiting for you" toast jumps to that agent). Cleared by default so
    // an ordinary toast is never left clickable from a previous message.
    m_topMessageHref = clickHref;
    m_topMessageKind = kind;
    // An "agent finished" card names its session in that href, which is what
    // turns the action row's button into "View agent" — including for a card
    // replayed out of the queue, where the href is all that survives.
    m_topMessageAgentSessionId = topMessageAgentDoneSessionId();
    m_topMessageActionRunId = actionRunId;
    m_topMessageError = error;
    m_topMessageIsPromptBubble = false;
    m_topMessagePromptStatus.clear();
    m_topMessagePromptImagePaths.clear();
    if (m_topMessagePromptHeader)
        m_topMessagePromptHeader->hide();
    if (m_topMessagePromptStatusLabel)
        m_topMessagePromptStatusLabel->hide();
    renderTopMessagePromptImages();
    m_topMessageHovering = false;
    m_topMessageRaw = trimmed;
    // The whole message is shown: it wraps to the bubble's full width and the
    // bubble grows within the prompt-anchored stack (topMessageBubbleRect), so a long git error is
    // readable in place instead of being cut off at an ellipsis.
    renderTopMessage();
    m_topMessage->show();

    if (!m_topMessageTimer) {
        // Ticks once a second so the countdown is visible; when the count runs out
        // the whole toast (message plus the action row under it) slides off to the
        // right rather than firing a single timeout.
        m_topMessageTimer = new QTimer(this);
        connect(m_topMessageTimer, &QTimer::timeout, this, [this] {
            if (!m_topMessage)
                return;
            if (--m_topMessageSecondsLeft <= 0) {
                slideTopMessageOut();
                return;
            }
            renderTopMessageCountdown();
        });
    }
    // Every bubble offers copy + prompt hand-off; errors receive a longer window
    // but otherwise behave exactly like a regular notification.
    m_topMessageSecondsLeft = durationSeconds > 0
                                  ? durationSeconds
                                  : topMessageSecondsFor(kind, error);
    if (m_topMessageCopy)
        m_topMessageCopy->show();
    if (m_topMessageActionOutput) {
        const bool canOpenOutput = error && actionRunId > 0 &&
                                   findRun(actionRunId) != nullptr;
        m_topMessageActionOutput->setVisible(canOpenOutput);
    }
    if (m_topMessageSendToPrompt) {
        setTopMessageAction(m_topMessageSendToPrompt, m_topMessageAgentSessionId);
        m_topMessageSendToPrompt->show();
    }
    if (m_topMessageClose)
        m_topMessageClose->show();
    renderTopMessageCountdown();
    if (m_topMessageActions)
        m_topMessageActions->show();

    // A previous bubble may have been mid-slide. Start just below the final dock
    // and glide up once its text and actions have been measured. This also makes
    // the next card in a burst rise smoothly after the card above it leaves.
    animateTopMessageEntry(topMessageBubbleRect());
    if (!m_topMessageEntering)
        m_topMessageTimer->start(1000);
}

// Repaint the dim countdown line on the action row under the message. It used to
// be appended to the message text itself, which cost the message the very room it
// needed to be readable; on its own row it can never crowd it out.
void MainWindow::renderTopMessageCountdown()
{
    if (!m_topMessageMeta)
        return;
    if (m_topMessageSlidingOut)
        return; // it already dropped its countdown and is on its way off-screen
    QStringList parts;
    if (m_topMessageSecondsLeft > 0)
        parts << QStringLiteral("%1s").arg(m_topMessageSecondsLeft);
    // Keep the count alongside the visible stack so a departing toast makes the
    // queue depth clear, even when the stack has to scroll for a large burst.
    if (!m_topMessageQueue.isEmpty())
        parts << QStringLiteral("+%1 more").arg(m_topMessageQueue.size());
    if (m_topMessageHovering)
        parts << QStringLiteral("paused");
    // "·" is a byte-escaped glyph, so it must go through fromUtf8 (QStringLiteral
    // would mangle the multibyte sequence).
    m_topMessageMeta->setText(parts.join(QString::fromUtf8(" \xC2\xB7 ")));
}

// Hide the top toast and its action row (countdown / Copy / dismiss). This
// is a hard reset: any errors still waiting behind the current one are dropped
// too (their full text remains in the network log regardless).
void MainWindow::dismissTopMessage()
{
    m_loadStatusShowing = false;
    m_topMessageHovering = false;
    m_topMessageIsPromptBubble = false;
    m_topMessagePromptStatus.clear();
    m_topMessagePromptImagePaths.clear();
    if (m_topMessagePromptHeader)
        m_topMessagePromptHeader->hide();
    if (m_topMessagePromptStatusLabel)
        m_topMessagePromptStatusLabel->hide();
    if (m_topMessageAgentRow)
        m_topMessageAgentRow->hide();
    m_topMessageAvatarPixmap = QPixmap();
    if (m_topMessageAvatar) {
        m_topMessageAvatar->clear();
        m_topMessageAvatar->hide();
    }
    renderTopMessagePromptImages();
    m_topMessageHref.clear(); // the next toast opts back in to clickability if it wants it
    m_topMessageKind.clear();
    m_topMessageAgentSessionId = -1;
    m_topMessageQueue.clear();
    renderTopMessageQueue();
    if (m_topMessageTimer)
        m_topMessageTimer->stop(); // don't keep ticking the countdown on a hidden toast
    if (m_topMessageFlight)
        m_topMessageFlight->stop();
    if (m_topMessageQueueFlight)
        m_topMessageQueueFlight->stop();
    m_topMessageSlidingOut = false;
    m_topMessageEntering = false;
    m_topMessageShifting = false;
    if (m_topMessage)
        m_topMessage->hide();
    if (m_topMessageContainer)
        m_topMessageContainer->hide();
    if (m_topMessageQueueScroll)
        m_topMessageQueueScroll->hide();
    if (m_topMessageMeta)
        m_topMessageMeta->clear();
    if (m_topMessageActions)
        m_topMessageActions->hide();
    if (m_topMessageCopy)
        m_topMessageCopy->hide();
    if (m_topMessageSendToPrompt)
        m_topMessageSendToPrompt->hide();
    m_topMessageActionRunId = -1;
    if (m_topMessageActionOutput)
        m_topMessageActionOutput->hide();
    if (m_topMessageTypeBadge)
        m_topMessageTypeBadge->hide();
    if (m_topMessageClose)
        m_topMessageClose->hide();
}

// Show the next queued message (its own full countdown, per flashMessage), or
// fully dismiss the toast if nothing is waiting. Called once the current toast
// has finished sliding out, or when the user dismisses it early.
void MainWindow::advanceTopMessageQueue()
{
    m_topMessageSlidingOut = false;
    if (m_topMessageQueue.isEmpty()) {
        dismissTopMessage();
        return;
    }
    const TopMessageQueueEntry next = m_topMessageQueue.takeFirst();
    renderTopMessageQueue();
    // Hide first: showTopMessage would otherwise see a toast that is still
    // visible and queue this one straight back behind itself.
    if (m_topMessage)
        m_topMessage->hide();
    if (m_topMessageTimer)
        m_topMessageTimer->stop();
    // Present only — this card was already logged (and, if it was an error,
    // already flashed the window) when it first arrived. It reaches the top
    // wearing the face it queued with, restored the same way the raising path
    // sets it (adhoc #1612).
    const QPixmap previousPendingAvatar = m_pendingToastAvatar;
    m_pendingToastAvatar = next.avatar;
    showTopMessage(next.text, next.error, next.clickHref, next.durationSeconds,
                   next.kind, next.actionRunId);
    m_pendingToastAvatar = previousPendingAvatar;
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
