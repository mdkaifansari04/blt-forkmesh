// MainWindowChat: MainWindow feature methods, split out of MainWindow.cpp.
// Peer chat: the server rail, favicons, and the chat page (messages, rooms, DMs).
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "KebabHeaderView.h"
#include "RepoSecurity.h"
#include "ScreenCaptureOverlay.h"
#include "ScreenDrawOverlay.h"
#include "ScreenshotMarkupWindow.h"

#include <QNetworkInformation>

using namespace forkmesh::ui;

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
                                 {"room", server.room}});
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
    }
}

void MainWindow::loadActiveServerIntoEdits()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    const ServerConfig &active = m_servers.at(m_activeServer);
    if (m_serverUrlEdit)
        m_serverUrlEdit->setText(serverHostDisplay(active.url)); // show host only
    if (m_roomNameEdit)
        m_roomNameEdit->setText(active.room);
}

void MainWindow::persistEditsToActiveServer()
{
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        return;
    ServerConfig &active = m_servers[m_activeServer];
    active.url = canonicalServerUrl(m_serverUrlEdit->text()); // host -> full URL
    active.room = m_roomNameEdit->text().trimmed();
    saveServers();
}

QPixmap MainWindow::faviconFor(const ServerConfig &server) const
{
    const QString host = serverHost(server.url);
    if (m_faviconCache.contains(host))
        return roundedRectPixmap(m_faviconCache.value(host), 36, 9);
    return letterFavicon(host); // already drawn as a rounded rect
}

void MainWindow::switchToServer(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    const bool live = m_backend != nullptr;
    if (index == m_activeServer && live) {
        showSection(0); // already connected here: just jump to its Home
        return;
    }

    if (live)
        persistEditsToActiveServer(); // capture any edits to the current server
    m_activeServer = index;
    saveServers();
    loadActiveServerIntoEdits();
    updateBreadcrumb();
    startSession(); // tears down the old backend and connects to the new server
}

void MainWindow::promptAddServer()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Add mainnode server");
    auto *urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(kDefaultServerUrl);
    // Room is fixed network-wide; only the relay URL is configurable.
    auto *roomEdit = new QLineEdit(kDefaultRoomName, &dialog);
    roomEdit->setReadOnly(true);

    auto *form = new QFormLayout;
    form->addRow("Server URL", urlEdit);
    form->addRow("Room", roomEdit);
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
    server.room = kDefaultRoomName;
    m_servers.append(server);
    const int newIndex = m_servers.size() - 1;
    saveServers();
    updateBreadcrumb();
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
    updateBreadcrumb();
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
        updateBreadcrumb();
    });
}

// ----------------------------------------------------------------- chat page

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;

    // One page per "place": Home holds the repos, quest board and chat all at
    // once (no nav bar — you click a server to see everything). Repo detail and
    // Settings are opened on demand (clicking a repo / the server-rail gear).
    m_sectionStack = new QStackedWidget;
    // Home now hosts the nodes column, repositories column and the repo detail
    // panel (with Chat as a tab) all at once, so there is no separate repo-detail
    // section any more.
    m_sectionStack->addWidget(buildHomeSection());       // 0 Home (nodes + repos + detail)
    logStartup(QStringLiteral("  buildChatPage: home section built"));
    auto *settingsScroll = new QScrollArea;
    settingsScroll->setObjectName("settingsScroll");
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->setWidgetResizable(true);
    // Settings content can be tall; keep it out of the section stack minimum.
    settingsScroll->setMinimumHeight(0);
    settingsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    settingsScroll->setWidget(buildSettingsSection());
    logStartup(QStringLiteral("  buildChatPage: settings section built"));
    m_sectionStack->addWidget(settingsScroll);           // 1 Settings
    // Chat is its own top-level place now (no longer a page buried in the repo
    // detail stack), so it survives switching between repos and stays reachable
    // from the always-visible nav.
    m_sectionStack->addWidget(buildChatSection());       // 2 Chat
    logStartup(QStringLiteral("  buildChatPage: chat section built"));
    m_sectionStack->addWidget(buildNotificationsSection()); // 3 Notifications
    logStartup(QStringLiteral("  buildChatPage: notifications section built"));
    m_sectionStack->addWidget(buildLogSection());        // 4 Log
    logStartup(QStringLiteral("  buildChatPage: log section built"));
    m_sectionStack->addWidget(buildLeaderboardsSection()); // 5 Leaderboards
    logStartup(QStringLiteral("  buildChatPage: leaderboards section built"));
    m_sectionStack->addWidget(buildSearchResultsSection()); // 6 Search results
    logStartup(QStringLiteral("  buildChatPage: search section built"));
    m_sectionStack->addWidget(buildHostsSection());      // 7 Hosts (adhoc #263)
    logStartup(QStringLiteral("  buildChatPage: hosts section built"));
    m_sectionStack->addWidget(buildRelaysSection());     // 8 Relays
    logStartup(QStringLiteral("  buildChatPage: relays section built"));
    m_sectionStack->addWidget(buildNodeProfileSection()); // 9 Node profile (full page)
    logStartup(QStringLiteral("  buildChatPage: node profile section built"));

    // No left rails any more: relays and nodes are top-bar dropdowns, so the
    // section fills the whole width.
    auto *content = new QWidget;
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_sectionStack, 1);

    // Global donation nudge: shown across the whole app until this node sets a
    // Solana address, so the network stays open to donations.
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildBreadcrumb());
    layout->addWidget(buildSolanaNotice());
    layout->addWidget(buildWalletVerifyNotice());
    auto *contentScroll = new QScrollArea;
    contentScroll->setWidgetResizable(true);
    contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentScroll->setFrameShape(QFrame::NoFrame);
    // Tall tab pages should scroll instead of becoming the window's minimum height.
    contentScroll->setMinimumHeight(0);
    contentScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    contentScroll->setWidget(content);
    layout->addWidget(contentScroll, 1);
    layout->addWidget(buildNetworkLogDock());
    return page;
}

// How many lines of prior history to seed the always-on footer log with on
// startup. Bounded well below kNetworkLogLimit so the corner widget (unlike the
// full Log tab, which defers its own render until first visit) stays cheap to
// populate on every launch while still giving a real scrollback to search.
constexpr int kFooterLogSeedLines = 300;

QWidget *MainWindow::buildNetworkLogDock()
{
    // Full-width, grey-bordered quick-add bar: the issue input expands on the
    // left, then a flexible gap pushes the donate/Reddit/X cluster to the far
    // right.
    auto *dock = new QWidget;
    dock->setObjectName("logDock");

    auto *card = new QWidget;
    card->setObjectName("quickAddCard");

    // A two-line wrapping box (adhoc #12), not a single-line edit, so the typed
    // prompt is actually visible on two lines. Enter sends / Shift+Enter adds a
    // newline (handled in the event filter); Up/Down still walk prompt history.
    m_issueQuickAdd = new QPlainTextEdit;
    m_issueQuickAdd->setObjectName("issueQuickAdd");
    m_issueQuickAdd->setPlaceholderText("enter prompt");
    m_issueQuickAdd->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_issueQuickAdd->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_issueQuickAdd->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // In "No issue" mode the typed text becomes a Claude agent's prompt, so the
    // field is capped at the same length as the Claude prompt / message input
    // (kMaxTextChars). QPlainTextEdit has no setMaxLength, so the cap is enforced
    // in the textChanged handler below.
    const int kQuickAddMaxChars = 16000;
    // Ctrl+V with an image on the clipboard attaches it (issue #79).
    m_issueQuickAdd->installEventFilter(this);
    // Restore the prompt history persisted from earlier sessions so Up recalls
    // prompts sent before the app was last closed (adhoc #200).
    m_quickAddHistory = QSettings().value(kQuickAddHistorySetting).toStringList();

    // Characters-remaining counter: counts down from the field's limit as you
    // type, so it's clear how much room is left before the field stops accepting
    // input. Greys out when empty, turns amber as the limit approaches.
    m_quickAddCharCount = new QLabel;
    m_quickAddCharCount->setObjectName("quickAddCharCount");
    m_quickAddCharCount->setToolTip("Characters remaining in the quick-add title");
    // Fixed width + right alignment so the count (1–5 digits) never changes the
    // label's footprint as you type — otherwise the expanding prompt field next to
    // it visibly jolts each time the digit count changes.
    m_quickAddCharCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_quickAddCharCount->setFixedWidth(
        m_quickAddCharCount->fontMetrics().horizontalAdvance(
            QString::number(kQuickAddMaxChars)) +
        6);
    auto updateQuickAddCharCount = [this, kQuickAddMaxChars]() {
        const int remaining =
            kQuickAddMaxChars - m_issueQuickAdd->toPlainText().length();
        m_quickAddCharCount->setText(QString::number(remaining));
        m_quickAddCharCount->setStyleSheet(QStringLiteral(
            "QLabel#quickAddCharCount{color:%1;font-size:11px;}")
                .arg(remaining <= 20 ? QStringLiteral("#d29922")
                                     : QStringLiteral("#8b949e")));
    };
    connect(m_issueQuickAdd, &QPlainTextEdit::textChanged, this,
            [this, updateQuickAddCharCount, kQuickAddMaxChars] {
                // Enforce the prompt-length cap QPlainTextEdit can't do itself:
                // if a paste pushes past the limit, trim back to it.
                const QString text = m_issueQuickAdd->toPlainText();
                if (text.length() > kQuickAddMaxChars) {
                    const QSignalBlocker block(m_issueQuickAdd);
                    m_issueQuickAdd->setPlainText(text.left(kQuickAddMaxChars));
                    m_issueQuickAdd->moveCursor(QTextCursor::End);
                }
                updateQuickAddCharCount();
                // Typing anything by hand drops out of history navigation, so the
                // next Up starts again from the most recent prompt (adhoc #200).
                // The flag skips the programmatic setPlainText() the history walk
                // does, which would otherwise look like a manual edit.
                if (!m_quickAddHistoryNavigating)
                    m_quickAddHistoryIndex = -1;
            });
    updateQuickAddCharCount();

    m_quickAddAssignAgent = new QCheckBox("Agent");
    m_quickAddAssignAgent->setObjectName("quickAddAgentCheck");
    m_quickAddAssignAgent->setToolTip(
        "When you add the issue, immediately assign a coding agent to it.");
    m_quickAddAgentProvider = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddAgentProvider->setObjectName("quickAddAgentSelector");
    m_quickAddAgentProvider->addItem(QStringLiteral("OpenAI API"),
                                     QStringLiteral("openai"));
    m_quickAddAgentProvider->addItem(QStringLiteral("Claude API"),
                                     QStringLiteral("claude-api"));
    // "Claude Code" drives the real `claude` CLI headlessly (no input) in a
    // tracked agent session, working until ForkMesh can open a PR from its diff.
    m_quickAddAgentProvider->addItem(QStringLiteral("Claude Code"),
                                     QStringLiteral("claude-code"));
    selectDefaultAgentProvider(m_quickAddAgentProvider);
    m_quickAddAgentProvider->setToolTip("Agent provider for quick-add assignment");
    // Show the whole list at once rather than a scrollable popup (adhoc #99).
    m_quickAddAgentProvider->setMaxVisibleItems(30);
    // Claude model chooser (adhoc #261): live list of models from the provider.
    // Populated by refreshClaudeModelCombo; choice persisted and fed to
    // startClaudeCodeTranscript.
    m_quickAddClaudeModel = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddClaudeModel->setObjectName("quickAddModelSelector");
    m_quickAddClaudeModel->setMinimumWidth(170);
    m_quickAddClaudeModel->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // Show the whole model list at once rather than a scrollable popup, even
    // once the live provider list-up fills in more than a handful (adhoc #99).
    m_quickAddClaudeModel->setMaxVisibleItems(30);
    populateClaudeModelCombo(m_quickAddClaudeModel);
    m_quickAddClaudeModel->setToolTip(
        "Claude model the Claude Code agent runs as (passed to the CLI as --model).");
    m_quickAddClaudeModel->view()->installEventFilter(this);
    m_quickAddClaudeModel->setProperty("claudeModelCombo", true);
    connect(m_quickAddClaudeModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                QSettings().setValue(kClaudeCodeModelSetting,
                                     m_quickAddClaudeModel->currentData().toString());
            });
    refreshClaudeModelCombo();
    // Mode selector (issue #348): a dropdown in the same style as the
    // provider/model pickers, mirroring the Claude Code CLI's own permission-mode
    // picker (Ask before edits / Edit automatically / Plan mode / Auto mode).
    // Backed by the same kClaudeAutoModeSetting the agent composer's "Auto mode /
    // Manual approve" toggle already uses: only "Auto mode" skips permission
    // prompts today, so the other three all mean "don't skip" until this app can
    // drive per-tool approval headlessly.
    m_quickAddModeSelector = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddModeSelector->setObjectName("quickAddModeSelector");
    m_quickAddModeSelector->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_quickAddModeSelector->addItem(QStringLiteral("Ask before edits"), false);
    m_quickAddModeSelector->addItem(QStringLiteral("Edit automatically"), false);
    m_quickAddModeSelector->addItem(QStringLiteral("Plan mode"), false);
    m_quickAddModeSelector->addItem(QStringLiteral("Auto mode"), true);
    m_quickAddModeSelector->setMaxVisibleItems(30);
    m_quickAddModeSelector->setToolTip(
        "How much freedom the agent has to make changes without asking first.");
    {
        const int idx = m_quickAddModeSelector->findData(
            QSettings().value(kClaudeAutoModeSetting, true).toBool());
        m_quickAddModeSelector->setCurrentIndex(
            idx >= 0 ? idx : m_quickAddModeSelector->count() - 1);
    }
    connect(m_quickAddModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                QSettings().setValue(kClaudeAutoModeSetting,
                                     m_quickAddModeSelector->currentData().toBool());
            });
    m_quickAddCreatePr = new QCheckBox("Create PR");
    m_quickAddCreatePr->setToolTip(
        "When quick-add assigns an agent, create a pull request from its patch.");
    // Not shown in the controls row (kept out of the prompt-box chrome); it stays
    // wired up and defaults to checked so quick-add agents still open a PR.
    m_quickAddCreatePr->setVisible(false);
    // "Create issue" toggle (adhoc #99): off by default and remembered across
    // launches, since the common quick-add path fires a coding agent straight
    // from the typed prompt rather than filing an issue first.
    m_quickAddCreateIssue = new QCheckBox("Create issue");
    m_quickAddCreateIssue->setObjectName("quickAddCreateIssueCheck");
    m_quickAddCreateIssue->setToolTip(
        "Create an issue for this prompt instead of starting an agent "
        "straight from it.");
    // Attach an image to the quick-add (issue #79): pick a file or paste with
    // Ctrl+V. In "No issue" mode the image path rides along in the agent's prompt;
    // otherwise it's attached to the created issue.
    m_quickAddImageButton = new QPushButton;
    m_quickAddImageButton->setObjectName("ghostButton");
    m_quickAddImageButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddImageButton, "paperclip", 16);
    connect(m_quickAddImageButton, &QPushButton::clicked, this,
            &MainWindow::attachQuickAddImage);
    // Strip of attachment chips beside the paperclip: each shows a thumbnail and a
    // little "x" to remove that one image. Hidden until something is attached.
    m_quickAddAttachStrip = new QWidget;
    m_quickAddAttachStrip->setObjectName("quickAddAttachStrip");
    auto *attachStripRow = new QHBoxLayout(m_quickAddAttachStrip);
    attachStripRow->setContentsMargins(0, 0, 0, 0);
    attachStripRow->setSpacing(4);
    m_quickAddAttachStrip->setVisible(false);
    updateQuickAddImageButton();

    // Mic: dictate the prompt with the locally-installed whisper.cpp. Hidden
    // until whisper.cpp is downloaded from Settings (updateVoiceInputButton()).
    m_quickAddMicButton = new QPushButton;
    m_quickAddMicButton->setObjectName("ghostButton");
    m_quickAddMicButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddMicButton, "mic", 16);
    // Push-to-talk: hold the button to record, release to stop and transcribe.
    connect(m_quickAddMicButton, &QPushButton::pressed, this,
            &MainWindow::startVoiceCapture);
    connect(m_quickAddMicButton, &QPushButton::released, this,
            &MainWindow::stopVoiceCapture);
    // Live input-level meter (adhoc #10): a thin bar beside the mic that fills
    // with the incoming audio level while recording, so you can see the mic is
    // actually picking you up. Hidden until recording starts.
    m_voiceLevelMeter = new QProgressBar;
    m_voiceLevelMeter->setObjectName("voiceLevelMeter");
    m_voiceLevelMeter->setRange(0, 100);
    m_voiceLevelMeter->setValue(0);
    m_voiceLevelMeter->setTextVisible(false);
    m_voiceLevelMeter->setFixedSize(48, 12);
    m_voiceLevelMeter->setToolTip("Live microphone input level");
    m_voiceLevelMeter->setStyleSheet(
        "QProgressBar#voiceLevelMeter{border:1px solid #30363d;border-radius:3px;"
        "background:#0d1117;}"
        "QProgressBar#voiceLevelMeter::chunk{background:#3fb950;border-radius:2px;}");
    m_voiceLevelMeter->setVisible(false);
    // Auto-send toggle beside the mic (adhoc #45): when checked, the prompt is sent
    // (same as Enter/Send) the moment a voice dictation finishes its final
    // transcription, so you can dictate-and-go hands-free. Persisted across launches.
    m_quickAddVoiceAutoSubmit = new QCheckBox("Auto");
    m_quickAddVoiceAutoSubmit->setObjectName("quickAddAutoCheck");
    m_quickAddVoiceAutoSubmit->setToolTip(
        "Automatically send the prompt when voice dictation finishes transcribing.");
    m_quickAddVoiceAutoSubmit->setChecked(
        QSettings().value(kVoiceAutoSubmitSetting, false).toBool());
    connect(m_quickAddVoiceAutoSubmit, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kVoiceAutoSubmitSetting, on);
    });
    // Restore the remembered "Create issue" state (off the first time a profile
    // runs it, per adhoc #99).
    m_quickAddCreateIssue->setChecked(
        QSettings().value(kQuickAddCreateIssueSetting, false).toBool());
    connect(m_quickAddCreateIssue, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(kQuickAddCreateIssueSetting, on);
    });
    m_quickAddAssignAgent->setChecked(true);
    m_quickAddCreatePr->setChecked(true);
    m_quickAddCreatePr->setEnabled(true);
    m_quickAddAgentProvider->setEnabled(true);
    // The provider/PR controls are live whenever an agent will run: either the
    // user asked to assign one, or "No issue" mode (which always starts one —
    // i.e. "Create issue" is off). In "No issue" mode the plain "Agent" toggle
    // is irrelevant, so disable it.
    auto syncQuickAddAgentControls = [this]() {
        const bool noIssue = !m_quickAddCreateIssue->isChecked();
        m_quickAddAssignAgent->setEnabled(!noIssue);
        const bool agentRuns = noIssue || m_quickAddAssignAgent->isChecked();
        m_quickAddAgentProvider->setEnabled(agentRuns);
        m_quickAddCreatePr->setEnabled(agentRuns);
        // The model chooser only applies to the Claude Code CLI, so hide it for
        // the API providers and grey it out when no agent will run (adhoc #261).
        const bool claudeCode = m_quickAddAgentProvider->currentData().toString() ==
                                QLatin1String("claude-code");
        m_quickAddClaudeModel->setVisible(claudeCode);
        m_quickAddClaudeModel->setEnabled(agentRuns);
        // The permission-mode chooser only means anything for the Claude Code
        // CLI too (issue #348) — the API providers have no such concept.
        m_quickAddModeSelector->setVisible(claudeCode);
        m_quickAddModeSelector->setEnabled(agentRuns);
    };
    connect(m_quickAddAssignAgent, &QCheckBox::toggled, this,
            [syncQuickAddAgentControls](bool) { syncQuickAddAgentControls(); });
    connect(m_quickAddCreateIssue, &QCheckBox::toggled, this,
            [syncQuickAddAgentControls](bool) { syncQuickAddAgentControls(); });
    connect(m_quickAddAgentProvider, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [syncQuickAddAgentControls](int) { syncQuickAddAgentControls(); });
    syncQuickAddAgentControls();

    // Slash-actions button (adhoc #116): a small bordered "/" box, like the
    // Claude Code extension's, that opens the filterable actions popup —
    // Context/Model quick actions plus the CLI's own slash commands, pulled
    // live from `claude` the first time the popup opens.
    m_quickAddSlashButton = new QPushButton(QStringLiteral("/"));
    m_quickAddSlashButton->setObjectName("quickAddSlashButton");
    m_quickAddSlashButton->setCursor(Qt::PointingHandCursor);
    m_quickAddSlashButton->setFixedSize(22, 22);
    m_quickAddSlashButton->setToolTip(
        "Commands and quick actions (pulled live from Claude Code)");
    connect(m_quickAddSlashButton, &QPushButton::clicked, this,
            &MainWindow::openQuickAddSlashActions);

    // Icon-only send button inside the prompt frame (paper airplane = send/submit).
    auto *quickAddSendButton = new QPushButton;
    quickAddSendButton->setObjectName("quickAddSendIcon");
    quickAddSendButton->setCursor(Qt::PointingHandCursor);
    setOcticon(quickAddSendButton, "paper-airplane", 16);
    quickAddSendButton->setFixedSize(28, 28);
    quickAddSendButton->setToolTip("Send (Enter)");
    connect(quickAddSendButton, &QPushButton::clicked, this,
            &MainWindow::quickAddIssue);

    // Second paper airplane, rotated to point straight up, stacked above the
    // regular send icon (adhoc #99): sends the typed prompt as a follow-up
    // message to the agent session currently open above, instead of the
    // quick-add issue/new-agent flow.
    m_quickAddSendToAgentButton = new QPushButton;
    m_quickAddSendToAgentButton->setObjectName("quickAddSendIcon");
    m_quickAddSendToAgentButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddSendToAgentButton, "paper-airplane", 16, -45.0);
    m_quickAddSendToAgentButton->setFixedSize(28, 28);
    m_quickAddSendToAgentButton->setToolTip(
        "Send to the agent open above, as a follow-up message");
    connect(m_quickAddSendToAgentButton, &QPushButton::clicked, this, [this] {
        if (!m_issueQuickAdd)
            return;
        const QString prompt = m_issueQuickAdd->toPlainText().trimmed();
        if (m_selectedAgentSessionId < 0) {
            logSystem(QStringLiteral(
                "No agent open above to send that to \xE2\x80\x94 open one first."));
            return;
        }
        if (prompt.isEmpty()) {
            // No text typed: just resume the open session with the same agent,
            // the same thing the old per-session Continue button did (adhoc #178).
            continueSelectedAgentSession();
            return;
        }
        recordQuickAddHistory(prompt);
        m_issueQuickAdd->clear();
        sendPromptToSelectedAgent(prompt);
    });

    m_issueQuickAdd->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Two send icons stacked in a column at the prompt's bottom-right corner.
    auto *sendColumn = new QVBoxLayout;
    sendColumn->setContentsMargins(0, 0, 0, 0);
    sendColumn->setSpacing(2);
    sendColumn->addWidget(m_quickAddSendToAgentButton);
    sendColumn->addWidget(quickAddSendButton);

    // Agent hand-off controls (adhoc #99): the "Agent" toggle plus the
    // provider/model/mode dropdowns it governs, grouped as one unit in the
    // middle of the bottom bar. No border/frame around them any more (adhoc
    // #111 removed the pill outline) — they just sit inline in the bar.
    auto *agentBox = new QWidget;
    agentBox->setObjectName("quickAddAgentBox");
    auto *agentBoxRow = new QHBoxLayout(agentBox);
    agentBoxRow->setContentsMargins(6, 1, 4, 1);
    agentBoxRow->setSpacing(2);
    agentBoxRow->addWidget(m_quickAddAssignAgent);
    agentBoxRow->addWidget(m_quickAddAgentProvider);
    agentBoxRow->addWidget(m_quickAddClaudeModel);
    agentBoxRow->addWidget(m_quickAddModeSelector);

    // Bottom bar nested inside the prompt frame, below the text area (adhoc
    // #99): paperclip and mic at the bottom-left (opposite the send icons),
    // the Auto/Create-issue toggles, the Agent box centred by the stretches on
    // either side, then the character count immediately left of the send icons.
    // No bottom margin (adhoc #111) so the row sits flush against the bottom
    // edge of the prompt frame instead of leaving a gap under it.
    // Every widget is bottom-aligned (adhoc #114): the send column is two
    // stacked 28px icons and taller than the rest of the row, so without an
    // explicit alignment Qt centres the shorter controls in that extra height
    // and they read as floating above the send icons instead of level with
    // them.
    auto *bottomBar = new QHBoxLayout;
    bottomBar->setContentsMargins(8, 6, 6, 0);
    bottomBar->setSpacing(6);
    bottomBar->addWidget(m_quickAddImageButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddMicButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_voiceLevelMeter, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddAttachStrip, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddVoiceAutoSubmit, 0, Qt::AlignBottom);
    bottomBar->addSpacing(14);
    bottomBar->addWidget(m_quickAddCreateIssue, 0, Qt::AlignBottom);
    bottomBar->addStretch(1);
    // The "/" actions box sits immediately left of the Agent checkbox (adhoc
    // #116), matching where the Claude Code extension keeps its actions menu.
    bottomBar->addWidget(m_quickAddSlashButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(agentBox, 0, Qt::AlignBottom);
    bottomBar->addStretch(1);
    bottomBar->addWidget(m_quickAddCharCount, 0, Qt::AlignBottom);
    bottomBar->addLayout(sendColumn);

    // Prompt wrapper: the border lives on this frame; the text edit sits on
    // top with the bottom bar nested below it inside the same box, so the
    // controls read as an overlay along the foot of the prompt input rather
    // than a separate strip above it.
    auto *promptWrapper = new QFrame;
    promptWrapper->setObjectName("promptWrapper");
    auto *promptLayout = new QVBoxLayout(promptWrapper);
    promptLayout->setContentsMargins(0, 0, 0, 0);
    promptLayout->setSpacing(0);
    promptLayout->addWidget(m_issueQuickAdd);
    promptLayout->addLayout(bottomBar);

    // "Agents:" status strip above the prompt input (adhoc #111): a clickable
    // label plus one small colored dot per known agent session — a status
    // dashboard at a glance. The label jumps to the most relevant session's
    // Agents tab; each dot jumps straight to that one. Populated by
    // refreshAgentStatusRow() (called from reloadAgents()), hidden until there
    // is at least one session to show.
    m_agentStatusLabel = new QPushButton("Agents:");
    m_agentStatusLabel->setObjectName("agentStatusLabel");
    m_agentStatusLabel->setFlat(true);
    m_agentStatusLabel->setCursor(Qt::PointingHandCursor);
    m_agentStatusLabel->setToolTip("Open the Agents tab");
    connect(m_agentStatusLabel, &QPushButton::clicked, this,
            &MainWindow::openAgentsOverview);

    m_agentStatusIconsHost = new QWidget;
    m_agentStatusIconsLayout = new QHBoxLayout(m_agentStatusIconsHost);
    m_agentStatusIconsLayout->setContentsMargins(0, 0, 0, 0);
    m_agentStatusIconsLayout->setSpacing(4);
    m_agentStatusIconsLayout->addStretch(1);

    auto *agentStatusScroll = new QScrollArea;
    agentStatusScroll->setObjectName("agentStatusScroll");
    agentStatusScroll->setWidget(m_agentStatusIconsHost);
    agentStatusScroll->setWidgetResizable(true);
    agentStatusScroll->setFrameShape(QFrame::NoFrame);
    agentStatusScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    agentStatusScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    agentStatusScroll->setFixedHeight(24);

    m_agentStatusRow = new QWidget;
    m_agentStatusRow->setObjectName("agentStatusRow");
    auto *agentStatusRowLayout = new QHBoxLayout(m_agentStatusRow);
    agentStatusRowLayout->setContentsMargins(2, 0, 2, 6);
    agentStatusRowLayout->setSpacing(6);
    agentStatusRowLayout->addWidget(m_agentStatusLabel);
    agentStatusRowLayout->addWidget(agentStatusScroll, 1);
    // Small "fix conflicts with agent" icon button (adhoc #139): built earlier
    // by buildAgentsTab() (called from buildHomeSection(), which runs before
    // this dock in buildChatPage()); it stays hidden until the selected
    // session's branch is flagged as conflicted (see showAgentSession()).
    if (m_agentFixConflictsButton)
        agentStatusRowLayout->addWidget(m_agentFixConflictsButton);
    m_agentStatusRow->setVisible(false); // shown once refreshAgentStatusRow() finds sessions

    // Card (right half): the "Agents:" strip on top of the prompt frame, whose
    // controls live inside it as the bottom bar.
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 8, 12, 8);
    cardLayout->setSpacing(4);
    cardLayout->addWidget(m_agentStatusRow);
    cardLayout->addWidget(promptWrapper);

    // A scrollable strip below the quick-add bar: the always-on live log. It
    // fills as much height as the dock row allows (matching the prompt card
    // beside it) and streams every network/update line, oldest at top, newest
    // at bottom — the scrollbar lets you scroll back through history to search
    // it instead of only ever seeing the latest line (adhoc #211).
    m_footerUpdateLog = new QPlainTextEdit;
    m_footerUpdateLog->setObjectName("footerUpdateLog");
    m_footerUpdateLog->setReadOnly(true);
    m_footerUpdateLog->setFrameShape(QFrame::NoFrame);
    m_footerUpdateLog->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_footerUpdateLog->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_footerUpdateLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_footerUpdateLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_footerUpdateLog->setToolTip(
        "Live log \xE2\x80\x94 scroll up to search back through recent history.");
    // Bound the live buffer the same way the seed below is bounded, so it can't
    // grow without limit over a long-running session.
    m_footerUpdateLog->setMaximumBlockCount(kFooterLogSeedLines);
    styleFooterUpdateLog();
    // Seed the always-on strip with recent history (or a ready placeholder) so
    // it's already scrollable on first paint; logSystem() then streams every new
    // event onto it. Keep the full dated lines so timestamps show.
    if (!m_networkLog.isEmpty()) {
        const int from = qMax(0, m_networkLog.size() - kFooterLogSeedLines);
        QStringList seed;
        seed.reserve(m_networkLog.size() - from);
        for (int i = from; i < m_networkLog.size(); ++i)
            seed << m_networkLog.at(i);
        m_footerUpdateLog->setPlainText(seed.join(QLatin1Char('\n')));
        m_footerUpdateLog->verticalScrollBar()->setValue(
            m_footerUpdateLog->verticalScrollBar()->maximum());
    } else {
        m_footerUpdateLog->setPlainText(QStringLiteral("ForkMesh ready"));
    }

    // Horizontal split: live-log strip on the left half, prompt card on the right.
    auto *dockRow = new QHBoxLayout(dock);
    dockRow->setContentsMargins(0, 0, 0, 0);
    dockRow->setSpacing(0);
    dockRow->addWidget(m_footerUpdateLog, 1);
    dockRow->addWidget(card, 1);

    // Enter sends (Shift+Enter inserts a newline) — handled in the event filter
    // since QPlainTextEdit has no returnPressed signal.
    updateVoiceInputButton();
    return dock;
}

// Footer slash-actions popup (adhoc #116): opened by the "/" box left of the
// Agent checkbox. Mirrors the Claude Code extension's own actions menu — a
// filter box over fixed Context/Model rows plus the CLI's own slash commands
// (fetched live the first time the popup opens, see refreshClaudeSlashCommands).
void MainWindow::openQuickAddSlashActions()
{
    if (!m_quickAddSlashButton || !m_issueQuickAdd)
        return;
    if (!m_slashActionsPopup) {
        m_slashActionsPopup = new QFrame(this);
        m_slashActionsPopup->setObjectName("slashActionsPopup");
        m_slashActionsPopup->setWindowFlags(Qt::Popup);
        m_slashActionsPopup->setFixedWidth(340);

        auto *popupLayout = new QVBoxLayout(m_slashActionsPopup);
        popupLayout->setContentsMargins(0, 0, 0, 0);
        popupLayout->setSpacing(0);

        m_slashActionsFilter = new QLineEdit;
        m_slashActionsFilter->setObjectName("slashActionsFilter");
        m_slashActionsFilter->setPlaceholderText("Filter actions\xE2\x80\xA6");
        m_slashActionsFilter->installEventFilter(this);
        connect(m_slashActionsFilter, &QLineEdit::textChanged, this,
                &MainWindow::populateSlashActionsList);
        popupLayout->addWidget(m_slashActionsFilter);

        m_slashActionsListHost = new QWidget;
        m_slashActionsListLayout = new QVBoxLayout(m_slashActionsListHost);
        m_slashActionsListLayout->setContentsMargins(0, 6, 0, 6);
        m_slashActionsListLayout->setSpacing(0);

        m_slashActionsScroll = new QScrollArea;
        m_slashActionsScroll->setObjectName("slashActionsScroll");
        m_slashActionsScroll->setWidget(m_slashActionsListHost);
        m_slashActionsScroll->setWidgetResizable(true);
        m_slashActionsScroll->setFrameShape(QFrame::NoFrame);
        m_slashActionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_slashActionsScroll->setFixedHeight(360);
        popupLayout->addWidget(m_slashActionsScroll);
    }

    {
        const QSignalBlocker block(m_slashActionsFilter);
        m_slashActionsFilter->clear();
    }
    populateSlashActionsList();
    // Fetches once per app run; a no-op if already loaded or a probe is in
    // flight. Repopulates the list in place once the live commands land.
    refreshClaudeSlashCommands();

    m_slashActionsPopup->adjustSize();
    const QPoint above = m_quickAddSlashButton->mapToGlobal(
        QPoint(0, -m_slashActionsPopup->sizeHint().height() - 4));
    m_slashActionsPopup->move(above);
    m_slashActionsPopup->show();
    m_slashActionsFilter->setFocus();
}

// Rebuilds the popup's row list from the current filter text. Called on open
// and on every filter-box keystroke, and again whenever a toggle/effort row is
// clicked so its new state is reflected immediately.
void MainWindow::populateSlashActionsList()
{
    if (!m_slashActionsListLayout || !m_slashActionsListHost)
        return;
    QLayoutItem *item;
    while ((item = m_slashActionsListLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_slashActionRows.clear();
    m_slashActionSelected = -1;

    const QString filter =
        m_slashActionsFilter ? m_slashActionsFilter->text().trimmed().toLower() : QString();
    auto matches = [&filter](const QString &label, const QString &extra = QString()) {
        return filter.isEmpty() || label.toLower().contains(filter) ||
               extra.toLower().contains(filter);
    };
    auto addHeader = [this](const QString &text) {
        auto *header = new QLabel(text);
        header->setObjectName("slashActionsHeader");
        m_slashActionsListLayout->addWidget(header);
    };
    // A plain row: a title label, an optional right-aligned value label, and a
    // "slashKind"/"slashValue" dynamic-property pair the click/Enter dispatch
    // (activateSlashActionRow) reads generically.
    auto addRow = [this](const QString &label, const QString &rightText,
                         const QString &kind, const QString &value) -> QWidget * {
        auto *row = new QFrame;
        row->setObjectName("slashActionRow");
        row->setCursor(Qt::PointingHandCursor);
        row->setProperty("slashKind", kind);
        row->setProperty("slashValue", value);
        row->installEventFilter(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 7, 12, 7);
        auto *title = new QLabel(label);
        title->setObjectName("slashActionRowLabel");
        // Qt delivers the click to whichever child is directly under the
        // cursor, not the parent frame — without this, clicking the label
        // text itself (rather than the row's bare padding) would miss the
        // "slashKind" property set on `row` and do nothing.
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(title, 1);
        if (!rightText.isEmpty()) {
            auto *right = new QLabel(rightText);
            right->setObjectName("slashActionRowValue");
            right->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(right);
        }
        m_slashActionsListLayout->addWidget(row);
        m_slashActionRows.append(row);
        return row;
    };
    // A toggle row (Thinking / model-fallback): the whole row is one click
    // target that flips the setting and repopulates so the switch redraws.
    auto addToggleRow = [this](const QString &label, const QString &kind, bool checked) {
        auto *row = new QFrame;
        row->setObjectName("slashActionRow");
        row->setCursor(Qt::PointingHandCursor);
        row->setProperty("slashKind", kind);
        row->installEventFilter(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 7, 12, 7);
        auto *title = new QLabel(label);
        title->setObjectName("slashActionRowLabel");
        title->setWordWrap(true);
        title->setAttribute(Qt::WA_TransparentForMouseEvents); // see addRow above
        rowLayout->addWidget(title, 1);
        auto *toggle = new QCheckBox;
        toggle->setObjectName("slashToggle");
        toggle->setChecked(checked);
        toggle->setAttribute(Qt::WA_TransparentForMouseEvents); // the row owns the click
        toggle->setFocusPolicy(Qt::NoFocus);
        rowLayout->addWidget(toggle);
        m_slashActionsListLayout->addWidget(row);
        m_slashActionRows.append(row);
    };

    // --- Context section ---
    struct ContextAction { QString label; QString kind; };
    const ContextAction contextActions[] = {
        {QStringLiteral("Attach file\xE2\x80\xA6"), QStringLiteral("attachFile")},
        {QStringLiteral("Mention file from this project\xE2\x80\xA6"), QStringLiteral("mentionFile")},
        {QStringLiteral("Clear conversation"), QStringLiteral("clearConversation")},
        {QStringLiteral("Rewind"), QStringLiteral("rewind")},
        // adhoc #256: resend the full issue (title + description + every
        // comment) to the agent open above, in case it didn't get the whole
        // thing the first time (e.g. a resumed session only replays a bare
        // "Continue").
        {QStringLiteral("Send issue context to agent"), QStringLiteral("sendIssueContext")},
    };
    bool anyContext = false;
    for (const ContextAction &a : contextActions)
        if (matches(a.label)) { anyContext = true; break; }
    if (anyContext) {
        addHeader(QStringLiteral("Context"));
        for (const ContextAction &a : contextActions)
            if (matches(a.label))
                addRow(a.label, QString(), a.kind, QString());
    }

    // --- Model section ---
    const char *effortLevels[] = {"low", "medium", "high", "xhigh", "max"};
    const char *effortLabels[] = {"Low", "Medium", "High", "Extra high", "Max"};
    const QString currentEffort =
        QSettings().value(kClaudeEffortSetting, QStringLiteral("high")).toString();
    int effortIdx = 2;
    for (int i = 0; i < 5; ++i)
        if (currentEffort == QLatin1String(effortLevels[i]))
            effortIdx = i;
    const bool modelSectionMatches =
        matches(QStringLiteral("Switch model")) || matches(QStringLiteral("Effort")) ||
        matches(QStringLiteral("Thinking")) ||
        matches(QStringLiteral("Switch models when a message is flagged")) ||
        matches(QStringLiteral("Account & usage"));
    if (modelSectionMatches) {
        addHeader(QStringLiteral("Model"));
        if (matches(QStringLiteral("Switch model")))
            addRow(QStringLiteral("Switch model\xE2\x80\xA6"),
                   m_quickAddClaudeModel ? m_quickAddClaudeModel->currentText() : QString(),
                   QStringLiteral("switchModel"), QString());
        if (matches(QStringLiteral("Effort"))) {
            auto *row = new QFrame;
            row->setObjectName("slashActionRow");
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(12, 7, 12, 7);
            auto *title = new QLabel(
                QStringLiteral("Effort (%1)").arg(QString::fromLatin1(effortLabels[effortIdx])));
            title->setObjectName("slashActionRowLabel");
            rowLayout->addWidget(title, 1);
            auto *dotsLayout = new QHBoxLayout;
            dotsLayout->setSpacing(4);
            for (int i = 0; i < 5; ++i) {
                auto *dot = new QToolButton;
                dot->setObjectName("slashEffortDot");
                dot->setCheckable(true);
                dot->setChecked(i == effortIdx);
                dot->setFixedSize(10, 10);
                dot->setCursor(Qt::PointingHandCursor);
                dot->setProperty("slashKind", QStringLiteral("effortLevel"));
                dot->setProperty("slashValue", QString::fromLatin1(effortLevels[i]));
                dot->installEventFilter(this);
                dotsLayout->addWidget(dot);
                m_slashActionRows.append(dot); // arrow-key reachable, like the rows
            }
            rowLayout->addLayout(dotsLayout);
            m_slashActionsListLayout->addWidget(row);
        }
        if (matches(QStringLiteral("Thinking")))
            addToggleRow(QStringLiteral("Thinking"), QStringLiteral("toggleThinking"),
                        QSettings().value(kClaudeThinkingSetting, true).toBool());
        if (matches(QStringLiteral("Switch models when a message is flagged")))
            addToggleRow(QStringLiteral("Switch models when a message is flagged"),
                        QStringLiteral("toggleFallback"),
                        QSettings().value(kClaudeFallbackModelSetting, false).toBool());
        if (matches(QStringLiteral("Account & usage")))
            addRow(QStringLiteral("Account & usage\xE2\x80\xA6"), QString(),
                   QStringLiteral("accountUsage"), QString());
    }

    // --- Commands section: the CLI's own slash commands, pulled live ---
    if (!m_claudeSlashCommands.isEmpty()) {
        bool anyCmd = false;
        for (const ClaudeSlashCommand &c : m_claudeSlashCommands)
            if (matches(c.name, c.description)) { anyCmd = true; break; }
        if (anyCmd) {
            addHeader(QStringLiteral("Commands"));
            for (const ClaudeSlashCommand &c : m_claudeSlashCommands) {
                if (!matches(c.name, c.description))
                    continue;
                addRow(QStringLiteral("/%1").arg(c.name), QString(),
                       QStringLiteral("command"), c.name);
            }
        }
    }

    m_slashActionsListLayout->addStretch(1);
    if (!m_slashActionRows.isEmpty()) {
        m_slashActionSelected = 0;
        m_slashActionRows.first()->setProperty("slashSelected", true);
        m_slashActionRows.first()->style()->unpolish(m_slashActionRows.first());
        m_slashActionRows.first()->style()->polish(m_slashActionRows.first());
    }
}

// Up/Down inside the popup filter box (adhoc #116): walk m_slashActionRows,
// which holds every row/dot in on-screen order, and keep it scrolled into view.
void MainWindow::moveSlashActionsSelection(int delta)
{
    if (m_slashActionRows.isEmpty())
        return;
    if (m_slashActionSelected >= 0 && m_slashActionSelected < m_slashActionRows.size()) {
        QWidget *prev = m_slashActionRows.at(m_slashActionSelected);
        prev->setProperty("slashSelected", false);
        prev->style()->unpolish(prev);
        prev->style()->polish(prev);
    }
    int next = qBound(0, m_slashActionSelected + delta, m_slashActionRows.size() - 1);
    m_slashActionSelected = next;
    QWidget *row = m_slashActionRows.at(next);
    row->setProperty("slashSelected", true);
    row->style()->unpolish(row);
    row->style()->polish(row);
    if (m_slashActionsScroll)
        m_slashActionsScroll->ensureWidgetVisible(row);
}

// Single dispatch point for every row/dot in the popup, driven by the
// "slashKind"/"slashValue" properties set when the row was built — reached
// from both a mouse click (MainWindow::eventFilter) and Enter in the filter box.
void MainWindow::activateSlashActionRow(QWidget *row)
{
    if (!row)
        return;
    const QString kind = row->property("slashKind").toString();
    const QString value = row->property("slashValue").toString();
    auto closePopup = [this] { if (m_slashActionsPopup) m_slashActionsPopup->hide(); };

    if (kind == QLatin1String("attachFile")) {
        closePopup();
        attachQuickAddImage();
    } else if (kind == QLatin1String("mentionFile")) {
        closePopup();
        mentionProjectFileInQuickAdd();
    } else if (kind == QLatin1String("clearConversation")) {
        if (m_issueQuickAdd)
            m_issueQuickAdd->clear();
        clearQuickAddImages();
        m_quickAddHistoryIndex = -1;
        closePopup();
    } else if (kind == QLatin1String("rewind")) {
        // No checkpoint/snapshot system exists to revert code changes yet, so
        // Rewind does the safe subset available today: recall the previous
        // sent prompt into the composer (same as Up in the quick-add history).
        navigateQuickAddHistory(-1);
        closePopup();
    } else if (kind == QLatin1String("sendIssueContext")) {
        closePopup();
        sendIssueContextToSelectedAgent();
    } else if (kind == QLatin1String("switchModel")) {
        closePopup();
        if (m_quickAddAgentProvider) {
            const int idx = m_quickAddAgentProvider->findData(QStringLiteral("claude-code"));
            if (idx >= 0)
                m_quickAddAgentProvider->setCurrentIndex(idx);
        }
        if (m_quickAddClaudeModel) {
            m_quickAddClaudeModel->setFocus();
            m_quickAddClaudeModel->showPopup();
        }
    } else if (kind == QLatin1String("effortLevel")) {
        QSettings().setValue(kClaudeEffortSetting, value);
        populateSlashActionsList();
    } else if (kind == QLatin1String("toggleThinking")) {
        QSettings().setValue(kClaudeThinkingSetting,
                             !QSettings().value(kClaudeThinkingSetting, true).toBool());
        populateSlashActionsList();
    } else if (kind == QLatin1String("toggleFallback")) {
        QSettings().setValue(
            kClaudeFallbackModelSetting,
            !QSettings().value(kClaudeFallbackModelSetting, false).toBool());
        populateSlashActionsList();
    } else if (kind == QLatin1String("accountUsage")) {
        closePopup();
        openAgentsOverview();
    } else if (kind == QLatin1String("command")) {
        if (m_issueQuickAdd) {
            QTextCursor cursor = m_issueQuickAdd->textCursor();
            cursor.movePosition(QTextCursor::End);
            if (!m_issueQuickAdd->toPlainText().isEmpty() &&
                !m_issueQuickAdd->toPlainText().endsWith(QLatin1Char('\n')))
                cursor.insertText(QStringLiteral("\n"));
            cursor.insertText(QStringLiteral("/%1 ").arg(value));
            m_issueQuickAdd->setTextCursor(cursor);
        }
        closePopup();
        if (m_issueQuickAdd)
            m_issueQuickAdd->setFocus();
    }
}

// Probes the live `claude` CLI for its slash-command list via the same
// control-protocol `initialize` request the VS Code extension sends
// (adhoc #116): pipe one control_request in, read the control_response, then
// tear the process down — this never runs a real turn. Cached for the rest of
// the app run; a no-op once loaded or while a probe is already in flight.
void MainWindow::refreshClaudeSlashCommands()
{
    if (m_claudeSlashCommandsLoaded || m_claudeSlashProbe)
        return;
    auto *proc = new QProcess(this);
    m_claudeSlashProbe = proc;
    m_claudeSlashProbeBuf.clear();
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_claudeSlashProbeBuf += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_claudeSlashProbeBuf.indexOf('\n')) >= 0) {
            const QByteArray line = m_claudeSlashProbeBuf.left(nl);
            m_claudeSlashProbeBuf.remove(0, nl + 1);
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            if (obj.value(QStringLiteral("type")).toString() !=
                QLatin1String("control_response"))
                continue;
            const QJsonArray commands = obj.value(QStringLiteral("response"))
                                             .toObject()
                                             .value(QStringLiteral("response"))
                                             .toObject()
                                             .value(QStringLiteral("commands"))
                                             .toArray();
            m_claudeSlashCommands.clear();
            for (const QJsonValue &v : commands) {
                const QJsonObject c = v.toObject();
                const QString name = c.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    continue;
                m_claudeSlashCommands.append(
                    {name, c.value(QStringLiteral("description")).toString(),
                     c.value(QStringLiteral("argumentHint")).toString()});
            }
            m_claudeSlashCommandsLoaded = true;
            if (m_slashActionsPopup && m_slashActionsPopup->isVisible())
                populateSlashActionsList();
            if (proc->state() != QProcess::NotRunning)
                proc->kill(); // the initialize handshake is all we needed
        }
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int, QProcess::ExitStatus) {
                if (m_claudeSlashProbe == proc)
                    m_claudeSlashProbe = nullptr;
                proc->deleteLater();
            });
    connect(proc, &QProcess::started, this, [proc] {
        const QJsonObject req{
            {QStringLiteral("type"), QStringLiteral("control_request")},
            {QStringLiteral("request_id"), QStringLiteral("forkmesh-slash-probe")},
            {QStringLiteral("request"),
             QJsonObject{{QStringLiteral("subtype"), QStringLiteral("initialize")}}}};
        proc->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
    });
    proc->start(QStringLiteral("bash"),
                {QStringLiteral("-lc"),
                 QStringLiteral("exec claude --print --input-format stream-json "
                                "--output-format stream-json --verbose")});
}

// "Mention file from this project…" (adhoc #116): pick a file under the
// current repo's working tree and insert an "@relative/path" reference into
// the quick-add prompt, the same shorthand the Claude Code CLI itself expects.
void MainWindow::mentionProjectFileInQuickAdd()
{
    if (!m_issueQuickAdd)
        return;
    QString baseDir;
    const int idx = issuesRepoIndex();
    if (idx >= 0 && idx < m_repositories.size())
        baseDir = m_repositories.at(idx).localPath;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Mention a file from this project"), baseDir);
    if (path.isEmpty()) {
        m_issueQuickAdd->setFocus();
        return;
    }
    QString mention = path;
    if (!baseDir.isEmpty()) {
        const QDir dir(baseDir);
        const QString rel = dir.relativeFilePath(path);
        if (!rel.startsWith(QStringLiteral("..")))
            mention = rel;
    }
    QTextCursor cursor = m_issueQuickAdd->textCursor();
    cursor.insertText(QStringLiteral("@%1 ").arg(mention));
    m_issueQuickAdd->setTextCursor(cursor);
    m_issueQuickAdd->setFocus();
}

// Show the mic only once a voice engine is installed; reset its idle look. Called
// when the bar is built and again after a successful install from Settings. Also
// syncs the comment-composer mics (m_voiceButtons), which share the same engine.
void MainWindow::updateVoiceInputButton()
{
    const bool ready = voiceInputReady();
    // The Auto-send toggle only makes sense alongside the mic, so it follows the
    // engine's installed state just like the mic button does.
    if (m_quickAddVoiceAutoSubmit)
        m_quickAddVoiceAutoSubmit->setVisible(ready);
    if (m_quickAddMicButton) {
        m_quickAddMicButton->setVisible(ready);
        // Leave the mic currently recording on its red broadcast glyph.
        if (!(m_voiceRecording && m_voiceActiveButton == m_quickAddMicButton)) {
            setOcticon(m_quickAddMicButton, "mic", 16);
            m_quickAddMicButton->setStyleSheet(QString());
            m_quickAddMicButton->setToolTip(
                QString::fromUtf8(
                    "Speak your prompt \xE2\x80\x94 hold to record, release "
                    "to transcribe.\nVoice model: %1")
                    .arg(voiceModelLabel()));
        }
    }
    for (QPushButton *b : m_voiceButtons) {
        if (!b)
            continue;
        b->setVisible(ready);
        if (m_voiceRecording && m_voiceActiveButton == b)
            continue;
        setOcticon(b, "mic", 16);
        b->setStyleSheet(QString());
    }
}

// Push-to-talk dictation: pressing the mic button records from the mic to a temp
// WAV; releasing it stops recording and runs whisper.cpp, inserting the text into
// the prompt box. All work is async (QProcess) so the UI never blocks.
//
// Release while recording: stop. The recorder finalizes the WAV on SIGTERM; the
// final transcription is kicked off from its finished handler.
void MainWindow::stopVoiceCapture()
{
    if (!m_voiceRecording)
        return;
    if (m_voiceLiveTimer)
        m_voiceLiveTimer->stop();
    stopVoiceLevelMeter();
    if (m_voiceRecordProc && m_voiceRecordProc->state() != QProcess::NotRunning)
        m_voiceRecordProc->terminate();
}

// Footer prompt mic: press-and-hold to dictate into the quick-add box.
void MainWindow::startVoiceCapture()
{
    startVoiceCaptureFor(m_issueQuickAdd, m_quickAddMicButton);
}

// Build a push-to-talk mic for a comment composer and remember it so the voice
// engine's install state keeps its visibility/idle look in sync. Holding it speaks
// into the composer's source editor (switching back to the write tab first so the
// dictated words are visible); releasing stops and transcribes.
QPushButton *MainWindow::makeVoiceButton(MarkdownEditor *composer)
{
    auto *btn = new QPushButton;
    btn->setObjectName("ghostButton");
    btn->setCursor(Qt::PointingHandCursor);
    setOcticon(btn, "mic", 16);
    btn->setToolTip(QString::fromUtf8("Speak your comment \xE2\x80\x94 hold to "
                                      "record, release to transcribe.\nVoice "
                                      "model: %1")
                        .arg(voiceModelLabel()));
    btn->setVisible(voiceInputReady());
    connect(btn, &QPushButton::pressed, this, [this, composer, btn] {
        if (!m_voiceRecording && composer)
            composer->showWriteArea();
        startVoiceCaptureFor(composer ? composer->sourceEdit() : nullptr, btn);
    });
    connect(btn, &QPushButton::released, this, &MainWindow::stopVoiceCapture);
    m_voiceButtons.append(btn);
    return btn;
}

// Press-and-hold to begin recording into `target` (see stopVoiceCapture for the
// release path). `target` may be the footer prompt or any comment composer's
// editor; `button` is the mic that was pressed.
void MainWindow::startVoiceCaptureFor(QPlainTextEdit *target, QPushButton *button)
{
    if (!button || !target)
        return;

    // Already recording (e.g. a stray second press): nothing to start.
    if (m_voiceRecording)
        return;

    if (!voiceInputReady()) {
        updateVoiceInputButton();
        return;
    }
    // Don't start a fresh recording while the previous clip is still transcribing
    // (it would clobber the shared insert span / target). Say so instead of
    // silently ignoring the press, so a quick "click again to dictate more" reads
    // as "wait a moment", not "the mic is broken".
    if (m_voiceTranscribeProc &&
        m_voiceTranscribeProc->state() != QProcess::NotRunning) {
        const QString busy =
            QStringLiteral("still transcribing the last clip\xE2\x80\xA6");
        const QString prev = target->placeholderText();
        if (prev != busy) {
            target->setPlaceholderText(busy);
            QTimer::singleShot(1200, target, [target, busy, prev] {
                if (target->placeholderText() == busy)
                    target->setPlaceholderText(prev);
            });
        }
        return;
    }

    // Remember which box this capture writes into and the box's own placeholder,
    // so status messages can be restored to it (not a hard-coded "enter prompt").
    m_voiceTargetEdit = target;
    m_voiceActiveButton = button;
    m_voiceIdlePlaceholder = target->placeholderText();

    m_voiceWavPath =
        QDir(QDir::tempPath())
            .filePath(QStringLiteral("forkmesh-voice-%1.wav")
                          .arg(QDateTime::currentMSecsSinceEpoch()));
    const AudioRecorderCommand rec = audioRecorderFor(m_voiceWavPath);
    if (rec.program.isEmpty()) {
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        logSystem(
            "Voice input needs ffmpeg to capture the microphone. Install it "
            "(macOS: \"brew install ffmpeg\"; Windows: ffmpeg.org) and make sure "
            "it's on your PATH.");
        target->setPlaceholderText("no recorder found (install ffmpeg)");
#else
        logSystem(
            "Voice input needs a microphone recorder. Install one of: arecord "
            "(alsa-utils), parecord (pulseaudio-utils) or ffmpeg.");
        target->setPlaceholderText(
            "no recorder found (install arecord / parecord / ffmpeg)");
#endif
        return;
    }

    auto *proc = new QProcess(this);
    m_voiceRecordProc = proc;
    connect(proc, &QProcess::finished, this,
            [this, proc](int, QProcess::ExitStatus) {
                m_voiceRecording = false;
                if (m_voiceLiveTimer)
                    m_voiceLiveTimer->stop();
                stopVoiceLevelMeter();
                if (m_voiceRecordProc == proc)
                    m_voiceRecordProc = nullptr;
                const QString err =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                updateVoiceInputButton();
                // The recorder may have failed to open the device at all (no
                // WAV, or just a header) — don't bother transcribing then.
                if (!QFileInfo::exists(m_voiceWavPath) ||
                    QFileInfo(m_voiceWavPath).size() < 1024) {
                    if (!err.isEmpty())
                        logSystem("Microphone capture failed: " + err.right(200));
                    if (m_voiceTargetEdit)
                        m_voiceTargetEdit->setPlaceholderText(m_voiceIdlePlaceholder);
                    m_voiceInsertPos = -1;
                    m_voiceInsertLen = 0;
                    QFile::remove(m_voiceWavPath);
                    return;
                }
                startVoiceTranscription(/*finalPass=*/true);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError err) {
                // Only FailedToStart skips finished(); other errors (a crash) still
                // emit finished, which owns cleanup. Avoid double-deleting here.
                if (err != QProcess::FailedToStart || m_voiceRecordProc != proc)
                    return;
                m_voiceRecording = false;
                m_voiceRecordProc = nullptr;
                if (m_voiceLiveTimer)
                    m_voiceLiveTimer->stop();
                stopVoiceLevelMeter();
                proc->deleteLater();
                updateVoiceInputButton();
                logSystem("Could not start the microphone recorder.");
                if (m_voiceTargetEdit)
                    m_voiceTargetEdit->setPlaceholderText(m_voiceIdlePlaceholder);
            });

    proc->start(rec.program, rec.args);
    if (!proc->waitForStarted(3000)) {
        // The recorder didn't come up in time (e.g. the audio device is busy).
        // errorOccurred() already cleaned up if this was a FailedToStart (then
        // m_voiceRecordProc is null and we skip). A plain timeout emits no such
        // signal, so tear the half-started process down ourselves and tell the
        // user — otherwise the mic looks dead while an orphan recorder lingers.
        if (m_voiceRecordProc == proc) {
            m_voiceRecordProc = nullptr;
            proc->kill();
            proc->deleteLater();
            logSystem("Couldn't start the microphone recorder \xE2\x80\x94 the "
                      "audio device may be busy. Try again.");
            target->setPlaceholderText(m_voiceIdlePlaceholder);
        }
        return;
    }
    m_voiceRecording = true;
    // Anchor the live-dictation span at the cursor so each refresh replaces only
    // the words we've inserted, leaving whatever the user typed alone.
    m_voiceInsertPos = target->textCursor().position();
    m_voiceInsertLen = 0;
    m_voiceLastTranscribeSize = 0;
    m_voiceLastPreview.clear();
    // Re-transcribe the growing clip on a timer so dictated words show up while
    // you're still talking (whisper-cli isn't streaming, so this re-runs over the
    // whole capture and replaces the span each pass). The final pass on stop is
    // authoritative; a flaky partial read just leaves the preview as-is. Parakeet
    // reloads its model on every invocation (seconds), so live ticks would thrash —
    // it transcribes once, on stop, only.
    if (voiceEngine() != QStringLiteral("parakeet")) {
        if (!m_voiceLiveTimer) {
            m_voiceLiveTimer = new QTimer(this);
            // Re-transcribe roughly every second so dictated words land in the box
            // soon after they're spoken. Ticks where the clip hasn't grown are
            // skipped in startVoiceTranscription(), so this stays cheap while you pause.
            m_voiceLiveTimer->setInterval(1000);
            connect(m_voiceLiveTimer, &QTimer::timeout, this,
                    [this] { startVoiceTranscription(/*finalPass=*/false); });
        }
        m_voiceLiveTimer->start();
    }
    // Drive the live input-level meter from the growing capture. The meter lives
    // in the footer next to the prompt mic, so only animate it for that mic; a
    // comment mic just turns red while recording.
    m_voiceLevelPos = 0;
    if (button == m_quickAddMicButton) {
        if (!m_voiceLevelTimer) {
            m_voiceLevelTimer = new QTimer(this);
            m_voiceLevelTimer->setInterval(80);
            connect(m_voiceLevelTimer, &QTimer::timeout, this,
                    &MainWindow::updateVoiceLevelMeter);
        }
        if (m_voiceLevelMeter) {
            m_voiceLevelMeter->setValue(0);
            m_voiceLevelMeter->setVisible(true);
        }
        m_voiceLevelTimer->start();
    }
    // A red broadcast glyph makes the "recording now" state unmistakable. Tinted
    // directly (not via setOcticon) so it stays red regardless of the button's
    // normal icon colour; updateVoiceInputButton() restores the idle mic.
    button->setIcon(themedOcticon("broadcast", QColor("#f85149"), 16));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(
        QStringLiteral("Recording\xE2\x80\xA6 release to stop and transcribe."));
    target->setPlaceholderText("listening\xE2\x80\xA6 release the mic to stop");
}

// Run whisper.cpp over the recorded WAV and drop the transcript into the prompt
// box. Called both live (finalPass=false, while the clip is still growing) and
// once after recording stops (finalPass=true, authoritative). Only one pass runs
// at a time: a live tick yields to an in-flight pass; the final pass preempts a
// still-running live tick so the box always ends on the full transcript.
void MainWindow::startVoiceTranscription(bool finalPass)
{
    if (!m_voiceTargetEdit)
        return;
    if (m_voiceTranscribeProc &&
        m_voiceTranscribeProc->state() != QProcess::NotRunning) {
        if (!finalPass)
            return; // a pass is already running; skip this live tick
        m_voiceTranscribeProc->kill();
        m_voiceTranscribeProc->waitForFinished(200);
    }

    // On the final pass any early-out must restore the idle prompt state, since
    // recording has already stopped and nothing else will.
    auto finishIdle = [this] {
        hideVoiceTranscribeSpinner();
        if (m_voiceTargetEdit)
            m_voiceTargetEdit->setPlaceholderText(m_voiceIdlePlaceholder);
        m_voiceInsertPos = -1;
        m_voiceInsertLen = 0;
        QFile::remove(m_voiceWavPath);
    };

    const bool parakeet = voiceEngine() == QStringLiteral("parakeet");
    if (parakeet ? !parakeetInstalled()
                 : (whisperBinaryPath().isEmpty() ||
                    !QFileInfo::exists(whisperModelPath()))) {
        if (finalPass)
            finishIdle();
        return;
    }
    // Need more than a bare WAV header to be worth transcribing (a live tick can
    // fire before the recorder has captured anything).
    const qint64 wavSize =
        QFileInfo::exists(m_voiceWavPath) ? QFileInfo(m_voiceWavPath).size() : 0;
    if (wavSize < 4096) {
        if (finalPass)
            finishIdle();
        return;
    }
    // Live ticks: skip when the clip hasn't grown by ~a quarter second of audio
    // (16 kHz mono 16-bit ≈ 32 KB/s) since the last pass. Re-running whisper over
    // an unchanged clip just reloads the model to redo identical work, which is the
    // main source of jank while the speaker pauses. The final pass always runs.
    if (!finalPass && wavSize - m_voiceLastTranscribeSize < 8192)
        return;
    m_voiceLastTranscribeSize = wavSize;

    if (finalPass) {
        m_voiceTargetEdit->setPlaceholderText("transcribing\xE2\x80\xA6");
        // Recording has stopped but whisper/Parakeet is still running — ring the mic
        // so the wait reads as active transcription, not a dead button.
        showVoiceTranscribeSpinner();
    }

    // whisper.cpp writes "<base>.txt" with -otxt -of <base>; reading the file is
    // more robust than parsing stdout (which also carries timing logs). Clear any
    // prior pass's file first so we never read a stale transcript.
    const QString base = m_voiceWavPath + QStringLiteral(".out");
    const QString wav = m_voiceWavPath;
    QFile::remove(base + QStringLiteral(".txt"));
    auto *proc = new QProcess(this);
    m_voiceTranscribeProc = proc;
    connect(proc, &QProcess::finished, this,
            [this, proc, base, wav, finalPass](int exitCode, QProcess::ExitStatus) {
                const QString err =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                if (m_voiceTranscribeProc == proc)
                    m_voiceTranscribeProc = nullptr;

                QString text;
                QFile txt(base + QStringLiteral(".txt"));
                if (txt.open(QIODevice::ReadOnly))
                    text = QString::fromUtf8(txt.readAll());
                QFile::remove(base + QStringLiteral(".txt"));
                // whisper marks silence with "[BLANK_AUDIO]"; collapse whitespace.
                text.remove(QStringLiteral("[BLANK_AUDIO]"));
                text = text.simplified();
                // Drop whisper's silence hallucinations ("you", "thank you", …)
                // when the clip was effectively quiet, so an unspoken capture
                // doesn't type a stray word into the prompt.
                if (!text.isEmpty() && isWhisperSilenceHallucination(text) &&
                    wavPeakAmplitude(wav) < kVoiceSpokeThreshold)
                    text.clear();

                if (!finalPass) {
                    // Live preview: only show real words; ignore empty/suppressed
                    // results and ones identical to what's already shown so the
                    // partial transcript doesn't flicker or churn the cursor.
                    if (!text.isEmpty() && text != m_voiceLastPreview) {
                        m_voiceLastPreview = text;
                        applyVoiceTranscript(text, /*finalPass=*/false);
                    }
                    return;
                }

                hideVoiceTranscribeSpinner();
                // Capture the dictation target before applyVoiceTranscript releases
                // the live span; the Auto-send check below needs to know whether the
                // footer prompt (not a comment composer) was the one being dictated.
                const bool wasFooterPrompt = m_voiceTargetEdit == m_issueQuickAdd;
                if (m_voiceTargetEdit)
                    m_voiceTargetEdit->setPlaceholderText(m_voiceIdlePlaceholder);
                QFile::remove(wav);
                applyVoiceTranscript(text, /*finalPass=*/true);
                if (text.isEmpty() && exitCode != 0)
                    logSystem("Transcription failed" +
                              (err.isEmpty() ? QString() : ": " + err.right(200)));
                else if (text.isEmpty())
                    logSystem("No speech detected \xE2\x80\x94 check that your "
                              "microphone is capturing audio.");
                // Auto-send (adhoc #45): once the footer prompt has its final
                // transcript, submit it just like pressing Enter/Send. Only fires when
                // the toggle is on and the dictation actually produced words, so a
                // silent capture never sends an empty (or stale) prompt.
                else if (wasFooterPrompt && m_quickAddVoiceAutoSubmit &&
                         m_quickAddVoiceAutoSubmit->isChecked())
                    quickAddIssue();
            });
    // Both engines write the transcript to "<base>.txt"; reading the file is more
    // robust than parsing stdout. whisper.cpp produces it via -otxt -of <base>;
    // Parakeet's transcribe.py is handed the exact path to write.
    if (parakeet) {
        proc->start(parakeetPython(),
                    {parakeetScriptPath(), wav, base + QStringLiteral(".txt"),
                     parakeetModelName()});
    } else {
        // Speed flags keep dictation snappy: greedy decode (-bs 1), no temperature
        // fallback (-nf, which otherwise re-decodes "hard" segments several times),
        // and most of the box's cores (-t) while leaving one for the UI so the app
        // stays smooth during transcription.
        const int threads = qBound(2, QThread::idealThreadCount() - 1, 8);
        proc->start(whisperBinaryPath(),
                    {QStringLiteral("-m"), whisperModelPath(), QStringLiteral("-f"),
                     wav, QStringLiteral("-nt"), QStringLiteral("-otxt"),
                     QStringLiteral("-of"), base, QStringLiteral("-bs"),
                     QStringLiteral("1"), QStringLiteral("-nf"), QStringLiteral("-t"),
                     QString::number(threads)});
    }
}

// Replace the live-dictation span [m_voiceInsertPos, +m_voiceInsertLen] with
// `text`, so successive (live or final) passes update the same words instead of
// piling up. A leading space is added when the dictation follows existing text so
// words don't run together. On the final pass the span is released.
void MainWindow::applyVoiceTranscript(const QString &text, bool finalPass)
{
    if (!m_voiceTargetEdit || m_voiceInsertPos < 0) {
        if (finalPass) {
            m_voiceInsertPos = -1;
            m_voiceInsertLen = 0;
        }
        return;
    }
    const QString full = m_voiceTargetEdit->toPlainText();
    const int start = qBound(0, m_voiceInsertPos, full.size());
    const int end = qBound(start, start + m_voiceInsertLen, full.size());

    QString prefix;
    if (start > 0 && start <= full.size() && !full.at(start - 1).isSpace())
        prefix = QStringLiteral(" ");
    const QString ins = text.isEmpty() ? QString() : prefix + text;

    QTextCursor cur = m_voiceTargetEdit->textCursor();
    cur.setPosition(start);
    cur.setPosition(end, QTextCursor::KeepAnchor);
    cur.insertText(ins);
    m_voiceInsertLen = ins.size();

    if (finalPass) {
        m_voiceInsertPos = -1;
        m_voiceInsertLen = 0;
        m_voiceTargetEdit->setTextCursor(cur);
        m_voiceTargetEdit->setFocus();
    }
}

// Sample the freshly-captured tail of the WAV and drive the meter beside the mic.
// Peaks are scaled with a square root so ordinary speech (well below full scale)
// still moves the bar visibly; the value snaps up on a louder sample (attack) and
// eases back down (release) so the meter reads like a real level indicator rather
// than flickering.
void MainWindow::updateVoiceLevelMeter()
{
    if (!m_voiceLevelMeter)
        return;
    const double peak = wavLevelSince(m_voiceWavPath, &m_voiceLevelPos);
    const int cur = m_voiceLevelMeter->value();
    int next;
    if (peak < 0.0) {
        next = qMax(0, cur - 14); // no new audio: decay toward silence
    } else {
        const int target =
            int(qBound(0.0, qSqrt(peak) * 135.0, 100.0));
        next = target >= cur ? target : qMax(target, cur - 14);
    }
    if (next != cur)
        m_voiceLevelMeter->setValue(next);
}

// Freeze and hide the input-level meter once recording stops.
void MainWindow::stopVoiceLevelMeter()
{
    if (m_voiceLevelTimer)
        m_voiceLevelTimer->stop();
    if (m_voiceLevelMeter) {
        m_voiceLevelMeter->setValue(0);
        m_voiceLevelMeter->setVisible(false);
    }
}

// Ring the active mic with a rotating processing circle while a released clip is
// still being transcribed, so the wait after letting go reads as "still working".
// The spinner is a sibling overlay of the mic, sized a touch larger so the ring
// sits around the glyph; it's reparented onto whichever mic started the capture
// (footer prompt or a comment composer) and re-centred each time it's shown.
void MainWindow::showVoiceTranscribeSpinner()
{
    QPushButton *btn = m_voiceActiveButton;
    if (!btn || !btn->parentWidget())
        return;
    if (!m_voiceTranscribeSpinner)
        m_voiceTranscribeSpinner = new RingSpinner(btn->parentWidget());
    QWidget *sp = m_voiceTranscribeSpinner;
    if (sp->parentWidget() != btn->parentWidget())
        sp->setParent(btn->parentWidget());
    const QRect bg = btn->geometry();
    const int pad = 3;
    const int d = qMax(bg.width(), bg.height()) + 2 * pad;
    QRect r(0, 0, d, d);
    r.moveCenter(bg.center());
    sp->setGeometry(r);
    sp->raise();
    sp->show();
}

void MainWindow::hideVoiceTranscribeSpinner()
{
    if (m_voiceTranscribeSpinner)
        m_voiceTranscribeSpinner->hide();
}

void MainWindow::updateFooterGitIdentity()
{
    if (!m_footerGitIdentity)
        return;
    // No repo open (Log/Leaderboards/etc.): nothing repo-specific to show.
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size()) {
        m_footerGitIdentity->clear();
        return;
    }
    const QString dir = m_repositories.at(m_repoDetailIndex).localPath;
    if (dir.isEmpty()) {
        m_footerGitIdentity->clear();
        return;
    }
    // `git config user.name/user.email` returns the effective value (repo-local
    // overriding global), i.e. the identity commits in this repo are authored as.
    // Read asynchronously: this runs inside openRepoDetail, and a synchronous
    // read here blocked the GUI thread ~600 ms during startup (adhoc #112). One
    // --get-regexp call covers both keys; git lists matches system→global→local,
    // so keeping the last occurrence of each key gives the effective value.
    runGitDetached(
        dir, {QStringLiteral("config"), QStringLiteral("--get-regexp"),
              QStringLiteral("^user\\.(name|email)$")},
        [this, dir](bool, const QByteArray &out) {
            if (!m_footerGitIdentity)
                return;
            // Repo switched (or closed) while the read was in flight.
            if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size() ||
                m_repositories.at(m_repoDetailIndex).localPath != dir)
                return;
            QString name, email;
            for (const QString &line :
                 QString::fromUtf8(out).split(QLatin1Char('\n'))) {
                const int sp = line.indexOf(QLatin1Char(' '));
                if (sp <= 0)
                    continue;
                const QString key = line.left(sp);
                const QString value = line.mid(sp + 1).trimmed();
                if (key == QLatin1String("user.name"))
                    name = value;
                else if (key == QLatin1String("user.email"))
                    email = value;
            }
            QString text;
            if (!name.isEmpty() && !email.isEmpty())
                text = QStringLiteral("%1 <%2>").arg(name, email);
            else if (!name.isEmpty())
                text = name;
            else if (!email.isEmpty())
                text = email;
            else
                text = QStringLiteral("git identity not set");
            m_footerGitIdentity->setText(text);
        });
}

// Start the UI-stall watchdog + the live CPU/memory readout. Called once the
// window is up so the heartbeat reflects a real, interactive event loop.
void MainWindow::startDiagnostics()
{
    if (!m_stallWatchdog) {
        m_stallWatchdog = new StallWatchdog(this);
        connect(m_stallWatchdog, &StallWatchdog::stalled, this, &MainWindow::onUiStall);
#ifdef FORKMESH_WINDOW_TESTS
        // Test binaries stall on purpose (blocking asserts, offscreen waits) —
        // never let their reports pollute the user's real diagnostics log.
        const QString logPath;
#else
        const QString logPath =
            QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics/stalls.log");
#endif
        m_stallLogPath = logPath;
        // Rotate an oversized log (it had grown past 12 MB) so appends and any
        // "read the stall log" tooling stay fast; one previous generation kept.
        if (QFileInfo(logPath).size() > 4 * 1024 * 1024) {
            const QString prev = logPath + QStringLiteral(".1");
            QFile::remove(prev);
            QFile::rename(logPath, prev);
        }
        // Recorded with each stall so a report sent to an agent identifies the
        // exact build and where its source lives (FORKMESH_SOURCE_DIR is the
        // build-time qt_client path).
        const QString buildInfo =
            QStringLiteral("ForkMesh v" FORKMESH_VERSION " (src " FORKMESH_SOURCE_DIR ")");
        // 500 ms, not 1500: "snappy" means sub-half-second interactions, and the
        // old threshold let real (but shorter) click-freezes go unrecorded. Every
        // report names the blocking operation via the BlockingCallScope crumbs.
        m_stallWatchdog->start(/*stallThresholdMs=*/500, logPath, buildInfo);
    }
    if (!m_diagTimer) {
        m_diagTimer = new QTimer(this);
        m_diagTimer->setInterval(1000); // one sample a second into the charts
        connect(m_diagTimer, &QTimer::timeout, this,
                &MainWindow::updateFooterDiagnostics);
        m_diagTimer->start();
    }
    updateFooterDiagnostics();
}

// Refresh the footer readout: this process's CPU% (since the last tick) and its
// resident memory, read from /proc, plus any UI-stall count.
void MainWindow::updateFooterDiagnostics()
{
    if (!m_footerDiagnostics)
        return;
    double cpuPct = -1.0;
    long rssMb = -1;
#if defined(__linux__)
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray s = stat.readAll();
        const int rp = s.lastIndexOf(')'); // comm field may hold spaces/parens
        const QList<QByteArray> f = s.mid(rp + 2).split(' ');
        if (f.size() > 12) { // utime=14th, stime=15th field overall
            const qulonglong ticks = f.at(11).toULongLong() + f.at(12).toULongLong();
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (m_diagLastCpuTicks > 0 && nowMs > m_diagLastCpuMs) {
                const double dTicks = double(ticks) - double(m_diagLastCpuTicks);
                const double dSec = (nowMs - m_diagLastCpuMs) / 1000.0;
                const long hz = sysconf(_SC_CLK_TCK);
                if (hz > 0 && dSec > 0)
                    cpuPct = qMax(0.0, (dTicks / hz) / dSec * 100.0);
            }
            m_diagLastCpuTicks = ticks;
            m_diagLastCpuMs = nowMs;
        }
    }
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> p = statm.readAll().split(' ');
        if (p.size() > 1)
            rssMb = static_cast<long>((p.at(1).toULongLong() * sysconf(_SC_PAGESIZE)) /
                                      (1024 * 1024));
    }
#endif
    // Feed the three moving sparklines. CPU is this process's busy fraction of
    // one core (the /proc/self/stat figure above); memory and disk are the
    // host's used fraction, so all three plot on a 0..100% scale (adhoc #17).
    const QString dash = QString::fromUtf8("\xE2\x80\x94"); // em dash
    if (auto *cpu = static_cast<ResourceSparkline *>(m_cpuChart)) {
        cpu->addSample(cpuPct >= 0 ? cpuPct : 0.0, 100.0,
                       cpuPct >= 0 ? QStringLiteral("%1%").arg(cpuPct, 0, 'f', 0)
                                   : dash);
        QString tip = QStringLiteral("CPU used by this app");
        if (rssMb >= 0)
            tip += QStringLiteral(" \xC2\xB7 %1\xE2\x80\xAFMB resident").arg(rssMb);
        cpu->setToolTip(tip);
    }
    if (auto *mem = static_cast<ResourceSparkline *>(m_memChart)) {
        const qint64 total = SystemStats::totalMemoryBytes();
        const qint64 avail = SystemStats::availableMemoryBytes();
        double pct = -1.0;
        if (total > 0 && avail >= 0 && avail <= total)
            pct = 100.0 * double(total - avail) / double(total);
        mem->addSample(pct >= 0 ? pct : 0.0, 100.0,
                       pct >= 0 ? QStringLiteral("%1%").arg(pct, 0, 'f', 0) : dash);
        mem->setToolTip(
            total > 0
                ? QStringLiteral("Host memory in use: %1 of %2")
                      .arg(SystemStats::formatBytes(total - avail),
                           SystemStats::formatBytes(total))
                : QStringLiteral("Host memory in use"));
    }
    if (auto *disk = static_cast<ResourceSparkline *>(m_diskChart)) {
        const QString path = QDir::homePath();
        const qint64 total = SystemStats::diskTotalBytes(path);
        const qint64 free = SystemStats::diskFreeBytes(path);
        double pct = -1.0;
        if (total > 0 && free >= 0 && free <= total)
            pct = 100.0 * double(total - free) / double(total);
        disk->addSample(pct >= 0 ? pct : 0.0, 100.0,
                        pct >= 0 ? QStringLiteral("%1%").arg(pct, 0, 'f', 0) : dash);
        disk->setToolTip(
            total > 0
                ? QStringLiteral("Drive space in use: %1 of %2 (%3 free)")
                      .arg(SystemStats::formatBytes(total - free),
                           SystemStats::formatBytes(total),
                           SystemStats::formatBytes(free))
                : QStringLiteral("Drive space in use"));
    }

    // The footer button keeps only the UI-stall badge now that CPU/MEM live in
    // the charts; the 🖥 glyph stays as the labelled click target.
    QString txt = QString::fromUtf8("\xF0\x9F\x96\xA5"); // 🖥
    if (m_stallCount > 0)
        txt += QString::fromUtf8("  \xE2\x9A\xA0 %1 stall%2")
                   .arg(m_stallCount)
                   .arg(m_stallCount == 1 ? QString() : QStringLiteral("s"));
    m_footerDiagnostics->setText(txt.trimmed());
}

// A UI stall ended: record it, surface it in the system log, and reflect the
// running count in the footer. The full backtrace is kept for the detail dialog.
void MainWindow::onUiStall(qint64 peakMs, const QString &blockingCall,
                           const QString &backtrace)
{
    ++m_stallCount;
    const QString when = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    // Name the culprit operation inline so the one-line Log entry is actionable on
    // its own; the full backtrace stays in the stall-detail dialog.
    QString head =
        QStringLiteral("[%1] UI stalled ~%2 ms (event loop blocked)").arg(when).arg(peakMs);
    if (!blockingCall.isEmpty())
        head += QStringLiteral(" while %1").arg(blockingCall);
    logSystem(head); // shows up in the app's Log view
    QString entry = head;
    if (!backtrace.isEmpty())
        entry += QLatin1Char('\n') + backtrace;
    m_stallLog.append(entry);
    while (m_stallLog.size() > 100)
        m_stallLog.removeFirst();
    if (m_footerDiagnostics)
        m_footerDiagnostics->setToolTip(
            QStringLiteral("Last UI stall: ~%1 ms at %2%3. Click for details (%4 logged).")
                .arg(peakMs)
                .arg(when)
                .arg(blockingCall.isEmpty() ? QString()
                                            : QStringLiteral(" (%1)").arg(blockingCall))
                .arg(m_stallCount));
    updateFooterDiagnostics();
    maybeAutoFileStallAgent(peakMs, backtrace);
}

// If the user has left the "auto-create an agent task for new stalls" setting on
// (the default), hand this freeze straight to a coding agent so it gets fixed.
// The backtrace already pinpoints the blocking call and carries the build's
// source dir, so it's an actionable task on its own. De-duped by backtrace and
// capped per session so a recurring freeze — or an agent run that itself stalls —
// can't spawn an unbounded pile of tasks (adhoc #205).
void MainWindow::maybeAutoFileStallAgent(qint64 peakMs, const QString &backtrace)
{
    if (!QSettings().value(kAutoAgentOnStallSetting, true).toBool())
        return;
    // The watchdog now *records* everything past 500 ms (sub-second jank matters
    // for snappiness), but only a solidly user-visible freeze warrants spinning
    // up a whole fix-it agent.
    if (peakMs < 1500)
        return;
    // No captured stack means nothing actionable to point an agent at.
    const QString signature = backtrace.trimmed();
    if (signature.isEmpty())
        return;
    if (m_autoFiledStallSignatures.contains(signature))
        return; // already filed this exact freeze this session
    // Safety cap: never spin up more than a handful of stall-fix agents in one
    // session, even if every stall has a distinct backtrace.
    constexpr int kMaxAutoStallAgents = 5;
    if (m_autoFiledStallSignatures.size() >= kMaxAutoStallAgents)
        return;

    // Prefer ForkMesh's own checkout (the freeze is in this app's GUI thread);
    // fall back to whatever repo the Issues tab is pointed at.
    const int repoIndex = stallReportRepoIndex();
    if (repoIndex < 0)
        return; // no local checkout to run an agent in

    const QString prompt =
        QStringLiteral(
            "ForkMesh's GUI thread stalled for ~%1 ms — the event loop was "
            "blocked, which makes the window freeze. Find the blocking call in "
            "the backtrace below and fix it so the UI stays responsive (move the "
            "slow work off the main thread, or skip it when nothing changed). "
            "Backtrace:\n\n%2")
            .arg(peakMs)
            .arg(backtrace);
    if (startAdHocAgentForRepo(repoIndex, prompt, defaultAgentProvider(),
                               /*createPr=*/true) > 0) {
        m_autoFiledStallSignatures.insert(signature);
        logSystem(QStringLiteral(
            "Auto-started an agent to fix the UI stall (toggle in Settings > "
            "Agents & IDE)."));
    }
}

// Repo whose checkout a stall-fix agent runs in: prefer ForkMesh's own source
// tree (the freeze is in this app's GUI thread), else the Issues tab's repo.
int MainWindow::stallReportRepoIndex() const
{
    const QString bakedSource = QStringLiteral(FORKMESH_SOURCE_DIR);
    if (!bakedSource.isEmpty()) {
        const QString selfSource = QDir(bakedSource).absolutePath();
        for (int i = 0; i < m_repositories.size(); ++i) {
            const QString local = m_repositories.at(i).localPath;
            if (!local.isEmpty() && QDir(local).absolutePath() == selfSource)
                return i;
        }
    }
    return issuesRepoIndex();
}

// Clear button on the diagnostics dialog: forget every recorded stall so the
// footer badge, the detail list and the durable log all start fresh. The
// watchdog re-creates the on-disk log (Append) whenever the next stall lands.
void MainWindow::clearStallLog()
{
    m_stallCount = 0;
    m_stallLog.clear();
    m_autoFiledStallSignatures.clear();
    if (!m_stallLogPath.isEmpty())
        QFile::remove(m_stallLogPath);
    if (m_footerDiagnostics)
        m_footerDiagnostics->setToolTip(
            QStringLiteral("Live CPU and memory use of this app. Click for UI-stall "
                           "diagnostics (when the UI freezes long enough to trip the "
                           "Wait/Kill prompt)."));
    updateFooterDiagnostics();
}

namespace {

// Cap per field so a chatty crash/stall log can never inflate the telemetry
// POST — the worker's isolate is small and this must never become an outage
// vector (issue #354). Whole payload stays well under the worker's hard cap.
constexpr int kTelemetryFieldCap = 8000;

// Read the not-yet-uploaded tail of a diagnostics log, given how many bytes were
// already sent. Returns the new tail and, via *newSize, the file's current size
// so the caller can advance the stored offset only after a successful upload. If
// the file shrank (rotated/cleared) since last time, the offset resets to 0.
QString readDiagnosticsTail(const QString &path, qint64 offset, qint64 *newSize)
{
    QFile f(path);
    *newSize = 0;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const qint64 size = f.size();
    *newSize = size;
    qint64 from = offset;
    if (from < 0 || from > size)
        from = 0; // rotated or cleared: start over
    if (from >= size)
        return QString();
    f.seek(from);
    return QString::fromUtf8(f.readAll());
}

// Strip repo names and filesystem paths from a diagnostics blob before it leaves
// the machine (privacy requirement, issue #354). Home directories carry the
// username and every local repo checkout, so collapse them to "~"; other
// absolute paths are reduced to their basename so a backtrace still names the
// source file without leaking where it lives.
QString scrubDiagnostics(QString text)
{
    if (text.isEmpty())
        return text;
    const QString home = QDir::homePath();
    if (!home.isEmpty())
        text.replace(home, QStringLiteral("~"));
    // Any remaining /home/<user>/ or /Users/<user>/ (e.g. from another account's
    // path recorded in a shared log) → ~/.
    static const QRegularExpression userHome(
        QStringLiteral("/(?:home|Users)/[^/\\s:]+"));
    text.replace(userHome, QStringLiteral("~"));
    // Absolute paths (a build/source dir, an object path in a frame) → basename,
    // so "/opt/build/src/MainWindow.cpp:42" becomes "MainWindow.cpp:42".
    static const QRegularExpression absPath(
        QStringLiteral("/(?:[^/\\s():]+/)+([^/\\s():]+)"));
    text.replace(absPath, QStringLiteral("\\1"));
    if (text.size() > kTelemetryFieldCap)
        text = text.right(kTelemetryFieldCap);
    return text;
}

} // namespace

// Opt-in crash/stall telemetry (issue #354). Off unless the user turned on
// kUploadTelemetrySetting in Settings. Uploads only the tail of each diagnostics
// log that hasn't been sent before (tracked by a byte offset), scrubbed of repo
// names and paths, tagged with just the app version, OS, and an anonymized
// one-way node hash. Fire-and-forget: never blocks startup, never surfaces UI.
void MainWindow::maybeUploadDiagnostics()
{
    if (!QSettings().value(kUploadTelemetrySetting, false).toBool())
        return;
    if (!m_networkAccess)
        return;

    QSettings settings;
    const QString diagDir = QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics/");
    const QString crashPath = diagDir + QStringLiteral("crashes.log");
    const QString stallPath = m_stallLogPath.isEmpty()
                                  ? diagDir + QStringLiteral("stalls.log")
                                  : m_stallLogPath;

    qint64 crashSize = 0, stallSize = 0;
    const QString crash = scrubDiagnostics(readDiagnosticsTail(
        crashPath, settings.value(kTelemetryCrashOffsetSetting, 0).toLongLong(),
        &crashSize));
    const QString stalls = scrubDiagnostics(readDiagnosticsTail(
        stallPath, settings.value(kTelemetryStallOffsetSetting, 0).toLongLong(),
        &stallSize));

    QJsonArray events;
    if (!crash.trimmed().isEmpty())
        events.append(QJsonObject{{"kind", "crash"}, {"summary", crash}});
    if (!stalls.trimmed().isEmpty())
        events.append(QJsonObject{{"kind", "stall"}, {"summary", stalls}});
    if (events.isEmpty()) {
        // Nothing new to report; still advance the offsets so a later append
        // doesn't re-scan the whole (unchanged) file.
        settings.setValue(kTelemetryCrashOffsetSetting, crashSize);
        settings.setValue(kTelemetryStallOffsetSetting, stallSize);
        return;
    }

    // Anonymized node hash: a one-way SHA-256 of our public key, so reports from
    // the same node group together for triage without revealing the identity.
    QString node;
    if (m_profileIdentity.isValid() || m_profileIdentity.load())
        node = QString::fromLatin1(
            QCryptographicHash::hash(m_profileIdentity.publicKey().toUtf8(),
                                     QCryptographicHash::Sha256)
                .toHex());

    QJsonObject body{
        {"node", node},
        {"version", QStringLiteral(FORKMESH_VERSION)},
        {"os", QSysInfo::prettyProductName() + QLatin1Char(' ') +
                   QSysInfo::currentCpuArchitecture()},
        {"events", events},
    };

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/telemetry"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, crashSize, stallSize] {
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        // Only advance the uploaded offsets once the mainnode has accepted the
        // batch, so a transient failure re-sends the same records next startup.
        if (status >= 200 && status < 300) {
            QSettings settings;
            settings.setValue(kTelemetryCrashOffsetSetting, crashSize);
            settings.setValue(kTelemetryStallOffsetSetting, stallSize);
            logSystem(QStringLiteral("Uploaded opt-in crash/stall telemetry."));
        }
    });
}

// "Send to a new agent" button on the diagnostics dialog: hand the whole batch
// of recorded stalls to one coding agent so the freezes get fixed. Mirrors the
// auto-file prompt but bundles every entry (the auto-file path only ever fires
// on one stall at a time). Returns true once an agent has been started.
bool MainWindow::sendStallLogToAgent()
{
    if (m_stallLog.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("UI stall diagnostics"),
                                 QStringLiteral("There are no recorded UI stalls to send."));
        return false;
    }
    const int repoIndex = stallReportRepoIndex();
    if (repoIndex < 0) {
        QMessageBox::warning(
            this, QStringLiteral("UI stall diagnostics"),
            QStringLiteral("No local checkout is available to run an agent in. Clone "
                           "ForkMesh (or add the repo on the Issues tab) and try again."));
        return false;
    }
    const QString prompt =
        QStringLiteral(
            "ForkMesh's GUI thread stalled %1 time(s) this session — the event loop "
            "was blocked, which makes the window freeze. For each report below, find "
            "the blocking call in the backtrace and fix it so the UI stays responsive "
            "(move the slow work off the main thread, or skip it when nothing "
            "changed). Recorded stalls:\n\n%2")
            .arg(m_stallLog.size())
            .arg(m_stallLog.join(QStringLiteral("\n\n---\n\n")));
    if (startAdHocAgentForRepo(repoIndex, prompt, defaultAgentProvider(),
                               /*createPr=*/true) <= 0) {
        QMessageBox::warning(this, QStringLiteral("UI stall diagnostics"),
                             QStringLiteral("Could not start an agent for the recorded stalls."));
        return false;
    }
    logSystem(QStringLiteral("Started an agent to fix the %1 recorded UI stall(s).")
                  .arg(m_stallLog.size()));
    return true;
}

// Detail view for the diagnostics readout: the recorded UI stalls (with the
// captured backtraces) plus where the durable log lives.
void MainWindow::showDiagnosticsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("UI stall diagnostics"));
    dlg.resize(720, 480);
    auto *v = new QVBoxLayout(&dlg);
    auto *summary = new QLabel(
        m_stallCount == 0
            ? QStringLiteral("No UI stalls detected this session. The app watches the "
                             "GUI thread and records any freeze longer than 1.5s here.")
            : QStringLiteral("%1 UI stall(s) detected this session. Each entry below "
                             "is where the GUI thread was blocked.")
                  .arg(m_stallCount));
    summary->setWordWrap(true);
    v->addWidget(summary);
    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    applyLogFont(view);
    view->setPlainText(m_stallLog.isEmpty() ? QStringLiteral("(nothing recorded yet)")
                                            : m_stallLog.join(QStringLiteral("\n\n")));
    v->addWidget(view, 1);
    if (!m_stallLogPath.isEmpty()) {
        auto *path = new QLabel(QStringLiteral("Durable log: %1").arg(m_stallLogPath));
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        path->setStyleSheet(QStringLiteral("color:#8b949e;font-size:11px;"));
        v->addWidget(path);
    }
    const bool haveStalls = !m_stallLog.isEmpty();
    auto *clearBtn = new QPushButton(QStringLiteral("Clear"));
    clearBtn->setToolTip(QStringLiteral("Forget every recorded stall (and its durable log)"));
    clearBtn->setEnabled(haveStalls);
    auto *sendBtn = new QPushButton(QStringLiteral("Send to a new agent"));
    sendBtn->setToolTip(
        QStringLiteral("Hand all recorded stalls to a coding agent to investigate and fix"));
    sendBtn->setEnabled(haveStalls);
    auto *close = new QPushButton(QStringLiteral("Close"));

    connect(clearBtn, &QPushButton::clicked, &dlg, [this, summary, view, clearBtn, sendBtn] {
        clearStallLog();
        summary->setText(
            QStringLiteral("No UI stalls detected this session. The app watches the "
                           "GUI thread and records any freeze longer than 1.5s here."));
        view->setPlainText(QStringLiteral("(nothing recorded yet)"));
        clearBtn->setEnabled(false);
        sendBtn->setEnabled(false);
    });
    connect(sendBtn, &QPushButton::clicked, &dlg, [this, &dlg] {
        if (sendStallLogToAgent())
            dlg.accept();
    });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto *row = new QHBoxLayout;
    row->addWidget(clearBtn);
    row->addStretch(1);
    row->addWidget(sendBtn);
    row->addWidget(close);
    v->addLayout(row);
    dlg.exec();
}

void MainWindow::showTreasuryDonateDialog()
{
    // Pull the central-fund address from the relay (a wallet the relay custodies
    // and sweeps out to online nodes hourly, issue #308) and render a Solana QR
    // so anyone can donate without us embedding the address.
    int status = 0;
    const QJsonObject resp = getAccountSync("central-fund", &status);
    const QString address = resp.value("address").toString().trimmed();
    if (address.isEmpty()) {
        QMessageBox::information(
            this, "Donate to ForkMesh",
            "The central fund isn't accepting donations right now. Please try "
            "again later.");
        return;
    }
    // The endpoint returns a proper Solana Pay URI (label + message); fall back
    // to a bare address URI if an older relay omits it.
    QString uri = resp.value("uri").toString().trimmed();
    if (uri.isEmpty())
        uri = QStringLiteral("solana:%1").arg(address);

    QDialog dialog(this);
    dialog.setWindowTitle("Donate to the ForkMesh central fund");
    auto *l = new QVBoxLayout(&dialog);
    l->setContentsMargins(20, 20, 20, 20);
    l->setSpacing(12);

    auto *intro = new QLabel(
        "Scan this Solana QR or copy the address below to donate to the ForkMesh "
        "central fund. The relay distributes the fund to every online node once "
        "an hour, so your donation goes straight to the people keeping the "
        "network alive.");
    intro->setWordWrap(true);
    l->addWidget(intro);

    auto *qrLabel = new QLabel;
    qrLabel->setAlignment(Qt::AlignCenter);
    const QImage qr = QrCode::encodeToImage(uri, 6, 4);
    if (!qr.isNull())
        qrLabel->setPixmap(QPixmap::fromImage(qr));
    l->addWidget(qrLabel);

    auto *addrLabel = new QLabel(address);
    addrLabel->setObjectName("payAddr");
    addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addrLabel->setWordWrap(true);
    addrLabel->setAlignment(Qt::AlignCenter);
    l->addWidget(addrLabel);

    auto *row = new QHBoxLayout;
    auto *copyBtn = new QPushButton("Copy address");
    copyBtn->setObjectName("primaryButton");
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QPushButton::clicked, this, [address, copyBtn] {
        QApplication::clipboard()->setText(address);
        copyBtn->setText("Copied!");
    });
    auto *closeBtn = new QPushButton("Close");
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    row->addStretch(1);
    row->addWidget(copyBtn);
    row->addWidget(closeBtn);
    l->addLayout(row);

    dialog.exec();
}

QWidget *MainWindow::buildLogSection()
{
    auto *page = new QWidget;

    auto *label = new QLabel("NETWORK LOG");
    label->setObjectName("sectionLabel");
    auto *clearButton = new QPushButton("Clear");
    clearButton->setObjectName("ghostButton");
    clearButton->setCursor(Qt::PointingHandCursor);
    clearButton->setToolTip("Clear the network log");
    setOcticon(clearButton, "trash", 14);

    m_settingsLog = new QPlainTextEdit;
    m_settingsLog->setReadOnly(true);
    m_settingsLog->setObjectName("networkLog");
    m_settingsLog->setMaximumBlockCount(kNetworkLogLimit);

    // Quick-filter chips that narrow the log to a single event category. The row
    // scrolls horizontally so a long set of categories never clips the log.
    auto *filterRowWidget = new QWidget;
    m_logFilterRow = new QHBoxLayout(filterRowWidget);
    m_logFilterRow->setContentsMargins(0, 0, 0, 0);
    m_logFilterRow->setSpacing(6);
    auto *filterScroll = new QScrollArea;
    filterScroll->setObjectName("logFilterScroll");
    filterScroll->setWidget(filterRowWidget);
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFixedHeight(34);

    // Discover which categories the buffered history contains and build the
    // chips now, but leave rendering the history itself (up to kNetworkLogLimit
    // lines of colored HTML, ~300ms) to the first visit of the Log section —
    // it's pure constructor cost for a view most launches never open. Live
    // logSystem() lines still append to the (empty) view immediately; the first
    // visit's full rebuild re-renders the buffer in order, history included.
    m_logFilterCategories.clear();
    for (const QString &line : std::as_const(m_networkLog))
        m_logFilterCategories.insert(logBadgeFor(line));
    rebuildLogFilterButtons();
    m_networkLogViewStale = !m_networkLog.isEmpty();

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_networkLog.clear();
        m_lastLogRenderDate.clear();
        m_logFilter.clear();
        m_logFilterCategories.clear();
        if (m_settingsLog)
            m_settingsLog->clear();
        saveNetworkLog();          // truncate the on-disk log too
        rebuildLogFilterButtons(); // drop the category chips, re-check "All"
    });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(label);
    headerRow->addStretch();
    headerRow->addWidget(clearButton);

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    layout->addLayout(headerRow);
    layout->addWidget(filterScroll);
    layout->addWidget(m_settingsLog, 1);
    return page;
}

QWidget *MainWindow::buildBreadcrumb()
{
    auto *bar = new QWidget;
    bar->setObjectName("breadcrumbBar");

    // --- Relay switcher: bigger favicon (shows that relay's nodes when
    // clicked), a "domain ▾ count" dropdown (search / switch / add), and an
    // open-in-browser icon. ---------------------------------------------------
    m_relayIconButton = new QPushButton;
    m_relayIconButton->setObjectName("relayIconButton");
    m_relayIconButton->setCursor(Qt::PointingHandCursor);
    m_relayIconButton->setFixedSize(38, 38);
    m_relayIconButton->setIconSize(QSize(30, 30));
    m_relayIconButton->setToolTip("Show this relay's nodes");
    connect(m_relayIconButton, &QPushButton::clicked, this,
            [this] { showSection(0); });

    m_relayMenuButton = new QPushButton;
    m_relayMenuButton->setObjectName("relayMenuButton");
    m_relayMenuButton->setCursor(Qt::PointingHandCursor);
    m_relayMenuButton->setToolTip("Switch, search, or add relays");
    connect(m_relayMenuButton, &QPushButton::clicked, this,
            &MainWindow::showRelayMenu);

    // Spinning radar + once-a-minute latency readout, sitting just left of the
    // relay name (issue #144). The probe itself is driven by m_relayLatencyTimer.
    m_relayRadar = new RelayRadarWidget;

    m_relayOpenButton = new QPushButton;
    m_relayOpenButton->setObjectName("relayOpenButton");
    m_relayOpenButton->setCursor(Qt::PointingHandCursor);
    m_relayOpenButton->setFixedSize(30, 30);
    setOcticon(m_relayOpenButton, "link", 16);
    m_relayOpenButton->setToolTip("Open this relay in your browser");
    connect(m_relayOpenButton, &QPushButton::clicked, this,
            [this] { openServerWebsite(m_activeServer); });

    // Node switcher, to the right of the relay switcher: "node ▾ count".
    m_nodeMenuButton = new QPushButton;
    m_nodeMenuButton->setObjectName("nodeMenuButton");
    m_nodeMenuButton->setCursor(Qt::PointingHandCursor);
    m_nodeMenuButton->setToolTip("Pick a node to view its repositories");
    connect(m_nodeMenuButton, &QPushButton::clicked, this, &MainWindow::showNodeMenu);

    // Node name shown above the wallet balance in the top-right cluster.
    m_navNodeName = new QLabel;
    m_navNodeName->setObjectName("navNodeName");
    m_navNodeName->setAlignment(Qt::AlignCenter);
    m_navNodeName->setFixedWidth(148);

    m_navSolanaBalance = new QLabel(QStringLiteral("SOL --"));
    m_navSolanaBalance->setObjectName("navSolanaBalance");
    m_navSolanaBalance->setAlignment(Qt::AlignCenter);
    m_navSolanaBalance->setFixedWidth(148);
    m_navSolanaBalance->setCursor(Qt::PointingHandCursor);
    m_navSolanaBalance->setToolTip(
        "This node's Solana wallet balance \xE2\x80\x94 click to switch "
        "currency (SOL / USD / INR)");
    // Clicking the balance itself cycles its display currency, so the control
    // sits right on the value instead of needing a separate swap icon.
    m_navSolanaBalance->installEventFilter(this);

    // The reward-availability toggle (online/offline switch, status line, uptime)
    // used to live here beside the balance; it's now built in
    // buildNodeProfilePanel(), right under "Get paid to mirror", as a clear
    // on/off switch for the whole node rather than a small top-bar pill.

    // Tiny Claude Code usage chart that rides beside the earnings/avatar (issue
    // #266): a 5-hour and a weekly horizontal gauge. Seed it from the last cached
    // utilisation so it renders immediately; from there it only updates when a
    // prompt is sent (bumpClaudeCodeUsage) or live rate-limit events land — plus
    // an on-demand refresh when the user hovers the chart to check it, since
    // there's no background poll keeping it current between those.
    auto *tokenUsage = new TokenUsageMiniChart;
    m_navTokenUsage = tokenUsage;
    tokenUsage->onHover = [this] { refreshClaudeCodeUsage(); };
    {
        QSettings settings;
        auto restore = [&](bool weekly, const QString &key) {
            if (settings.contains(key))
                tokenUsage->setUsage(weekly, settings.value(key).toInt());
        };
        restore(false, kClaudeUsage5hPctSetting);
        restore(true, kClaudeUsageWeekPctSetting);
        // Reset countdown (issue #50): the cached instant is wall-clock, so derive
        // the remaining time relative to now; a window that already elapsed shows
        // no countdown until the next poll refreshes it.
        auto restoreReset = [&](bool weekly, const QString &key) {
            if (!settings.contains(key))
                return;
            const qint64 remaining = settings.value(key).toLongLong() -
                                     QDateTime::currentMSecsSinceEpoch();
            if (remaining > 0)
                tokenUsage->setReset(weekly, humanizeRemaining(remaining));
        };
        restoreReset(false, kClaudeUsage5hResetSetting);
        restoreReset(true, kClaudeUsageWeekResetSetting);
    }

    // Repo switcher, to the right of the node switcher: "repo ▾ count".
    m_repoMenuButton = new QPushButton;
    m_repoMenuButton->setObjectName("repoMenuButton");
    m_repoMenuButton->setCursor(Qt::PointingHandCursor);
    m_repoMenuButton->setToolTip("Open a repository, or add a local repo to mirror");
    connect(m_repoMenuButton, &QPushButton::clicked, this, &MainWindow::showRepoMenu);

    // Primary section nav: Code / Chat / Notifications / Settings. These four
    // are a uniform, checkable button group that lives in the always-visible top
    // bar (so the nav stays put on Settings, Chat and Notifications too) and
    // highlights the active section. Each maps to an m_sectionStack index.
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);

    // "Code" button: show the repo detail (Home section). When a repo is open it
    // jumps to that repo's Code view; otherwise it just lands on Home.
    m_repoViewButton = new QPushButton(QStringLiteral("Repo"));
    m_repoViewButton->setObjectName("topNavButton");
    m_repoViewButton->setCheckable(true);
    m_repoViewButton->setCursor(Qt::PointingHandCursor);
    m_repoViewButton->setToolTip(QStringLiteral("View the current repository's code"));
    setOcticon(m_repoViewButton, "code", 16);
    m_navGroup->addButton(m_repoViewButton, 0); // section 0: Home / Code
    connect(m_repoViewButton, &QPushButton::clicked, this, [this] {
        showSection(0);
        if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
            if (m_repoCodeTab)
                m_repoCodeTab->setChecked(true);
            if (m_repoDetailStack)
                m_repoDetailStack->setCurrentIndex(0); // Code
            showRepoOverview();
        }
    });

    // m_repoPushButton ("Publish N") is created in buildRepoDetailSection where
    // its row lives, so it is parented before it can ever be shown. (Building it
    // in a section constructed later left it parentless and it popped up as its
    // own floating window.)

    m_breadcrumb = new QLabel;
    m_breadcrumb->setObjectName("breadcrumb");
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href == "repos") {
            showSection(0);
        } else if (href == "server") {
            openServerWebsite(m_activeServer);
        }
    });
    // The live connection indicator is now a small status dot painted over the
    // top-right avatar (created with the avatar below), not a separate text pill.

    m_notificationButton = new QPushButton(QStringLiteral("Notifications"));
    m_notificationButton->setObjectName("topNavButton");
    m_notificationButton->setCheckable(true);
    m_notificationButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_notificationButton, "bell", 16);
    m_notificationButton->setToolTip("Notifications");
    m_navGroup->addButton(m_notificationButton, 3); // section 3: Notifications
    connect(m_notificationButton, &QPushButton::clicked, this,
            &MainWindow::showNotifications);

    // Compact, centered success/failure toast. It lives in the middle of the
    // top bar (between the breadcrumb and the notifications bell) and is flanked
    // by stretches so it stays centered regardless of breadcrumb width.
    m_topMessage = new QLabel;
    m_topMessage->setObjectName("topMessage");
    m_topMessage->setTextFormat(Qt::RichText);
    m_topMessage->setAlignment(Qt::AlignCenter);
    // Hard cap on the pill's width so a long toast can never widen the window; the
    // text itself is elided to one line in flashMessage. A long message reveals its
    // full text inline via the Expand button beside the toast (see renderTopMessage)
    // rather than popping up a modal.
    m_topMessage->setMaximumWidth(620);
    // Selectable like before, plus clickable links so the integrity-pin warning can
    // carry its "Reset integrity pin" / "Why?" actions inline (see showPinWarning).
    m_topMessage->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                          Qt::LinksAccessibleByMouse);
    connect(m_topMessage, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href == QLatin1String("fm:resetpin"))
            resetRepoPin();
        else if (href == QLatin1String("fm:whypin"))
            showPinExplanation();
        else if (href.startsWith(QLatin1String("fm:agent:"))) {
            // "agent is waiting for you" toast: jump straight to that session.
            bool ok = false;
            const int sid = href.mid(9).toInt(&ok);
            if (ok) {
                switchToAgentsTab(sid);
                dismissTopMessage();
            }
        }
    });
    m_topMessage->hide();

    // Copy button shown beside the toast for errors only. An error toast counts
    // down for a long window (kToastErrorSeconds) and keeps this Copy / ✕ pair the
    // whole time, so a failure can be read and grabbed for a bug report before it
    // fades on its own.
    m_topMessageCopy = new QPushButton(QStringLiteral("Copy"));
    m_topMessageCopy->setObjectName("ghostButton");
    m_topMessageCopy->setCursor(Qt::PointingHandCursor);
    m_topMessageCopy->setToolTip(QStringLiteral("Copy this message and dismiss it"));
    setOcticon(m_topMessageCopy, "copy", 14);
    m_topMessageCopy->hide();
    connect(m_topMessageCopy, &QPushButton::clicked, this, [this] {
        if (!m_topMessageRaw.isEmpty())
            QGuiApplication::clipboard()->setText(m_topMessageRaw);
        advanceTopMessageQueue(); // move on to the next queued error, if any
    });
    // A plain "x" to dismiss an error toast without copying it.
    m_topMessageClose = new QPushButton(QString::fromUtf8("\xE2\x9C\x95")); // ✕
    m_topMessageClose->setObjectName("ghostButton");
    m_topMessageClose->setCursor(Qt::PointingHandCursor);
    m_topMessageClose->setToolTip(QStringLiteral("Dismiss"));
    m_topMessageClose->hide();
    connect(m_topMessageClose, &QPushButton::clicked, this,
            [this] { advanceTopMessageQueue(); }); // skip straight to the next queued error

    // Shown beside the toast when a message is too long to fit on one line.
    // Clicking it expands the full message in place (wrapped, growing the toast)
    // and toggles back to the elided one-liner — no modal pops up.
    m_topMessageExpand = new QPushButton;
    m_topMessageExpand->setObjectName("ghostButton");
    m_topMessageExpand->setCursor(Qt::PointingHandCursor);
    m_topMessageExpand->setToolTip(QStringLiteral("Show the full message"));
    setOcticon(m_topMessageExpand, "chevron-down", 14);
    m_topMessageExpand->hide();
    connect(m_topMessageExpand, &QPushButton::clicked, this, [this] {
        m_topMessageExpanded = !m_topMessageExpanded;
        renderTopMessage();
        // Keep the live countdown suffix if a success toast is still ticking.
        if (m_topMessageTimer && m_topMessageTimer->isActive())
            renderTopMessageCountdown();
    });

    // The expanded full text lives in this floating panel, parented to the window
    // (not to any layout) and raised above everything when shown. Revealing it
    // therefore overlays the UI on top instead of growing the inline toast, so it
    // never shifts the top bar or the layout below it. See renderTopMessage.
    m_topMessageOverlay = new QFrame(this);
    m_topMessageOverlay->setObjectName("topMessageOverlay");
    auto *overlayLayout = new QVBoxLayout(m_topMessageOverlay);
    overlayLayout->setContentsMargins(12, 10, 12, 10);
    m_topMessageOverlayText = new QLabel;
    m_topMessageOverlayText->setObjectName("topMessageOverlayText");
    m_topMessageOverlayText->setTextFormat(Qt::RichText);
    m_topMessageOverlayText->setWordWrap(true);
    m_topMessageOverlayText->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                     Qt::LinksAccessibleByMouse);
    connect(m_topMessageOverlayText, &QLabel::linkActivated, this,
            [this](const QString &href) {
                if (href == QLatin1String("fm:resetpin"))
                    resetRepoPin();
                else if (href == QLatin1String("fm:whypin"))
                    showPinExplanation();
            });
    overlayLayout->addWidget(m_topMessageOverlayText);
    m_topMessageOverlay->hide();

    // User avatar, pinned to the top-right-most of the bar. Clicking it opens a
    // dropdown with account-level actions.
    m_avatarNavButton = new QPushButton;
    m_avatarNavButton->setObjectName("serverFooterButton");
    m_avatarNavButton->setCursor(Qt::PointingHandCursor);
    m_avatarNavButton->setFixedSize(40, 40);
    m_avatarNavButton->setIconSize(QSize(34, 34));
    m_avatarNavButton->setToolTip("Your node profile");
    connect(m_avatarNavButton, &QPushButton::clicked, this, [this] {
        // Open this node's own profile in the side panel (which carries the
        // restart options, settings shortcut and logout for self).
        showSection(0);
        showNodeProfile(m_profileIdentity.publicKey(), m_userName);
    });
    updateAvatarButton();

    // Connection status dot, overlaid on the bottom-right of the avatar. It's
    // purely decorative (clicks fall through to the avatar); the live status
    // text lives in the avatar's tooltip, set by updateConnectionStatus.
    m_connectionDot = new QLabel(m_avatarNavButton);
    m_connectionDot->setObjectName("connectionDot");
    m_connectionDot->setFixedSize(12, 12);
    m_connectionDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_connectionDot->move(40 - 12 - 1, 40 - 12 - 1);
    m_connectionDot->raise();

    // Captions for the three top-bar dropdowns.
    auto makeCaption = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setObjectName("navCaption");
        return l;
    };
    m_relayLabel = makeCaption(QStringLiteral("Relay"));
    m_nodeLabel = makeCaption(QStringLiteral("Node"));
    m_repoLabel = makeCaption(QStringLiteral("Repo"));

    // Agents: a shortcut into the current repo's Agents tab (adhoc #194), not a
    // section of its own — it just jumps via openAgentsOverview() the same way
    // the footer "Agents:" label does. Sits between Repo and Chat in the nav
    // row. Checkable to show when the Agents tab is active (adhoc #201).
    m_agentsNavButton = new QPushButton(QStringLiteral("Agents"));
    m_agentsNavButton->setObjectName("topNavButton");
    m_agentsNavButton->setCheckable(true);
    m_agentsNavButton->setCursor(Qt::PointingHandCursor);
    m_agentsNavButton->setToolTip(QStringLiteral("Agents"));
    setOcticon(m_agentsNavButton, "terminal", 16);
    connect(m_agentsNavButton, &QPushButton::clicked, this,
            &MainWindow::openAgentsOverview);

    // Chat: its own top-level section (m_sectionStack index 2).
    m_chatButton = new QPushButton(QStringLiteral("Chat"));
    m_chatButton->setObjectName("topNavButton");
    m_chatButton->setCheckable(true);
    m_chatButton->setCursor(Qt::PointingHandCursor);
    m_chatButton->setToolTip(QStringLiteral("Chat"));
    setOcticon(m_chatButton, "comment", 16);
    m_navGroup->addButton(m_chatButton, 2); // section 2: Chat
    connect(m_chatButton, &QPushButton::clicked, this, &MainWindow::showChatView);
    // Red unread-count badge pinned to the chat button's top-right corner. It's
    // decorative (clicks fall through to the button); updateChatButton sizes,
    // positions and shows/hides it from the unread tally.
    m_chatUnreadBadge = new QLabel(m_chatButton);
    m_chatUnreadBadge->setObjectName("chatUnreadBadge");
    m_chatUnreadBadge->setAlignment(Qt::AlignCenter);
    m_chatUnreadBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_chatUnreadBadge->hide();

    // Settings: its own top-level section (m_sectionStack index 1).
    m_settingsNavButton = new QPushButton(QStringLiteral("Settings"));
    m_settingsNavButton->setObjectName("topNavButton");
    m_settingsNavButton->setCheckable(true);
    m_settingsNavButton->setCursor(Qt::PointingHandCursor);
    m_settingsNavButton->setToolTip(QStringLiteral("Settings"));
    setOcticon(m_settingsNavButton, "gear", 16);
    m_navGroup->addButton(m_settingsNavButton, 1); // section 1: Settings
    connect(m_settingsNavButton, &QPushButton::clicked, this,
            [this] { showSection(1); });

    // Log: the full network log, next to Settings (m_sectionStack index 4).
    m_logNavButton = new QPushButton(QStringLiteral("Log"));
    m_logNavButton->setObjectName("topNavButton");
    m_logNavButton->setCheckable(true);
    m_logNavButton->setCursor(Qt::PointingHandCursor);
    m_logNavButton->setToolTip(QString::fromUtf8("Network log \xE2\x80\x94 all activity"));
    setOcticon(m_logNavButton, "list-unordered", 16);
    m_navGroup->addButton(m_logNavButton, 4); // section 4: Log
    connect(m_logNavButton, &QPushButton::clicked, this,
            [this] { showSection(4); });

    // Leaderboards: the public network rankings (issue #11), section index 5.
    m_leaderboardNavButton = new QPushButton(QStringLiteral("Leaderboards"));
    m_leaderboardNavButton->setObjectName("topNavButton");
    m_leaderboardNavButton->setCheckable(true);
    m_leaderboardNavButton->setCursor(Qt::PointingHandCursor);
    m_leaderboardNavButton->setToolTip(
        QString::fromUtf8("Leaderboards \xE2\x80\x94 network rankings"));
    setOcticon(m_leaderboardNavButton, "graph", 16);
    m_navGroup->addButton(m_leaderboardNavButton, 5); // section 5: Leaderboards
    connect(m_leaderboardNavButton, &QPushButton::clicked, this,
            [this] { showSection(5); });

    // Hosts (adhoc #263): provision a remote machine by SSHing in and running the
    // ForkMesh installer over ansible. Sits right next to Leaderboards, section 7.
    m_hostsNavButton = new QPushButton(QStringLiteral("Hosts"));
    m_hostsNavButton->setObjectName("topNavButton");
    m_hostsNavButton->setCheckable(true);
    m_hostsNavButton->setCursor(Qt::PointingHandCursor);
    m_hostsNavButton->setToolTip(
        QString::fromUtf8("Hosts \xE2\x80\x94 install ForkMesh on a remote machine"));
    setOcticon(m_hostsNavButton, "server", 16);
    m_navGroup->addButton(m_hostsNavButton, 7); // section 7: Hosts
    connect(m_hostsNavButton, &QPushButton::clicked, this,
            [this] { showSection(7); });

    // Relays: a live list of the configured mainnode relays with their online
    // status, round-trip response time and running version. Sits next to Hosts,
    // section 8.
    m_relaysNavButton = new QPushButton(QStringLiteral("Relays"));
    m_relaysNavButton->setObjectName("topNavButton");
    m_relaysNavButton->setCheckable(true);
    m_relaysNavButton->setCursor(Qt::PointingHandCursor);
    m_relaysNavButton->setToolTip(
        QString::fromUtf8("Relays \xE2\x80\x94 online status, response time and version"));
    setOcticon(m_relaysNavButton, "broadcast", 16);
    m_navGroup->addButton(m_relaysNavButton, 8); // section 8: Relays
    connect(m_relaysNavButton, &QPushButton::clicked, this,
            [this] { showSection(8); });

    // Small, icon-only rebuild+restart button, right-aligned under the avatar on
    // the section-nav row. Hidden unless opted in via Settings (off by default);
    // it's a dev-iteration shortcut for the same fast rebuild as the profile panel.
    m_navRebuildButton = new QPushButton;
    m_navRebuildButton->setObjectName("topNavButton");
    m_navRebuildButton->setCursor(Qt::PointingHandCursor);
    m_navRebuildButton->setToolTip(
        QString::fromUtf8("Rebuild & restart \xE2\x80\x94 fast local rebuild, "
                          "then relaunch"));
    setOcticon(m_navRebuildButton, "sync", 14);
    connect(m_navRebuildButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_navRebuildButton); quickRebuildRestart(); });

    // Square screenshot button beside the rebuild/restart button: drag a region
    // anywhere on screen and it lands in the prompt as an attachment.
    m_navScreenshotButton = new QPushButton;
    m_navScreenshotButton->setObjectName("topNavButton");
    m_navScreenshotButton->setCursor(Qt::PointingHandCursor);
    m_navScreenshotButton->setToolTip(
        QString::fromUtf8("Screenshot a region \xE2\x80\x94 drag a square anywhere on "
                          "screen and it's attached to your prompt"));
    setOcticon(m_navScreenshotButton, "screen-full", 14);
    connect(m_navScreenshotButton, &QPushButton::clicked, this,
            &MainWindow::captureScreenRegion);

    // Pencil button beside the screenshot button: drop a transparent overlay you
    // can scribble on freehand anywhere on screen — handy for pointing things out.
    m_navDrawButton = new QPushButton;
    m_navDrawButton->setObjectName("topNavButton");
    m_navDrawButton->setCursor(Qt::PointingHandCursor);
    m_navDrawButton->setToolTip(
        QString::fromUtf8("Draw on the screen \xE2\x80\x94 scribble freehand "
                          "anywhere; Esc to clear it away"));
    setOcticon(m_navDrawButton, "pencil", 14);
    connect(m_navDrawButton, &QPushButton::clicked, this,
            &MainWindow::startScreenDraw);

    // Donate + social cluster, moved up out of the footer (adhoc #117). A standout
    // donate button (opens the central-fund QR) sits beside a compact column of two
    // icon-only social buttons — the ForkMesh Reddit and Twitter/X links — stacked
    // vertically so they take up little width next to the donate button.
    auto *donateButton = new QPushButton(QString::fromUtf8("\xE2\x99\xA5 Donate"));
    donateButton->setObjectName("donateButton");
    donateButton->setCursor(Qt::PointingHandCursor);
    donateButton->setToolTip(
        "Donate SOL to the ForkMesh central fund (distributed to online nodes "
        "hourly)");
    connect(donateButton, &QPushButton::clicked, this,
            &MainWindow::showTreasuryDonateDialog);

    auto *redditButton = new QPushButton;
    redditButton->setObjectName("socialIconButton");
    redditButton->setCursor(Qt::PointingHandCursor);
    redditButton->setToolTip("ForkMesh on Reddit");
    redditButton->setFixedSize(28, 22);
    setOcticon(redditButton, "reddit", 15);
    connect(redditButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://www.reddit.com/user/forkmesh"));
    });

    auto *twitterButton = new QPushButton;
    twitterButton->setObjectName("socialIconButton");
    twitterButton->setCursor(Qt::PointingHandCursor);
    twitterButton->setToolTip("ForkMesh on X (Twitter)");
    twitterButton->setFixedSize(28, 22);
    setOcticon(twitterButton, "twitter-bird", 14);
    connect(twitterButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://x.com/forkmesh"));
    });

    auto *socialColumn = new QVBoxLayout;
    socialColumn->setContentsMargins(0, 0, 0, 0);
    socialColumn->setSpacing(3);
    socialColumn->addWidget(redditButton);
    socialColumn->addWidget(twitterButton);

    // Live diagnostics, also moved up out of the footer (adhoc #117): CPU / memory
    // of this process plus a count of detected UI stalls. Click to see the stall
    // details.
    m_footerDiagnostics = new QPushButton;
    m_footerDiagnostics->setObjectName("footerDiagnostics");
    m_footerDiagnostics->setFlat(true);
    m_footerDiagnostics->setCursor(Qt::PointingHandCursor);
    m_footerDiagnostics->setToolTip(
        "Live CPU and memory use of this app. Click for UI-stall diagnostics "
        "(when the UI freezes long enough to trip the Wait/Kill prompt).");
    m_footerDiagnostics->setStyleSheet(
        "QPushButton#footerDiagnostics{color:#8b949e;border:none;background:transparent;"
        "font-size:11px;padding:2px 6px;}"
        "QPushButton#footerDiagnostics:hover{color:#e6edf3;}");
    connect(m_footerDiagnostics, &QPushButton::clicked, this,
            &MainWindow::showDiagnosticsDialog);

    // Three little button-sized squares beside the diagnostics glyph, each plotting
    // one resource — this app's CPU, the host's memory and its disk — as a moving
    // sparkline fed one sample a second by updateFooterDiagnostics. Clicking one
    // opens the same diagnostics dialog as the glyph.
    auto *cpuChart = new ResourceSparkline(QStringLiteral("CPU"));
    auto *memChart = new ResourceSparkline(QStringLiteral("MEM"));
    auto *diskChart = new ResourceSparkline(QStringLiteral("DISK"));
    for (ResourceSparkline *chart : {cpuChart, memChart, diskChart})
        chart->onClicked = [this] { showDiagnosticsDialog(); };
    m_cpuChart = cpuChart;
    m_memChart = memChart;
    m_diskChart = diskChart;

    auto *layout = new QVBoxLayout(bar);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    // First row: relay > node > repo navigation, breadcrumb, centered toast,
    // and the right-aligned connection / balance / avatar cluster.
    auto *mainRow = new QHBoxLayout;
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(8);
    mainRow->addWidget(m_relayIconButton);
    mainRow->addWidget(m_relayLabel);
    mainRow->addWidget(m_relayRadar); // radar + latency, left of the relay name
    mainRow->addWidget(m_relayMenuButton);
    mainRow->addWidget(m_relayOpenButton);
    mainRow->addSpacing(10);
    mainRow->addWidget(m_nodeLabel);
    mainRow->addWidget(m_nodeMenuButton);
    mainRow->addSpacing(10);
    mainRow->addWidget(m_repoLabel);
    mainRow->addWidget(m_repoMenuButton);
    // m_repoPushButton ("Publish N") now lives in its own row above the repo tab
    // bar (see buildRepoDetail), not in the top navigation row.
    mainRow->addSpacing(12);
    // Browser-style Back / Forward buttons, sat just left of the search box.
    mainRow->addWidget(createNavHistoryButtons());
    // One box that searches everything (sections, relays, nodes, repos, and the
    // open repo's issues/PRs/branches/files/commits) and jumps to the result.
    mainRow->addWidget(createGlobalSearchBox());
    mainRow->addSpacing(6);
    mainRow->addWidget(m_breadcrumb);
    mainRow->addStretch();
    mainRow->addWidget(m_topMessage);
    mainRow->addWidget(m_topMessageExpand);
    mainRow->addWidget(m_topMessageCopy);
    mainRow->addWidget(m_topMessageClose);
    mainRow->addStretch();
    // Live CPU/MEM/DISK sparklines, moved up next to the donate button (adhoc #121).
    mainRow->addWidget(cpuChart);
    mainRow->addWidget(memChart);
    mainRow->addWidget(diskChart);
    mainRow->addSpacing(8);
    // Donate button + the vertically-stacked Reddit/X icons, sat just left of the
    // account cluster (adhoc #117).
    mainRow->addWidget(donateButton);
    mainRow->addSpacing(4);
    mainRow->addLayout(socialColumn);
    mainRow->addSpacing(10);
    // Stack the node name above the wallet balance — "this is your money". The
    // online/reward toggle that used to sit here now lives in the node profile
    // panel, under "Get paid to mirror".
    auto *balanceColumn = new QVBoxLayout;
    balanceColumn->setContentsMargins(0, 0, 0, 0);
    balanceColumn->setSpacing(0);
    balanceColumn->addWidget(m_navNodeName);
    balanceColumn->addWidget(m_navSolanaBalance);
    mainRow->addLayout(balanceColumn);
    // The tiny token-usage chart tucks between the earnings and the avatar.
    mainRow->addSpacing(6);
    mainRow->addWidget(m_navTokenUsage);
    mainRow->addSpacing(4);
    mainRow->addWidget(m_avatarNavButton);
    layout->addLayout(mainRow);

    // Hairline divider separating the relay/node row from the section nav below.
    auto *navDivider = new QFrame;
    navDivider->setObjectName("navDivider");
    navDivider->setFrameShape(QFrame::HLine);
    navDivider->setFixedHeight(1);
    layout->addWidget(navDivider);

    // The primary section nav (Code / Chat / Notifications / Settings / Log)
    // lives in its own row in the always-visible top bar, so these buttons stay
    // put above whatever section they open — they don't disappear when you leave
    // the repo view, and the log is one click away next to Settings.
    auto *navRow = new QHBoxLayout;
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->setSpacing(8);
    navRow->addWidget(m_repoViewButton);
    navRow->addWidget(m_agentsNavButton);
    navRow->addWidget(m_chatButton);
    navRow->addWidget(m_notificationButton);
    navRow->addWidget(m_settingsNavButton);
    navRow->addWidget(m_logNavButton);
    navRow->addWidget(m_leaderboardNavButton);
    navRow->addWidget(m_hostsNavButton);
    navRow->addWidget(m_relaysNavButton);
    navRow->addSpacing(16);
    // Live diagnostics glyph (CPU/MEM/DISK sparklines moved up to mainRow for adhoc #121).
    navRow->addWidget(m_footerDiagnostics);
    navRow->addStretch();
    // Right-aligned so they sit under the top-right avatar; the pencil and
    // screenshot buttons sit just left of the rebuild/restart button.
    navRow->addWidget(m_navDrawButton);
    navRow->addWidget(m_navScreenshotButton);
    navRow->addWidget(m_navRebuildButton);
    layout->addLayout(navRow);
    // Home/Code is the initial section, so show its nav button selected up front.
    m_repoViewButton->setChecked(true);

    updateBreadcrumb();
    updateConnectionStatus();
    updateNotificationButton();
    updateChatButton();
    updateNavSolanaBalance();
    updateRepoPushButton();
    updateNavRebuildButton();
    updateNodeOnlineControls();
    return bar;
}

void MainWindow::updateNavRebuildButton()
{
    if (m_navRebuildButton)
        m_navRebuildButton->setVisible(
            QSettings().value(kShowRebuildButtonSetting, false).toBool());
}

// Screenshot button: drop a transparent overlay (the live desktop stays visible),
// let the user drag a dotted rectangle anywhere on the computer, then grab that
// region on release and save it to a temp PNG queued as the next attachment.
void MainWindow::captureScreenRegion()
{
    ScreenCaptureOverlay *overlay = ScreenCaptureOverlay::begin();
    if (!overlay) {
        logSystem("Couldn't grab the screen for a region screenshot.");
        return;
    }
    connect(overlay, &ScreenCaptureOverlay::captured, this,
            [this](const QImage &image) {
                auto *markup = new ScreenshotMarkupWindow(image, this);
                connect(markup, &ScreenshotMarkupWindow::imageAccepted, this,
                        [this](const QImage &annotated) {
                            const QString path = saveNewAgentPromptImage(annotated);
                            if (path.isEmpty()) {
                                logSystem("Couldn't save the screenshot.");
                                return;
                            }
                            queueQuickAddImage(path);
                            if (m_issueQuickAdd)
                                m_issueQuickAdd->setFocus();
                        });
                markup->show();
                markup->raise();
                markup->activateWindow();
            });
}

// Pencil button: drop a transparent overlay over the whole desktop (the live
// screen stays visible) that you can scribble on freehand with the pointer, to
// point things out on screen. Esc / right-click clears the ink and dismisses it.
// The overlay also carries a "Screenshot" button: clicking it grabs a region with
// the drawn ink baked in and queues it as the next attachment.
void MainWindow::startScreenDraw()
{
    ScreenDrawOverlay *overlay = ScreenDrawOverlay::begin();
    if (!overlay) {
        logSystem("Couldn't open the on-screen drawing overlay.");
        return;
    }
    // Let clicks on the nav screenshot button open the capture selector rather than draw.
    if (m_navScreenshotButton) {
        const QRect globalRect(m_navScreenshotButton->mapToGlobal(QPoint(0, 0)),
                               m_navScreenshotButton->size());
        overlay->setScreenshotHotzone(globalRect);
    }
    connect(overlay, &ScreenDrawOverlay::captured, this,
            [this](const QImage &image) {
                auto *markup = new ScreenshotMarkupWindow(image, this);
                connect(markup, &ScreenshotMarkupWindow::imageAccepted, this,
                        [this](const QImage &annotated) {
                            const QString path = saveNewAgentPromptImage(annotated);
                            if (path.isEmpty()) {
                                logSystem("Couldn't save the screenshot.");
                                return;
                            }
                            queueQuickAddImage(path);
                            if (m_issueQuickAdd)
                                m_issueQuickAdd->setFocus();
                        });
                markup->show();
                markup->raise();
                markup->activateWindow();
            });
}

void MainWindow::updateConnectionStatus()
{
    if (!m_connectionDot)
        return;

    // Connected when our own node shows a live link in the roster; the online
    // count includes every node currently online (ourselves included).
    bool selfOnline = false;
    int onlineCount = 0;
    for (const MemberInfo &member : std::as_const(m_homeRoster)) {
        if (member.online)
            ++onlineCount;
        if (member.self && member.online)
            selfOnline = true;
    }
    const bool connected = m_backend && selfOnline;

    QString color, text;
    if (connected) {
        color = "#3fb950"; // green
        text = QString::fromUtf8("Connected \xC2\xB7 %1 %2 online")
                   .arg(onlineCount)
                   .arg(onlineCount == 1 ? "node" : "nodes");
    } else if (m_backend) {
        color = "#d29922"; // amber: connecting / backing off
        text = QString::fromUtf8("Connecting\xE2\x80\xA6");
    } else {
        color = "#8b949e"; // grey: offline / not started
        text = QStringLiteral("Offline");
    }
    // The status text rides on the avatar tooltip; the dot itself just shows the
    // colour. (The count can change while the colour doesn't, so the tooltip is
    // always refreshed but the dot stylesheet is only rewritten on colour change.)
    if (m_avatarNavButton)
        m_avatarNavButton->setToolTip(
            QString::fromUtf8("%1 \xC2\xB7 your node profile").arg(text));
    if (color == m_connectionStatusColor)
        return;
    m_connectionStatusColor = color;
    // Only the fill + radius are set inline; the background-matching ring is
    // themed via the #connectionDot rule in Theme.h so it works in light mode too.
    m_connectionDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:6px;").arg(color));
}

// Flip this node online/offline from the top-bar toggle. "Offline" keeps the user
// in the app but stops the two things that earn rewards — the once-a-minute reward
// heartbeat and live repo serving — and folds the open session into the saved
// uptime total. "Online" resumes both and restarts the uptime clock. The choice is
// persisted so a node the user deliberately parked offline doesn't silently start
// collecting rewards again on the next launch.
void MainWindow::setNodeOffline(bool offline)
{
    if (offline == m_nodeOffline) {
        updateNodeOnlineControls();
        return;
    }
    m_nodeOffline = offline;
    QSettings().setValue(kNodeOfflineSetting, offline);

    if (offline) {
        // Stop the uptime clock and bank the elapsed session into the total.
        if (m_connectedAtMs > 0) {
            m_totalConnectionMs +=
                QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs;
            m_connectedAtMs = 0;
            QSettings().setValue(kConnectionTotalSetting, m_totalConnectionMs);
        }
        if (m_heartbeatTimer)
            m_heartbeatTimer->stop();
        stopRepoHosts();
        logSystem("Node taken offline \xE2\x80\x94 no longer serving repos or "
                  "collecting rewards.");
    } else {
        // Restart the uptime clock only if we are actually attached to a relay.
        if (m_backend && m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (m_backend) {
            startRepoHosts();
            if (!m_heartbeatTimer) {
                m_heartbeatTimer = new QTimer(this);
                m_heartbeatTimer->setInterval(60000);
                connect(m_heartbeatTimer, &QTimer::timeout, this,
                        &MainWindow::sendNodeHeartbeat);
            }
            m_heartbeatTimer->start();
            sendNodeHeartbeat();
        }
        logSystem("Node back online \xE2\x80\x94 serving repos and collecting "
                  "rewards.");
    }
    updateNodeOnlineControls();
    updateConnectionStatus();
}

void MainWindow::updateNodeOnlineControls()
{
    if (!m_nodeOnlineToggle)
        return;
    // Online means the user hasn't parked the node *and* a relay link exists; the
    // toggle reflects the user's intent even before the backend finishes attaching.
    const bool online = !m_nodeOffline;
    if (m_nodeOnlineToggle->isChecked() != online)
        m_nodeOnlineToggle->setChecked(online);
    m_nodeOnlineToggle->setToolTip(
        online ? QStringLiteral("This node is online and collecting rewards. "
                                "Click to take it offline.")
               : QStringLiteral("This node is offline and not collecting "
                                "rewards. Click to bring it back online."));

    if (m_nodeOnlineStatusLabel) {
        m_nodeOnlineStatusLabel->setText(online ? QStringLiteral("Online")
                                                : QStringLiteral("Offline"));
        m_nodeOnlineStatusLabel->setStyleSheet(
            online ? QStringLiteral("color:#3fb950; font-size:13px; font-weight:800;")
                   : QStringLiteral("color:#d29922; font-size:13px; font-weight:800;"));
    }

    if (m_nodeRewardStatus) {
        m_nodeRewardStatus->setText(online
                                        ? QStringLiteral("available for rewards")
                                        : QStringLiteral("offline \xC2\xB7 not "
                                                         "collecting rewards"));
        m_nodeRewardStatus->setStyleSheet(
            online ? QStringLiteral("color:#3fb950; font-size:10px; font-weight:700;")
                   : QStringLiteral("color:#d29922; font-size:10px; font-weight:700;"));
    }

    if (m_nodeUptimeLabel) {
        const qint64 sessionMs =
            m_connectedAtMs > 0
                ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs
                : 0;
        m_nodeUptimeLabel->setText(
            !online ? QStringLiteral("offline")
            : sessionMs > 0
                ? QStringLiteral("online %1").arg(formatDuration(sessionMs))
                : QString::fromUtf8("connecting\xE2\x80\xA6"));
    }
}

void MainWindow::updateBreadcrumb()
{
    // The active relay (favicon + domain) now lives in the relay switcher.
    updateRelaySwitcher();
    updateRepoPushButton();
    if (!m_breadcrumb)
        return;
    // The relay / node / repo switchers and the always-visible section nav (with
    // its checked button) already show the active location, so the old breadcrumb
    // trail is redundant. Keep the label hidden.
    m_breadcrumb->clear();
    m_breadcrumb->hide();
}

void MainWindow::updateRelaySwitcher()
{
    if (!m_relayMenuButton)
        return;
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);

    if (m_relayIconButton) {
        m_relayIconButton->setIcon(
            (m_activeServer >= 0 && m_activeServer < m_servers.size())
                ? QIcon(faviconFor(m_servers.at(m_activeServer)))
                : QIcon(letterFavicon(host.isEmpty() ? QStringLiteral("ForkMesh")
                                                     : host)));
    }
    if (m_relayOpenButton)
        m_relayOpenButton->setEnabled(!host.isEmpty());

    if (host.isEmpty())
        host = QStringLiteral("ForkMesh");
    // "domain ▾ count": the caret signals it drops down; the count is the
    // number of configured relays.
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    m_relayMenuButton->setText(host + "  " + caret + "  " +
                               QString::number(m_servers.size()));
}

// Measure the round-trip latency to the active relay and feed it to the radar
// readout. We GET the relay's lightweight /api/version endpoint (small JSON, no
// Durable-Object fan-out) and time the request; a transport error or timeout
// flips the radar to its red "offline" alert. Only one probe runs at a time.
void MainWindow::probeRelayLatency()
{
    if (!m_relayRadar || !m_networkAccess || m_relayProbeInFlight)
        return;
    auto *radar = static_cast<RelayRadarWidget *>(m_relayRadar);

    QUrl url = catalogApiUrl(); // same relay host, http(s) scheme
    if (!url.isValid() || url.host().isEmpty()) {
        radar->setUnreachable();
        return;
    }
    url.setPath(QStringLiteral("/api/version"));
    url.setQuery(QString());

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(10000); // a no-answer within 10s counts as down

    m_relayProbeInFlight = true;
    auto *clock = new QElapsedTimer;
    clock->start();
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, clock, radar] {
        const qint64 elapsed = clock->elapsed();
        delete clock;
        m_relayProbeInFlight = false;
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_relayProbeFailures = 0;
            radar->setLatency(static_cast<int>(elapsed));
            // A one-off slow sample (first request on a cold connection, a
            // momentary hiccup) paints the dish amber/red and then sits there
            // unchanged for up to a minute — not "live" at all. Once the
            // reading is elevated, keep re-probing on a short leash (same
            // idea as the offline fast-retry below) so the indicator either
            // confirms the slowdown or snaps back to green within a second or
            // two instead of lagging reality. But a link that's simply *far*
            // from the relay (a 300ms+ round-trip is normal from across an
            // ocean) would otherwise get re-probed every single second
            // forever — that's the "too many network requests" flood. So back
            // the confirm loop off exponentially (1s, 2s, 4s, ...) up to the
            // normal once-a-minute cadence, and reset the moment latency drops
            // back to healthy (adhoc #74).
            if (elapsed >= 300) {
                const int steps = qMin(m_relayProbeElevated++, 6);
                const qint64 delayMs = qMin<qint64>(1000LL << steps, 60 * 1000);
                QTimer::singleShot(static_cast<int>(delayMs), this,
                                   &MainWindow::probeRelayLatency);
            } else {
                m_relayProbeElevated = 0;
            }
        } else {
            // Drop any pooled keep-alive connection so the next probe dials a
            // fresh socket: otherwise QNetworkAccessManager can keep reusing a
            // now-dead connection and the radar never clears even after we're
            // back online.
            if (m_networkAccess)
                m_networkAccess->clearConnectionCache();
            // A single miss is usually just a stale keep-alive socket or a
            // momentary blip (very common for the first probe right after
            // launch, before the connection is warm) — not a real outage. Don't
            // flip the radar to red on the strength of one failure; re-probe
            // shortly on the now-clean connection and only declare "offline"
            // once a second consecutive probe also fails. This stops the dish
            // getting stranded on "offline" while we're genuinely online.
            // Exception: when the OS itself reports the machine has no network
            // at all, the outage is real — skip the grace period and show it
            // immediately (adhoc #41).
            const auto *netInfo = QNetworkInformation::instance();
            const bool osOffline =
                netInfo && netInfo->reachability() ==
                               QNetworkInformation::Reachability::Disconnected;
            if (++m_relayProbeFailures >= 2 || osOffline) {
                radar->setUnreachable();
                // While offline, re-probe on a short leash instead of waiting
                // out the minute timer, so the dish flips back within seconds
                // of the relay answering again (adhoc #41). But a relay that's
                // down for minutes/hours shouldn't get hammered every 3s the
                // whole time: back off exponentially (3s, 6s, 12s, ...) capped
                // at 5 minutes. initRelayReachabilityWatch still fires an
                // immediate probe the moment the OS reports the link back, so
                // real recoveries aren't delayed by the backoff.
                const int backoffSteps = qMax(0, m_relayProbeFailures - 2);
                const qint64 delayMs =
                    qMin<qint64>(3000LL << qMin(backoffSteps, 10), 5 * 60 * 1000);
                QTimer::singleShot(delayMs, this, &MainWindow::probeRelayLatency);
            } else {
                QTimer::singleShot(2500, this, &MainWindow::probeRelayLatency);
            }
        }
    });
}

// The once-a-minute probe alone makes the radar lag reality by up to a minute
// in both directions (adhoc #41). The OS already knows the instant the link
// drops or comes back, so subscribe to Qt's reachability signal: a
// Disconnected report flips the dish straight to red "offline", and any
// recovery fires an immediate probe so the green latency readout is back
// within one round-trip. Platforms without a reachability backend still get
// the fast offline re-probe loop in probeRelayLatency().
void MainWindow::initRelayReachabilityWatch()
{
    if (!QNetworkInformation::loadBackendByFeatures(
            QNetworkInformation::Feature::Reachability))
        return;
    connect(QNetworkInformation::instance(),
            &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability reachability) {
                if (!m_relayRadar)
                    return;
                if (reachability ==
                    QNetworkInformation::Reachability::Disconnected) {
                    // Definitive: no network interface is up. No point probing;
                    // mark the outage as established so a later probe failure
                    // doesn't get the one-blip grace period.
                    m_relayProbeFailures = 2;
                    static_cast<RelayRadarWidget *>(m_relayRadar)
                        ->setUnreachable();
                } else {
                    // Link is (possibly) back: confirm with a real probe right
                    // away. The radar stays red until the probe succeeds, so a
                    // half-up link never shows a false green.
                    probeRelayLatency();
                }
            });
}

void MainWindow::openServerWebsite(int index)
{
    if (index < 0 || index >= m_servers.size())
        return;
    // Open the relay's website in the system browser (ws/wss -> http/https).
    QUrl url(m_servers.at(index).url);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/"));
    url.setQuery(QString());
    url.setFragment(QString());
    if (url.isValid() && !url.host().isEmpty())
        QDesktopServices::openUrl(url);
}

void MainWindow::openRepositoryWebsite()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
    const QUrl url(repositoryWebUrl(repo));
    if (url.isValid() && !url.host().isEmpty())
        QDesktopServices::openUrl(url);
}

void MainWindow::showRelayMenu()
{
    if (!m_relayMenuButton)
        return;
    QMenu menu(this);

    // Header showing the relay count.
    QAction *header =
        menu.addAction(QStringLiteral("Relays (%1)").arg(formatCount(m_servers.size())));
    header->setEnabled(false);

    // Search box at the top; filters the relay list live.
    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search relays") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(240);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    // One checkable action per relay (active one checked).
    QList<QAction *> relayActions;
    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        QAction *act =
            menu.addAction(QIcon(faviconFor(server)), serverHost(server.url));
        act->setCheckable(true);
        act->setChecked(i == m_activeServer);
        connect(act, &QAction::triggered, this, [this, i] { switchToServer(i); });
        relayActions.append(act);
    }

    menu.addSeparator();
    QAction *addAct = menu.addAction(QStringLiteral("Add relay") +
                                     QString::fromUtf8("\xE2\x80\xA6"));
    connect(addAct, &QAction::triggered, this, &MainWindow::promptAddServer);

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [this, relayActions](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0;
                     i < relayActions.size() && i < m_servers.size(); ++i)
                    relayActions.at(i)->setVisible(
                        needle.isEmpty() ||
                        serverHost(m_servers.at(i).url).toLower().contains(needle));
            });
    // Focus the search box once the menu's event loop is running.
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_relayMenuButton->mapToGlobal(
        QPoint(0, m_relayMenuButton->height())));
}

// Public Solana JSON-RPC endpoints. Tried in order with fallback so the UI can
// still show a balance if one public endpoint is unavailable.
namespace {
const char *kSolanaRpcEndpoints[] = {
    "https://api.mainnet-beta.solana.com",
    "https://solana-rpc.publicnode.com",
};

bool isLikelySolanaAddress(const QString &address)
{
    static const QRegularExpression re(
        QStringLiteral("^[1-9A-HJ-NP-Za-km-z]{32,44}$"));
    return re.match(address.trimmed()).hasMatch();
}

QString formatSolanaBalance(qint64 lamports)
{
    return QStringLiteral("%1 SOL").arg(lamports / 1000000000.0, 0, 'f', 9);
}

// solanaDisplayCurrency() ("sol" | "usd" | "inr") is a shared helper declared in
// MainWindowInternal.h (used by both this view and the settings panel).

QString fiatCurrencySymbol(const QString &cur)
{
    return cur == QLatin1String("inr") ? QString::fromUtf8("\xE2\x82\xB9")
                                       : QStringLiteral("$");
}

QString formatFiatBalance(qint64 lamports, double rate, const QString &cur)
{
    const double value = (lamports / 1000000000.0) * rate;
    return QStringLiteral("%1%2 %3")
        .arg(fiatCurrencySymbol(cur))
        .arg(value, 0, 'f', 2)
        .arg(cur.toUpper());
}

QString lastSolanaBalanceSetting(const QString &address)
{
    return kSolanaLastBalanceSettingPrefix + address.trimmed();
}
}  // namespace

void MainWindow::updateNodeSwitcher()
{
    if (!m_nodeMenuButton)
        return;
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    const QString label =
        m_selectedNode.isEmpty() ? QStringLiteral("Nodes") : m_selectedNode;
    m_nodeMenuButton->setText(label + "  " + caret + "  " +
                              QString::number(m_nodeMenuEntries.size()));
    // Badge the button with the selected node's platform/online state.
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        if (e.name == m_selectedNode) {
            m_nodeMenuButton->setIcon(osBadgeIcon(e.platform, e.online, 16));
            return;
        }
    }
    m_nodeMenuButton->setIcon(QIcon());
}

void MainWindow::cycleNavSolanaCurrency()
{
    QSettings s;
    QString cur = s.value(kSolanaDisplayCurrencySetting).toString().toLower();
    if (cur.isEmpty())
        cur = s.value(kSolanaDisplayUsdSetting, false).toBool()
                  ? QStringLiteral("usd")
                  : QStringLiteral("sol");
    const QString next = cur == QLatin1String("sol")   ? QStringLiteral("usd")
                         : cur == QLatin1String("usd") ? QStringLiteral("inr")
                                                       : QStringLiteral("sol");
    s.setValue(kSolanaDisplayCurrencySetting, next);
    // Re-render from the cached balance/rate rather than re-querying the chain +
    // price API on every click — that re-querying is what made the figure stall
    // (rate-limited) after a few quick switches.
    renderNavSolanaBalance();
}

void MainWindow::updateNavSolanaBalance()
{
    if (m_navNodeName) {
        const QString name = accountNameFromInput(m_userName, QString());
        // Admins get a little crown next to their name. U+1F451 (👑).
        const QString crown = QString::fromUtf8(" \xF0\x9F\x91\x91");
        m_navNodeName->setText(m_isAdmin && !name.isEmpty() ? name + crown : name);
        m_navNodeName->setToolTip(m_isAdmin && !name.isEmpty() ? name + " (admin)" : name);
        m_navNodeName->setVisible(!name.isEmpty());
    }
    if (!m_navSolanaBalance)
        return;

    const QString addr = savedSolanaAddress();
    if (addr != m_navSolanaBalanceAddress)
        m_navSolanaLamports = -1; // address changed: cached balance no longer applies
    m_navSolanaBalanceAddress = addr;
    if (addr.isEmpty()) {
        m_navSolanaLamports = -1;
        m_navSolanaBalance->setText(QStringLiteral("SOL --"));
        m_navSolanaBalance->setToolTip("Add a Solana address to show this node's balance");
        return;
    }
    if (!isLikelySolanaAddress(addr)) {
        m_navSolanaLamports = -1;
        m_navSolanaBalance->setText(QStringLiteral("SOL invalid"));
        m_navSolanaBalance->setToolTip("Saved Solana address is invalid");
        return;
    }

    m_navSolanaBalance->setText(QStringLiteral("SOL ..."));
    m_navSolanaBalance->setToolTip(QStringLiteral("Checking this node's Solana balance"));
    queryNavSolanaBalance(addr, 0);
}

// Re-render the balance label from the cached lamports + fiat rate, without
// touching the network. Falls back to a full refresh when we don't have a
// cached balance yet, and to a single price fetch when the rate is stale.
void MainWindow::renderNavSolanaBalance()
{
    if (!m_navSolanaBalance)
        return;
    if (m_navSolanaLamports < 0 || m_navSolanaBalanceAddress.isEmpty()) {
        updateNavSolanaBalance(); // nothing cached yet — do the real fetch
        return;
    }
    const QString cur = solanaDisplayCurrency();
    const QString solBalance = formatSolanaBalance(m_navSolanaLamports);
    if (cur == QLatin1String("sol")) {
        m_navSolanaBalance->setText(solBalance);
        m_navSolanaBalance->setToolTip(
            QStringLiteral("This node's Solana balance: %1").arg(solBalance));
        return;
    }
    const auto it = m_navFiatRates.constFind(cur);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool fresh = it != m_navFiatRates.constEnd() && it->first > 0.0 &&
                       now - it->second < 5 * 60 * 1000; // 5-minute rate cache
    if (fresh) {
        const QString fiatBalance =
            formatFiatBalance(m_navSolanaLamports, it->first, cur);
        m_navSolanaBalance->setText(fiatBalance);
        m_navSolanaBalance->setToolTip(
            QStringLiteral("This node's balance: %1 (%2)")
                .arg(fiatBalance, solBalance));
        return;
    }
    // No fresh rate cached: show the SOL figure with a hint and fetch one rate.
    m_navSolanaBalance->setText(QStringLiteral("%1 ...").arg(fiatCurrencySymbol(cur)));
    m_navSolanaBalance->setToolTip(
        QStringLiteral("Checking SOL/%1 price for %2").arg(cur.toUpper(), solBalance));
    queryNavSolanaUsdPrice(m_navSolanaBalanceAddress, m_navSolanaLamports);
}

void MainWindow::queryNavSolanaBalance(const QString &addr, int endpointIndex)
{
    const int count = int(sizeof(kSolanaRpcEndpoints) / sizeof(kSolanaRpcEndpoints[0]));
    if (endpointIndex >= count) {
        if (m_navSolanaBalance && m_navSolanaBalanceAddress == addr) {
            m_navSolanaBalance->setText(QStringLiteral("SOL unavailable"));
            m_navSolanaBalance->setToolTip("Solana balance is temporarily unavailable");
        }
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kSolanaRpcEndpoints[endpointIndex])));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject body{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "getBalance"},
        {"params", QJsonArray{addr}},
    };
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, addr, endpointIndex]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (!m_navSolanaBalance || m_navSolanaBalanceAddress != addr)
            return;

        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject result = root.value("result").toObject();
        if (netError != QNetworkReply::NoError || !result.contains("value")) {
            queryNavSolanaBalance(addr, endpointIndex + 1);
            return;
        }
        const qint64 lamports = result.value("value").toVariant().toLongLong();
        m_navSolanaLamports = lamports; // cache so currency switches don't re-query
        QSettings settings;
        const QString lastBalanceKey = lastSolanaBalanceSetting(addr);
        const QVariant previousValue = settings.value(lastBalanceKey);
        const qint64 previousLamports = previousValue.toLongLong();
        const QString balance = formatSolanaBalance(lamports);
        if (previousValue.isValid() && lamports > previousLamports &&
            QSettings().value(kDisbursementAlertSetting, false).toBool()) {
            const QString amount = formatSolanaBalance(lamports - previousLamports);
            QApplication::alert(this, 0);
            postNotification(QStringLiteral("New disbursement received"),
                             QStringLiteral("%1 added to this node's wallet. "
                                            "New balance: %2")
                                 .arg(amount, balance),
                             false, QStringLiteral("emblem-default"));
        }
        settings.setValue(lastBalanceKey, QString::number(lamports));
        const QString cur = solanaDisplayCurrency();
        if (cur != QLatin1String("sol")) {
            m_navSolanaBalance->setText(
                QStringLiteral("%1 ...").arg(fiatCurrencySymbol(cur)));
            m_navSolanaBalance->setToolTip(
                QStringLiteral("Checking SOL/%1 price for %2")
                    .arg(cur.toUpper(), balance));
            queryNavSolanaUsdPrice(addr, lamports);
            return;
        }
        m_navSolanaBalance->setText(balance);
        m_navSolanaBalance->setToolTip(
            QStringLiteral("This node's Solana balance: %1").arg(balance));
    });
}

void MainWindow::queryNavSolanaUsdPrice(const QString &addr, qint64 lamports)
{
    const QString cur = solanaDisplayCurrency();
    if (cur == QLatin1String("sol"))
        return;
    QNetworkRequest request(QUrl(
        QStringLiteral("https://api.coingecko.com/api/v3/simple/price"
                       "?ids=solana&vs_currencies=%1").arg(cur)));
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, addr, lamports, cur]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (!m_navSolanaBalance || m_navSolanaBalanceAddress != addr ||
            solanaDisplayCurrency() != cur)
            return;

        const QString solBalance = formatSolanaBalance(lamports);
        const double rate =
            QJsonDocument::fromJson(raw).object()
                .value(QStringLiteral("solana")).toObject()
                .value(cur).toDouble();
        if (netError != QNetworkReply::NoError || rate <= 0.0) {
            m_navSolanaBalance->setText(solBalance);
            m_navSolanaBalance->setToolTip(
                QStringLiteral("SOL/%1 price unavailable. Balance: %2")
                    .arg(cur.toUpper(), solBalance));
            return;
        }

        m_navFiatRates[cur] = {rate, QDateTime::currentMSecsSinceEpoch()};
        const QString fiatBalance = formatFiatBalance(lamports, rate, cur);
        m_navSolanaBalance->setText(fiatBalance);
        m_navSolanaBalance->setToolTip(
            QStringLiteral("This node's balance: %1 (%2 at %3%4/SOL)")
                .arg(fiatBalance, solBalance, fiatCurrencySymbol(cur),
                     QString::number(rate, 'f', 2)));
    });
}

void MainWindow::showNodeMenu()
{
    if (!m_nodeMenuButton)
        return;
    QMenu menu(this);

    QAction *header =
        menu.addAction(QStringLiteral("Nodes (%1)").arg(formatCount(m_nodeMenuEntries.size())));
    header->setEnabled(false);

    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search nodes") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(240);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    if (m_nodeMenuEntries.isEmpty()) {
        QAction *empty = menu.addAction(QStringLiteral("No nodes yet"));
        empty->setEnabled(false);
    }

    QList<QAction *> nodeActions;
    QStringList nodeNames;
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        QString text = e.name;
        if (e.self)
            text += " (you)";
        text += QStringLiteral("   %1 repo%2")
                    .arg(e.repoCount)
                    .arg(e.repoCount == 1 ? "" : "s");
        QAction *act = menu.addAction(osBadgeIcon(e.platform, e.online, 16), text);
        act->setCheckable(true);
        act->setChecked(e.name == m_selectedNode);
        const QString node = e.name;
        connect(act, &QAction::triggered, this, [this, node] {
            selectNode(node);                 // fill the repositories column
            showNodeProfile(QString(), node); // and open the node's profile
        });
        nodeActions.append(act);
        nodeNames.append(e.name.toLower());
    }

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [nodeActions, nodeNames](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0; i < nodeActions.size(); ++i)
                    nodeActions.at(i)->setVisible(needle.isEmpty() ||
                                                  nodeNames.at(i).contains(needle));
            });
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_nodeMenuButton->mapToGlobal(
        QPoint(0, m_nodeMenuButton->height())));
}

void MainWindow::showNodesWindow()
{
    auto *dialog = new QDialog(this);
    dialog->setObjectName("nodesWindow");
    dialog->setWindowTitle(QStringLiteral("Network nodes"));
    dialog->setMinimumSize(560, 520);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);

    auto *heading = new QLabel;
    heading->setObjectName("nodesWindowHeading");
    heading->setTextFormat(Qt::RichText);
    outer->addWidget(heading);

    auto *scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *listHost = new QWidget;
    auto *listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(8);
    listLayout->addStretch();
    scroll->setWidget(listHost);
    outer->addWidget(scroll, 1);

    const bool dark = currentThemeIsDark();
    const QString cardBg = dark ? "#161b22" : "#ffffff";
    const QString cardBorder = dark ? "#30363d" : "#d0d7de";
    const QString subFg = dark ? "#8b949e" : "#57606a";

    // Rebuildable so a delete reflects immediately without reopening.
    auto populate = std::make_shared<std::function<void()>>();
    *populate = [this, listLayout, heading, dialog, cardBg, cardBorder, subFg,
                 populate] {
        // Clear existing cards (keep the trailing stretch at the end).
        while (listLayout->count() > 1) {
            QLayoutItem *item = listLayout->takeAt(0);
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }

        QList<MemberInfo> nodes = m_homeRoster;
        std::sort(nodes.begin(), nodes.end(), [](const MemberInfo &a,
                                                 const MemberInfo &b) {
            if (a.self != b.self)
                return a.self; // you first
            if (a.online != b.online)
                return a.online; // then online
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });

        int online = 0;
        for (const MemberInfo &m : nodes)
            if (m.online || m.self)
                ++online;
        heading->setText(
            QString::fromUtf8("<b>%1</b> node%2 \xC2\xB7 <span style='color:#3fb950'>"
                           "%3 online</span>")
                .arg(nodes.size())
                .arg(nodes.size() == 1 ? "" : "s")
                .arg(online));

        if (nodes.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral("No nodes on this relay yet."));
            empty->setStyleSheet(QStringLiteral("color:%1; padding:24px;").arg(subFg));
            empty->setAlignment(Qt::AlignCenter);
            listLayout->insertWidget(0, empty);
            return;
        }

        for (const MemberInfo &node : nodes) {
            auto *card = new QWidget;
            card->setObjectName("nodeCard");
            card->setStyleSheet(
                QStringLiteral("#nodeCard { background:%1; border:1px solid %2; "
                               "border-radius:10px; }")
                    .arg(cardBg, cardBorder));
            auto *row = new QHBoxLayout(card);
            row->setContentsMargins(12, 12, 12, 12);
            row->setSpacing(12);

            // Icon: real avatar if known, else a generated tile.
            QPixmap avatar = m_avatars.value(node.id);
            if (avatar.isNull())
                avatar = letterFavicon(node.name);
            auto *icon = new QLabel;
            icon->setPixmap(roundedRectPixmap(avatar, 48, 12));
            icon->setFixedSize(48, 48);
            row->addWidget(icon, 0, Qt::AlignTop);

            // Identity + every detail we hold about this node.
            auto *info = new QVBoxLayout;
            info->setSpacing(3);
            const bool isOnline = node.self ? (m_backend != nullptr) : node.online;
            auto *title = new QLabel(
                QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> "
                               "<b>%2</b>%3")
                    .arg(isOnline ? "#3fb950" : "#8b949e",
                         node.name.toHtmlEscaped(),
                         node.self ? " <span style='color:#8b949e'>(you)</span>"
                                   : QString()));
            title->setTextFormat(Qt::RichText);
            info->addWidget(title);

            QStringList lines;
            lines << QStringLiteral("Status: %1")
                         .arg(isOnline ? "Online" : "Offline");
            if (!node.platform.isEmpty())
                lines << QStringLiteral("Platform: %1").arg(node.platform.toHtmlEscaped());
            if (!node.version.isEmpty())
                lines << QStringLiteral("Version: %1").arg(node.version.toHtmlEscaped());
            lines << QStringLiteral("Earnings: %1")
                         .arg(node.solanaBalance.trimmed().isEmpty()
                                  ? QString::fromUtf8("\xE2\x80\x94")
                                  : node.solanaBalance.trimmed().toHtmlEscaped() +
                                        " SOL");
            if (!node.solanaAddress.trimmed().isEmpty())
                lines << QStringLiteral("Solana: %1")
                             .arg(node.solanaAddress.trimmed().toHtmlEscaped());
            lines << QStringLiteral("Mirrors: %1")
                         .arg(node.mirrors.isEmpty()
                                  ? QStringLiteral("none")
                                  : node.mirrors.join(", ").toHtmlEscaped());
            if (!node.id.isEmpty())
                lines << QStringLiteral("Node id: %1")
                             .arg(node.id.left(16).toHtmlEscaped() +
                                  (node.id.size() > 16 ? "\xE2\x80\xA6" : ""));
            if (!node.note.trimmed().isEmpty())
                lines << node.note.trimmed().toHtmlEscaped();

            auto *details = new QLabel(lines.join("<br>"));
            details->setTextFormat(Qt::RichText);
            details->setWordWrap(true);
            details->setStyleSheet(QStringLiteral("color:%1; font-size:12px;").arg(subFg));
            details->setTextInteractionFlags(Qt::TextSelectableByMouse);
            info->addWidget(details);
            row->addLayout(info, 1);

            // Per-node actions: open the profile, or forget the node.
            auto *actions = new QVBoxLayout;
            actions->setSpacing(6);
            auto *profileBtn = new QPushButton(QStringLiteral("Profile"));
            profileBtn->setCursor(Qt::PointingHandCursor);
            const QString nid = node.id;
            const QString nname = node.name;
            connect(profileBtn, &QPushButton::clicked, dialog, [this, nid, nname] {
                showNodeProfile(nid, nname);
            });
            actions->addWidget(profileBtn);
            if (!node.self && !node.id.isEmpty()) {
                auto *delBtn = new QPushButton(QStringLiteral("Delete"));
                delBtn->setCursor(Qt::PointingHandCursor);
                delBtn->setObjectName("dangerButton");
                connect(delBtn, &QPushButton::clicked, dialog,
                        [this, nid, nname, populate] {
                            if (QMessageBox::question(
                                    nullptr, QStringLiteral("Delete node"),
                                    QStringLiteral("Forget %1? It reappears if the "
                                                   "node announces itself again.")
                                        .arg(nname)) != QMessageBox::Yes)
                                return;
                            removeChatMember(nid, nname);
                            (*populate)();
                        });
                actions->addWidget(delBtn);
            }
            actions->addStretch();
            row->addLayout(actions, 0);

            listLayout->insertWidget(listLayout->count() - 1, card);
        }
    };
    (*populate)();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    outer->addWidget(buttons);

    dialog->show();
}

void MainWindow::updateRepoSwitcher()
{
    if (!m_repoMenuButton)
        return;
    // Mid node-switch: the repo list belongs to the node being loaded, so keep
    // the button visible with a "Loading…" label (the spinner icon is driven by
    // startRepoSwitchSpin) instead of revealing a count or repo name until the
    // switch completes.
    if (m_nodeSwitching) {
        m_repoMenuButton->setVisible(true);
        if (m_repoLabel)
            m_repoLabel->setVisible(true);
        m_repoMenuButton->setText(QString::fromUtf8("Loading\xE2\x80\xA6"));
        return;
    }
    // Nothing in the repo area when the selected node has no repos.
    const bool hasRepos = !m_repoMenuEntries.isEmpty();
    m_repoMenuButton->setVisible(hasRepos);
    // The Code button is primary section nav now, so it stays visible even with
    // no repos (it just lands on the empty Home view).
    if (m_repoLabel)
        m_repoLabel->setVisible(hasRepos);
    if (!hasRepos)
        return;
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    QString label = QStringLiteral("Repos");
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size())
        label = m_repositories.at(m_repoDetailIndex).name;
    m_repoMenuButton->setText(label + "  " + caret + "  " +
                              QString::number(m_repoMenuEntries.size()));
}

bool MainWindow::relayPublishRepo(const RepositoryRecord &repo,
                                  QString *localBranch, int *unpublished) const
{
    if (localBranch)
        localBranch->clear();
    if (unpublished)
        *unpublished = 0;
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return false;
    if (repo.mirrorPath.isEmpty() || !QDir(repo.mirrorPath).exists())
        return false;

    // The branch must track a remote whose URL resolves to the ForkMesh relay.
    QByteArray upstreamOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("--symbolic-full-name"),
                        QStringLiteral("@{upstream}")},
                       &upstreamOut, nullptr))
        return false;
    const QString upstream = QString::fromUtf8(upstreamOut).trimmed();
    const int slash = upstream.indexOf('/');
    if (slash <= 0)
        return false;
    QByteArray urlOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("remote"), QStringLiteral("get-url"),
                        upstream.left(slash)},
                       &urlOut, nullptr))
        return false;
    QString relayHost =
        serverHost(QSettings().value(kServerUrlSetting).toString().trimmed());
    if (relayHost.isEmpty())
        relayHost = serverHost(kDefaultServerUrl);
    const QString remoteUrl = QString::fromUtf8(urlOut).trimmed();
    const bool isRelay =
        !relayHost.isEmpty() && QUrl(remoteUrl).host() == relayHost;
    // Or the upstream is this repo's own served mirror (a local bare repo): then
    // "publish" must force-sync that mirror from the working copy via
    // syncRepository, never a raw `git push`. The mirror is also advanced by the
    // app's own background sync (and agents pushing branches into it), so a plain
    // push to its `main` races and gets "[remote rejected] main" (a ref-lock /
    // non-fast-forward).
    const bool isOwnMirror =
        !remoteUrl.isEmpty() && QUrl(remoteUrl).host().isEmpty() &&
        QDir(remoteUrl).absolutePath() == QDir(repo.mirrorPath).absolutePath();
    if (!isRelay && !isOwnMirror)
        return false;

    // Local branch + commits not yet folded into the served mirror. The mirror's
    // HEAD commit always exists in the working copy (the mirror is fetched from
    // it), so counting from there gives the unpublished commits.
    QByteArray branchOut;
    runGitCapture(repo.localPath,
                  {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                   QStringLiteral("HEAD")},
                  &branchOut, nullptr);
    if (localBranch)
        *localBranch = QString::fromUtf8(branchOut).trimmed();
    const QString mirrorCommit =
        mirrorBranchCommit(repo.mirrorPath, mirrorHeadBranch(repo.mirrorPath));
    QByteArray countOut;
    runGitCapture(repo.localPath,
                  mirrorCommit.isEmpty()
                      ? QStringList{QStringLiteral("rev-list"),
                                    QStringLiteral("--count"), QStringLiteral("HEAD")}
                      : QStringList{QStringLiteral("rev-list"),
                                    QStringLiteral("--count"),
                                    mirrorCommit + QStringLiteral("..HEAD")},
                  &countOut, nullptr);
    if (unpublished)
        *unpublished = QString::fromUtf8(countOut).trimmed().toInt();
    return true;
}

// Gather the rich-tooltip detail for a pending sync: the pending commits (subject
// + per-commit line diffstat, newest first, capped) and the aggregate +/- line
// counts over the whole range. `base` is the ref the pending commits are ahead of
// (a served-mirror commit, or @{upstream}); empty means "from the root commit".
// Shells git on the given path only, so it's safe on the worker thread.
void MainWindow::collectPushDetail(const QString &localPath, const QString &base,
                                   RepoPushState *st)
{
    if (!st || localPath.isEmpty())
        return;
    constexpr int kMax = 8; // cap the list so a big backlog can't blow up the tooltip
    const QString logRange =
        base.isEmpty() ? QStringLiteral("HEAD") : base + QStringLiteral("..HEAD");

    // One `git log --numstat` pass gives every pending commit's subject and its
    // added/removed lines. Records are split on RS (0x1e); within a record the
    // header line is "<hash>\x1f<subject>", followed by numstat rows.
    QByteArray logOut;
    if (runGitCapture(localPath,
                      {QStringLiteral("log"), logRange,
                       QStringLiteral("--max-count=%1").arg(kMax + 1),
                       QStringLiteral("--numstat"),
                       QStringLiteral("--format=%x1e%h%x1f%s")},
                      &logOut, nullptr)) {
        const QList<QByteArray> records = logOut.split('\x1e');
        for (const QByteArray &record : records) {
            if (record.trimmed().isEmpty())
                continue;
            if (st->commits.size() >= kMax) {
                st->extraCommits++;
                continue;
            }
            const int nl = record.indexOf('\n');
            const QByteArray head = nl >= 0 ? record.left(nl) : record;
            const int us = head.indexOf('\x1f');
            RepoPushState::PendingCommit c;
            c.hash = QString::fromUtf8(us >= 0 ? head.left(us) : head).trimmed();
            c.subject = QString::fromUtf8(us >= 0 ? head.mid(us + 1) : QByteArray());
            if (nl >= 0) {
                const QList<QByteArray> rows = record.mid(nl + 1).split('\n');
                for (const QByteArray &row : rows) {
                    const QList<QByteArray> cols = row.split('\t');
                    if (cols.size() < 2)
                        continue; // blank line, or a binary file's "-\t-\t"
                    bool okA = false, okR = false;
                    const int a = QString::fromUtf8(cols[0]).toInt(&okA);
                    const int r = QString::fromUtf8(cols[1]).toInt(&okR);
                    if (okA) c.added += a;
                    if (okR) c.removed += r;
                }
            }
            st->commits << c;
        }
    }

    // Aggregate +/- over the FULL range (including commits past the cap) so the
    // headline totals stay honest. `git diff --numstat A HEAD` collapses the whole
    // span; from the root, diff against the empty tree.
    static const QString kEmptyTree =
        QStringLiteral("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
    QByteArray diffOut;
    if (runGitCapture(localPath,
                      {QStringLiteral("diff"), QStringLiteral("--numstat"),
                       base.isEmpty() ? kEmptyTree : base, QStringLiteral("HEAD")},
                      &diffOut, nullptr)) {
        const QList<QByteArray> rows = diffOut.split('\n');
        for (const QByteArray &row : rows) {
            const QList<QByteArray> cols = row.split('\t');
            if (cols.size() < 2)
                continue;
            bool okA = false, okR = false;
            const int a = QString::fromUtf8(cols[0]).toInt(&okA);
            const int r = QString::fromUtf8(cols[1]).toInt(&okR);
            if (okA) st->added += a;
            if (okR) st->removed += r;
        }
    }
}

// Resolve every git-derived count the "Sync" button needs — the relay /
// upstream classification, unpublished/ahead/behind walks. Each is a rev-list /
// rev-parse subprocess on the working copy + served mirror, so this is the part
// that used to freeze the window on every commit/sync; it runs on a worker thread
// (see updateRepoPushButton). It reads only the passed-in record and free git
// helpers, never m_repositories or a widget, so it is safe off the GUI thread.
MainWindow::RepoPushState
MainWindow::computeRepoPushState(const RepositoryRecord &repo) const
{
    RepoPushState st;
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return st; // st.valid stays false
    st.valid = true;

    // Commits we're behind by (incoming, to pull) — from the served mirror for a
    // relay repo, else the configured upstream. Used to flag a two-way sync.
    auto behindCount = [](const RepositoryRecord &r) -> int {
        QString ref;
        if (!r.mirrorPath.isEmpty())
            ref = mirrorBranchCommit(r.mirrorPath, mirrorHeadBranch(r.mirrorPath));
        else {
            QByteArray u;
            if (runGitCapture(r.localPath,
                              {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                               QStringLiteral("--symbolic-full-name"),
                               QStringLiteral("@{upstream}")},
                              &u, nullptr))
                ref = QString::fromUtf8(u).trimmed();
        }
        if (ref.isEmpty())
            return 0;
        QByteArray b;
        if (!runGitCapture(r.localPath,
                           {QStringLiteral("rev-list"), QStringLiteral("--count"),
                            QStringLiteral("HEAD..%1").arg(ref)},
                           &b, nullptr))
            return 0;
        return QString::fromUtf8(b).trimmed().toInt();
    };

    // ForkMesh relay-backed repo: the relay has no git-receive-pack, so publish
    // local commits by syncing the served mirror from this working copy.
    QString relayBranch;
    int unpublished = 0;
    if (relayPublishRepo(repo, &relayBranch, &unpublished)) {
        st.relay = true;
        st.unpublished = unpublished;
        st.behind = behindCount(repo);
        st.target = QStringLiteral("your served mirror");
        if (unpublished > 0)
            collectPushDetail(repo.localPath,
                              mirrorBranchCommit(repo.mirrorPath,
                                                 mirrorHeadBranch(repo.mirrorPath)),
                              &st);
        return st;
    }

    QByteArray upstreamOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                        QStringLiteral("--symbolic-full-name"),
                        QStringLiteral("@{upstream}")},
                       &upstreamOut, nullptr))
        return st;
    const QString upstream = QString::fromUtf8(upstreamOut).trimmed();
    if (upstream.isEmpty())
        return st;
    st.hasUpstream = true;
    st.upstreamRef = upstream;

    QByteArray countOut;
    if (!runGitCapture(repo.localPath,
                       {QStringLiteral("rev-list"), QStringLiteral("--count"),
                        QStringLiteral("@{upstream}..HEAD")},
                       &countOut, nullptr))
        return st;
    st.ahead = QString::fromUtf8(countOut).trimmed().toInt();
    st.behind = behindCount(repo);
    st.target = upstream;
    if (st.ahead > 0)
        collectPushDetail(repo.localPath, upstream, &st);
    return st;
}

// Paint the "Sync" button from an already-computed RepoPushState (no git).
// Runs on the GUI thread, reads the live m_pushingRepos/m_syncingRepos membership
// so a Sync click flips it to "Syncing…" the instant the click marks the
// repo, and hides the button whenever there's nothing pending.
void MainWindow::applyRepoPushButtonState(int index, const RepoPushState &state)
{
    if (!m_repoPushButton)
        return;
    auto hideButton = [this] {
        m_repoPushButton->hide();
        m_repoPushButton->setEnabled(false);
        if (m_repoPushEyeButton)
            m_repoPushEyeButton->hide();
        if (m_repoPushTimer)
            m_repoPushTimer->stop(); // hidden: no need to keep repositioning it
        if (m_repoPublishBar)
            m_repoPublishBar->hide();
    };
    // Reveal the floating sync button positioned just above the Code tab. As an
    // overlay (not a laid-out widget) it never reflows the page underneath — even
    // while a mirror picks up a push on the Mirror nodes screen. A modest timer
    // keeps it pinned over the tab as the window resizes or tabs reflow.
    auto reveal = [this] {
        positionRepoPushButton(); // reparents to the page + anchors over Code
        m_repoPushButton->show();
        m_repoPushButton->raise();
        if (m_repoPushEyeButton) {
            m_repoPushEyeButton->show();
            m_repoPushEyeButton->raise();
        }
        if (m_repoPublishBar)
            m_repoPublishBar->show();
        if (!m_repoPushTimer) {
            m_repoPushTimer = new QTimer(this);
            connect(m_repoPushTimer, &QTimer::timeout, this,
                    &MainWindow::positionRepoPushButton);
        }
        if (!m_repoPushTimer->isActive())
            m_repoPushTimer->start(300);
    };
    // Outgoing (↑), incoming (↓), or both at once (⇅). The double-headed arrow is
    // how the button shows it's syncing both ways.
    auto arrow = [](int out, int in) -> QString {
        if (out > 0 && in > 0) return QString::fromUtf8(" \xE2\x87\x85"); // ⇅
        if (in > 0) return QString::fromUtf8(" \xE2\x86\x93");            // ↓
        return QString::fromUtf8(" \xE2\x86\x91");                        // ↑
    };
    // A rich (HTML) tooltip that answers "what am I about to sync, and where to?":
    // a one-line summary (count + destination + aggregate ± lines), then the pending
    // commits with their per-commit line-change markers. `count` is the true pending
    // total; state.commits is the capped, detail-bearing subset.
    auto minus = QString::fromUtf8("\xE2\x88\x92"); // U+2212 minus (matches diff UI)
    auto detailTip = [&minus](int count, const QString &fromRepo,
                              const RepoPushState &s) -> QString {
        const QString headline =
            QStringLiteral("Sync <b>%1</b> commit%2 from %3 to <b>%4</b>")
                .arg(count)
                .arg(count == 1 ? QString() : QStringLiteral("s"),
                     fromRepo.toHtmlEscaped(),
                     (s.target.isEmpty() ? QStringLiteral("your served mirror")
                                         : s.target).toHtmlEscaped());
        QString html = QStringLiteral("<div style='white-space:nowrap'>%1")
                           .arg(headline);
        if (s.behind > 0)
            html += QStringLiteral(
                        " <span style='color:#8b949e'>(and pull %1 incoming)</span>")
                        .arg(s.behind);
        if (s.added > 0 || s.removed > 0)
            html += QStringLiteral(
                        " &nbsp;<span style='color:#3fb950'>+%1</span> "
                        "<span style='color:#f85149'>%2%3</span>")
                        .arg(s.added).arg(minus).arg(s.removed);
        html += QStringLiteral("</div>");
        if (!s.commits.isEmpty()) {
            html += QStringLiteral("<table cellspacing='0' cellpadding='0' "
                                   "style='margin-top:4px'>");
            for (const RepoPushState::PendingCommit &c : s.commits)
                html += QStringLiteral(
                            "<tr>"
                            "<td style='color:#8b949e;padding-right:8px'><code>%1</code></td>"
                            "<td style='color:#3fb950;padding-right:4px'>+%2</td>"
                            "<td style='color:#f85149;padding-right:8px'>%3%4</td>"
                            "<td style='white-space:nowrap'>%5</td>"
                            "</tr>")
                            .arg(c.hash.toHtmlEscaped())
                            .arg(c.added)
                            .arg(minus).arg(c.removed)
                            .arg(c.subject.toHtmlEscaped());
            html += QStringLiteral("</table>");
            if (s.extraCommits > 0)
                html += QStringLiteral(
                            "<div style='color:#8b949e;margin-top:2px'>"
                            "+%1 more commit%2</div>")
                            .arg(s.extraCommits)
                            .arg(s.extraCommits == 1 ? QString() : QStringLiteral("s"));
        }
        return html;
    };

    if (index < 0 || index >= m_repositories.size() || !state.valid) {
        hideButton();
        return;
    }
    const RepositoryRecord &repo = m_repositories.at(index);
    const int behind = state.behind;

    if (m_pushingRepos.contains(index)) {
        setOcticon(m_repoPushButton, "sync", 14);
        m_repoPushButton->setText(QString::fromUtf8("Syncing")
                                  + arrow(1, behind) + QString::fromUtf8("\xE2\x80\xA6"));
        m_repoPushButton->setToolTip(
            behind > 0
                ? QStringLiteral("Syncing both ways: pushing local commits and "
                                 "pulling %1 incoming").arg(behind)
                : QStringLiteral("Syncing local commits upstream"));
        reveal();
        return;
    }

    if (state.relay) {
        const int unpublished = state.unpublished;
        if (m_syncingRepos.contains(index)) {
            setOcticon(m_repoPushButton, "sync", 14);
            m_repoPushButton->setText(QString::fromUtf8("Syncing")
                                      + arrow(qMax(unpublished, 1), behind)
                                      + QString::fromUtf8("\xE2\x80\xA6"));
            m_repoPushButton->setToolTip(
                behind > 0
                    ? QStringLiteral("Syncing both ways: publishing to your served "
                                     "mirror and pulling %1 incoming").arg(behind)
                    : QStringLiteral("Publishing local commits to your served mirror"));
            reveal();
            return;
        }
        if (unpublished <= 0) {
            hideButton();
            return;
        }
        setOcticon(m_repoPushButton, "sync", 14);
        // Surface the pending count right on the button — a small number above the
        // Commits tab — instead of hiding it in the tooltip (issue #208).
        m_repoPushButton->setText(QStringLiteral("Sync (%1)").arg(unpublished)
                                  + arrow(unpublished, behind));
        m_repoPushButton->setToolTip(detailTip(
            unpublished, QStringLiteral("%1/%2").arg(repo.owner, repo.name), state));
        m_repoPushButton->setEnabled(true);
        reveal();
        return;
    }

    if (!state.hasUpstream || state.ahead <= 0) {
        hideButton();
        return;
    }
    const int ahead = state.ahead;

    // A repo with a real upstream remote (origin/main, …). "Sync" with a
    // direction arrow — ⇅ when there's also incoming to pull. The count and target
    // move to the tooltip; pushCurrentRepoUpstream still does the push.
    setOcticon(m_repoPushButton, "sync", 14);
    // Show the pending commit count on the button itself (issue #208).
    m_repoPushButton->setText(QStringLiteral("Sync (%1)").arg(ahead)
                              + arrow(ahead, behind));
    m_repoPushButton->setToolTip(detailTip(
        ahead, QStringLiteral("%1/%2").arg(repo.owner, repo.name), state));
    m_repoPushButton->setEnabled(true);
    reveal();
}

void MainWindow::updateRepoPushButton()
{
    if (!m_repoPushButton)
        return;
    // This runs whenever the push state may have changed (a new local commit, a
    // completed publish/sync). If the commit list is on screen, keep its "waiting
    // to sync" markers in step so they appear/clear without a manual refresh.
    refreshCommitMarkersIfStale();

    const int index = m_repoDetailIndex;
    // Paint immediately from the last computed state so a Sync/Syncing click flips
    // the button without waiting on git; applyRepoPushButtonState reads the live
    // m_pushingRepos/m_syncingRepos membership. A stale cache is corrected the
    // moment the worker below finishes.
    applyRepoPushButtonState(index,
                             m_pushStateIndex == index ? m_pushState : RepoPushState{});

    if (index < 0 || index >= m_repositories.size())
        return;
    const RepositoryRecord &repo = m_repositories.at(index);
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return;

    // Recompute the git-derived counts off the GUI thread. These shell several
    // rev-list/rev-parse subprocesses on the working copy + served mirror, and this
    // is called after every commit (issue/comment), every sync start/finish, and on
    // the 60s home-stats timer — running them here froze the window each time. The
    // worker only reads path strings (computeRepoPushState touches no member state);
    // the result is applied back on the main thread. Coalesce while one is in flight
    // so a burst of calls runs at most one extra recompute.
    if (m_pushStateInFlight) {
        m_pushStatePending = true;
        return;
    }
    m_pushStateInFlight = true;
    const RepositoryRecord repoCopy = repo;
    auto result = std::make_shared<RepoPushState>();
    QThread *worker = QThread::create(
        [this, repoCopy, result] { *result = computeRepoPushState(repoCopy); });
    connect(worker, &QThread::finished, this, [this, worker, index, result] {
        worker->deleteLater();
        m_pushStateInFlight = false;
        m_pushState = *result;
        m_pushStateIndex = index;
        // Repaint only if the open repo is still the one we computed for.
        if (index == m_repoDetailIndex)
            applyRepoPushButtonState(index, *result);
        // A request that arrived mid-flight (e.g. the sync we kicked has since
        // finished) gets one fresh recompute now.
        if (m_pushStatePending) {
            m_pushStatePending = false;
            updateRepoPushButton();
        }
    });
    worker->start();
}

// Canonicalize and hash the stdout of `git for-each-ref
// --format=%(objectname) %(refname) refs/heads/ refs/tags/` into the sha256 the
// relay pins. This MUST stay byte-for-byte identical to the worker's
// advertised_refs_canonical() so the relay's integrity pin matches what we sign
// (the worker rejects every clone whose live refs don't hash to the pin).
static QString hashForEachRefOutput(const QByteArray &out)
{
    QStringList lines;
    const QStringList rows = QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
    for (const QString &raw : rows) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.endsWith(QStringLiteral("^{}")))
            continue;
        lines.append(line);
    }
    lines.sort();
    return QString::fromUtf8(
        QCryptographicHash::hash(lines.join('\n').toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}

// sha256 over the canonical heads+tags advertisement of a bare mirror (see
// hashForEachRefOutput). Synchronous; refreshRepoPinBanner runs the same git
// command asynchronously to avoid blocking the UI thread.
QString MainWindow::mirrorStateHash(const QString &mirrorPath) const
{
    if (mirrorPath.trimmed().isEmpty())
        return QString();
    QByteArray out;
    if (!runGitCapture(mirrorPath,
                       {"for-each-ref", "--format=%(objectname) %(refname)",
                        "refs/heads/", "refs/tags/"},
                       &out, nullptr))
        return QString();
    return hashForEachRefOutput(out);
}

// Surface the relay's tamper/rollback gate to the owner: when the pinned
// stateHash no longer matches the refs this node serves, every clone is rejected
// with "repository failed integrity check". Only the owning, publishing node can
// fix it (the relay verifies the maintainer key on the re-attestation), so the
// warning — and its "Reset integrity pin" action — only surfaces there. It is
// shown in the top-bar notification toast (see showPinWarning), not an in-page banner.
void MainWindow::refreshRepoPinBanner()
{
    if (!m_topMessage)
        return;
    dismissPinWarning();
    m_repoPinCheckIndex = -1;
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const RepositoryRecord repo = m_repositories.at(m_repoDetailIndex);
    // Nothing to attest unless we publish a real served mirror of this repo…
    if (!repo.publishToNetwork || repo.previewOnly ||
        repo.mirrorPath.trimmed().isEmpty())
        return;
    // …only the owner key holder can overwrite the pin…
    if (!m_profileIdentity.isValid() || catalogOwner(repo) != accountOwner())
        return;
    // …and the integrity pin is the source of truth's concern alone: only the
    // node that holds the working copy can (and should) re-attest it. A node that
    // merely mirrors this repo — even one on the owner's own account — must never
    // surface the banner; a stale pin on a mirror is the source of truth's problem
    // to see (see loadMirrorNodesPanel), not the mirror's.
    if (!repoHasWorkingTree())
        return;

    const int index = m_repoDetailIndex;
    const QString owner = catalogOwner(repo);
    const QString name = repoSegment(repo.name, QStringLiteral("repository"));
    const QString mirrorPath = repo.mirrorPath;
    m_repoPinCheckIndex = index;

    // Hash the live refs off the UI thread. `git for-each-ref` shells out and can
    // stall on a large or busy mirror, and this fires every time a repo detail
    // opens (and after every sync/reset); doing it synchronously froze the window.
    // Drive the subprocess through the event loop, then compare against the relay's
    // pin with the same async catalog fetch as before.
    QProcess *git = new QProcess(this);
    connect(git, &QProcess::errorOccurred, this,
            [this, git, index](QProcess::ProcessError e) {
                // Only FailedToStart skips finished(); every other error still
                // emits finished(), which owns the cleanup below.
                if (e != QProcess::FailedToStart)
                    return;
                git->deleteLater();
                if (m_repoPinCheckIndex == index)
                    m_repoPinCheckIndex = -1;
            });
    connect(git, &QProcess::finished, this,
            [this, git, index, owner, name](int code, QProcess::ExitStatus status) {
                git->deleteLater();
                // The user may have switched repos while git was running.
                if (!m_topMessage || m_repoPinCheckIndex != index ||
                    m_repoDetailIndex != index)
                    return;
                if (status != QProcess::NormalExit || code != 0)
                    return;
                const QString localHash =
                    hashForEachRefOutput(git->readAllStandardOutput());
                if (localHash.isEmpty())
                    return;

                QNetworkReply *reply =
                    m_networkAccess->get(QNetworkRequest(catalogListUrl()));
                connect(reply, &QNetworkReply::finished, this,
                        [this, reply, index, owner, name, localHash] {
                            reply->deleteLater();
                            // The user may have switched repos in flight.
                            if (!m_topMessage || m_repoPinCheckIndex != index ||
                                m_repoDetailIndex != index)
                                return;
                            const QJsonArray repos =
                                QJsonDocument::fromJson(reply->readAll())
                                    .object()
                                    .value("repositories")
                                    .toArray();
                            QString pinned;
                            bool found = false;
                            for (const QJsonValue &v : repos) {
                                const QJsonObject o = v.toObject();
                                if (o.value("owner").toString() == owner &&
                                    o.value("name").toString() == name) {
                                    pinned = o.value("stateHash").toString();
                                    found = true;
                                    break;
                                }
                            }
                            // Only a non-empty pin that disagrees with our live
                            // refs blocks clones. An absent pin fails open on the
                            // relay (nothing to fix), a matching pin is healthy.
                            if (found && !pinned.isEmpty() && pinned != localHash)
                                showPinWarning();
                        });
            });
    git->start("git", QStringList{"-C", mirrorPath, "for-each-ref",
                                  "--format=%(objectname) %(refname)",
                                  "refs/heads/", "refs/tags/"});
}

// Auto-heal the integrity pin across ALL of this node's source-of-truth repos,
// not just the one whose detail is open (refreshRepoPinBanner) or reset by hand
// (resetRepoPin). Only the node holding the working copy can re-sign the pin, so
// when a source repo's served refs drift past its published pin — a direct git
// op on the mirror, a sync that landed without a re-publish, a dropped publish —
// every clone of it is rejected until the owner happens to open that repo and
// click "Reset integrity pin". This closes that gap: while the source is online,
// it keeps its own pins in step automatically. Security is unchanged — we only
// re-attest OUR OWN authentic served refs (the same signing every publish does),
// so mirrors are still validated against a pin the source signed; when the source
// is offline this never runs, the pin freezes, and the relay's tamper gate keeps
// protecting clones against a stale or forged mirror exactly as before.
void MainWindow::reattestStalePins()
{
    if (!m_networkAccess || !hasActiveAccountSession())
        return;
    if (!m_profileIdentity.isValid() && !m_profileIdentity.load())
        return;

    // The repos we are the source of truth for: a published, served mirror we own
    // AND hold the working copy for. Same gate as refreshRepoPinBanner, applied to
    // every repo rather than the open one. A pure mirror never attests — keeping
    // its pin in step is its own source's job, and it lacks the owner key anyway.
    struct SourceRepo {
        int index;
        QString cowner;
        QString name;
        QString mirrorPath;
    };
    QVector<SourceRepo> candidates;
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.previewOnly || !repo.publishToNetwork ||
            repo.mirrorPath.trimmed().isEmpty())
            continue;
        if (repo.localPath.trimmed().isEmpty() ||
            !QDir(repo.localPath).exists(QStringLiteral(".git")))
            continue;
        if (catalogOwner(repo) != accountOwner())
            continue;
        candidates.append({i, catalogOwner(repo),
                           repoSegment(repo.name, QStringLiteral("repository")),
                           repo.mirrorPath});
    }
    if (candidates.isEmpty())
        return;

    // Hash each served ref set off the GUI thread (git for-each-ref shells out per
    // repo — mirrorStateHash notes it must not run synchronously on the UI thread),
    // then compare against the relay's published pins with a single catalog fetch
    // and re-attest only the drifted ones.
    runOffThread<QHash<int, QString>>(
        [candidates] {
            QHash<int, QString> hashes;
            for (const SourceRepo &c : candidates) {
                QByteArray out;
                if (runGitCapture(c.mirrorPath,
                                  {QStringLiteral("for-each-ref"),
                                   QStringLiteral("--format=%(objectname) %(refname)"),
                                   QStringLiteral("refs/heads/"),
                                   QStringLiteral("refs/tags/")},
                                  &out, nullptr))
                    hashes.insert(c.index, hashForEachRefOutput(out));
            }
            return hashes;
        },
        [this, candidates](QHash<int, QString> hashes) {
            QNetworkReply *reply =
                m_networkAccess->get(QNetworkRequest(catalogListUrl()));
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, candidates, hashes] {
                        reply->deleteLater();
                        const QJsonArray repos =
                            QJsonDocument::fromJson(reply->readAll())
                                .object()
                                .value(QStringLiteral("repositories"))
                                .toArray();
                        bool touchedOpen = false;
                        for (const SourceRepo &c : candidates) {
                            const QString localHash = hashes.value(c.index);
                            if (localHash.isEmpty())
                                continue; // for-each-ref failed; nothing to compare
                            // The list order can shift between the async hops, so
                            // confirm this index still points at the same repo.
                            if (c.index < 0 || c.index >= m_repositories.size())
                                continue;
                            const RepositoryRecord &repo = m_repositories.at(c.index);
                            if (repo.previewOnly ||
                                catalogOwner(repo) != c.cowner ||
                                repoSegment(repo.name,
                                            QStringLiteral("repository")) != c.name)
                                continue;
                            QString pinned;
                            bool found = false;
                            for (const QJsonValue &v : repos) {
                                const QJsonObject o = v.toObject();
                                if (o.value(QStringLiteral("owner")).toString() ==
                                        c.cowner &&
                                    o.value(QStringLiteral("name")).toString() ==
                                        c.name) {
                                    pinned = o.value(QStringLiteral("stateHash"))
                                                 .toString();
                                    found = true;
                                    break;
                                }
                            }
                            // Only a non-empty pin that disagrees with our live refs
                            // blocks clones; an absent pin fails open on the relay.
                            if (!found || pinned.isEmpty() || pinned == localHash)
                                continue;
                            logSystem(
                                QStringLiteral("Integrity pin: served refs of %1/%2 "
                                               "drifted past the relay's pin; "
                                               "re-attesting automatically.")
                                    .arg(c.cowner, c.name));
                            publishRepository(c.index, false);
                            if (c.index == m_repoDetailIndex)
                                touchedOpen = true;
                        }
                        // A re-attest of the open repo makes its warning toast stale;
                        // re-check once the signed write has had a moment to land.
                        if (touchedOpen)
                            QTimer::singleShot(1500, this, [this] {
                                refreshRepoPinBanner();
                            });
                    });
        });
}

// Show the integrity-pin warning as a persistent top-bar toast. Mirrors the error
// branch of flashMessage (red, stays up with Copy / dismiss affordances) but the
// label carries the "Reset integrity pin" and "Why?" actions as inline links,
// routed by the linkActivated handler wired in the constructor.
void MainWindow::showPinWarning()
{
    if (!m_topMessage)
        return;
    m_loadStatusShowing = false;
    m_pinWarningActive = true;
    m_topMessageRaw = QStringLiteral(
        "Clones of this repo are being rejected - the relay's integrity pin no longer "
        "matches the refs this node serves. Reset the integrity pin to fix it.");
    logSystem(m_topMessageRaw);
    // Byte-escaped glyphs (✕, ·) must go through fromUtf8, not QStringLiteral, or they
    // render as mojibake (each byte becomes its own char16_t).
    m_topMessage->setText(QString::fromUtf8(
        "<span style='color:#f85149'>\xE2\x9C\x95" " <b>Clones of this repo are being "
        "rejected.</b> The integrity pin no longer matches the refs this node "
        "serves. </span>"
        "<a href='fm:resetpin' style='color:#58a6ff;text-decoration:none'>Reset "
        "integrity pin</a>"
        "<span style='color:#f85149'> \xC2\xB7" " </span>"
        "<a href='fm:whypin' style='color:#58a6ff;text-decoration:none'>Why?</a>"));
    // The warning carries its own inline links, so it isn't an expandable toast.
    m_topMessageElided = false;
    m_topMessageExpanded = false;
    m_topMessage->setWordWrap(false);
    m_topMessage->show();
    // Persistent like an error toast: no auto-timeout, dismissible via Copy / ✕.
    if (m_topMessageTimer)
        m_topMessageTimer->stop();
    if (m_topMessageOverlay)
        m_topMessageOverlay->hide(); // drop any leftover expanded panel
    if (m_topMessageExpand)
        m_topMessageExpand->hide();
    if (m_topMessageCopy)
        m_topMessageCopy->show();
    if (m_topMessageClose)
        m_topMessageClose->show();
}

void MainWindow::dismissPinWarning()
{
    if (m_pinWarningActive)
        dismissTopMessage();
}

// Re-publish the open repo's catalog record, which re-signs the CURRENT mirror
// stateHash and overwrites the stale pin so the relay serves clones again.
void MainWindow::resetRepoPin()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
        return;
    const int index = m_repoDetailIndex;
    const RepositoryRecord &repo = m_repositories.at(index);
    logSystem("Integrity pin: re-attesting current refs for " + repo.owner + "/" +
              repo.name + ".");
    flashMessage(QStringLiteral("Re-attesting the integrity pin…"));
    publishRepository(index, true);
    // Give the signed write a moment to land, then re-check: refreshRepoPinBanner
    // clears the warning toast if the pin now matches, or re-shows it if not.
    QTimer::singleShot(1500, this, [this, index] {
        if (m_repoDetailIndex == index)
            refreshRepoPinBanner();
    });
}

void MainWindow::showPinExplanation()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Repository integrity pin"));
    box.setText(QStringLiteral(
        "ForkMesh pins an owner-signed fingerprint of the branches and tags your "
        "node serves."));
    box.setInformativeText(QStringLiteral(
        "When you publish, your node signs a hash of its current refs and the "
        "relay pins it. The relay then refuses to serve any mirror whose live "
        "refs don't hash to that pin — this is what stops a tampered or rolled-"
        "back mirror from ever being cloned.\n\n"
        "If the refs you serve have moved on (new commits, branches, or tags) but "
        "the pinned hash wasn't refreshed, the relay rejects every clone with "
        "\"repository failed integrity check\".\n\n"
        "As the owner you are the source of truth: \"Reset integrity pin\" re-"
        "signs the refs you currently serve and overwrites the stale pin, so "
        "clones work again."));
    box.exec();
}

void MainWindow::pushCurrentRepoUpstream()
{
    if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size() ||
        m_pushingRepos.contains(m_repoDetailIndex))
        return;

    const int index = m_repoDetailIndex;
    const RepositoryRecord repo = m_repositories.at(index);
    if (repo.localPath.isEmpty() || !QDir(repo.localPath).exists(".git"))
        return;

    // ForkMesh relay-backed repo: the relay serves clone/fetch only (no
    // git-receive-pack), so a `git push` to it 404s ("repository not found").
    // Publish instead by syncing the served mirror from this working copy; the
    // host then serves the new commits and peers fetch them.
    const bool isRelay = relayPublishRepo(repo, nullptr, nullptr);

    // Determine the upstream ref before scanning so we can diff only the
    // commits being pushed (more precise than scanning all tracked files).
    QString upstream;
    if (!isRelay) {
        QByteArray upstreamOut;
        if (!runGitCapture(repo.localPath,
                           {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                            QStringLiteral("--symbolic-full-name"),
                            QStringLiteral("@{upstream}")},
                           &upstreamOut, nullptr)) {
            flashMessage(QStringLiteral("No upstream branch is configured for %1/%2.")
                             .arg(repo.owner, repo.name),
                         true);
            updateRepoPushButton();
            return;
        }
        upstream = QString::fromUtf8(upstreamOut).trimmed();
    }

    // The secret scan walks the pushed commits (or every tracked file for a relay
    // repo) and the ahead-count runs rev-list — both are git reads heavy enough to
    // freeze the GUI thread on a large repo (StallWatchdog's top push offender,
    // issue #353). Run them off-thread and resume on the GUI thread for the
    // (possibly modal) result. Mark the repo "pushing" now so the button flips to
    // its busy state and the entry guard blocks a second click during the scan.
    m_pushingRepos.insert(index);
    updateRepoPushButton();

    struct PushScan {
        QList<RepoSecurityFinding> findings;
        int ahead = 0;
    };
    const bool scanEnabled = repo.secretScanningEnabled;
    const QString localPath = repo.localPath;
    runOffThread<PushScan>(
        [scanEnabled, localPath, upstream, isRelay]() {
            PushScan scan;
            if (scanEnabled)
                scan.findings = RepoSecurity::findSecretsInPush(localPath, upstream);
            if (!isRelay) {
                QByteArray countOut;
                runGitCapture(localPath,
                              {QStringLiteral("rev-list"), QStringLiteral("--count"),
                               QStringLiteral("@{upstream}..HEAD")},
                              &countOut, nullptr);
                scan.ahead = QString::fromUtf8(countOut).trimmed().toInt();
            }
            return scan;
        },
        [this, index, repo, upstream, isRelay](PushScan scan) {
            // The repo list can be rebuilt while the scan runs; bail (releasing the
            // pushing marker) if this index no longer points at the same repo.
            if (index < 0 || index >= m_repositories.size() ||
                m_repositories.at(index).localPath != repo.localPath) {
                m_pushingRepos.remove(index);
                updateRepoPushButton();
                return;
            }
            if (!scan.findings.isEmpty()) {
                QString detail;
                const int shown = qMin(scan.findings.size(), 5);
                for (int i = 0; i < shown; ++i) {
                    const RepoSecurityFinding &f = scan.findings.at(i);
                    detail += QStringLiteral("• %1 in %2 (line %3)\n")
                                  .arg(f.title, f.path)
                                  .arg(f.line);
                }
                if (scan.findings.size() > shown)
                    detail += QStringLiteral("  … and %1 more\n")
                                  .arg(scan.findings.size() - shown);

                QMessageBox box(this);
                box.setWindowTitle(QStringLiteral("Secret scanning: push blocked"));
                box.setIcon(QMessageBox::Critical);
                box.setText(
                    QStringLiteral(
                        "Push protection detected %1 probable secret%2 in the "
                        "commits being pushed for %3/%4.\n\n%5\n"
                        "Rotate any exposed credentials before pushing.")
                        .arg(scan.findings.size())
                        .arg(scan.findings.size() == 1 ? QString() : QStringLiteral("s"))
                        .arg(repo.owner, repo.name, detail));
                auto *cancelBtn = box.addButton(QStringLiteral("Cancel push"),
                                                QMessageBox::RejectRole);
                auto *bypassBtn = box.addButton(QStringLiteral("Push anyway"),
                                                QMessageBox::DestructiveRole);
                box.setDefaultButton(cancelBtn);
                box.exec();
                if (box.clickedButton() != bypassBtn) {
                    m_pushingRepos.remove(index);
                    updateRepoPushButton();
                    return;
                }
                logSystem(
                    QStringLiteral(
                        "Git: secret-scan bypass: pushing %1/%2 despite %3 finding%4.")
                        .arg(repo.owner, repo.name)
                        .arg(scan.findings.size())
                        .arg(scan.findings.size() == 1 ? QString() : QStringLiteral("s")));
            }

            if (isRelay) {
                m_pushingRepos.remove(index);
                logSystem(QStringLiteral("Git: publishing local commits for %1/%2 to "
                                         "the served mirror.")
                              .arg(repo.owner, repo.name));
                syncRepository(index, /*quiet=*/false);
                updateRepoPushButton();
                return;
            }
            startRepoPush(index, repo, upstream, scan.ahead);
        });
}

// Kick off the actual (already-async) `git push`, wiring up the completion/error
// handlers. Split out of pushCurrentRepoUpstream so the off-thread secret scan can
// resume here on the GUI thread once the push is approved. The repo is assumed to
// already be marked in m_pushingRepos.
void MainWindow::startRepoPush(int index, const RepositoryRecord &repo,
                               const QString &upstream, int ahead)
{
    updateRepoPushButton();
    logSystem(QStringLiteral("Git: pushing %1/%2 to %3.")
                  .arg(repo.owner, repo.name, upstream));

    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, index, repo, upstream, ahead](int exitCode,
                                                          QProcess::ExitStatus status) {
                if (process->property("handled").toBool())
                    return;
                process->setProperty("handled", true);
                const QString errors =
                    QString::fromUtf8(process->readAllStandardError()).trimmed();
                process->deleteLater();
                m_pushingRepos.remove(index);

                if (status == QProcess::NormalExit && exitCode == 0) {
                    const QString count =
                        ahead > 0 ? QString::number(ahead) + QLatin1Char(' ') : QString();
                    logSystem(QStringLiteral("Git: pushed %1commit%2 from %3/%4 to %5.")
                                  .arg(count,
                                       ahead == 1 ? QString() : QStringLiteral("s"),
                                       repo.owner, repo.name, upstream));
                    flashMessage(QStringLiteral("Pushed %1/%2 to %3.")
                                     .arg(repo.owner, repo.name, upstream));
                    if (index >= 0 && index < m_repositories.size()) {
                        if (!m_repositories.at(index).mirrorPath.isEmpty())
                            syncRepository(index, /*quiet=*/true);
                        else if (index == m_repoDetailIndex)
                            refreshOpenRepoDetail();
                    }
                } else {
                    const QString detail =
                        errors.isEmpty() ? QStringLiteral("git push failed")
                                         : errors.right(300);
                    logSystem(QStringLiteral("Git: push failed for %1/%2: %3")
                                  .arg(repo.owner, repo.name, detail));
                    flashMessage(QStringLiteral("Push failed for %1/%2: %3")
                                     .arg(repo.owner, repo.name, detail.left(160)),
                                 true);
                }
                updateRepoPushButton();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, index, repo](QProcess::ProcessError) {
                if (process->property("handled").toBool())
                    return;
                process->setProperty("handled", true);
                process->deleteLater();
                m_pushingRepos.remove(index);
                logSystem(QStringLiteral("Git: could not start push for %1/%2.")
                              .arg(repo.owner, repo.name));
                flashMessage(QStringLiteral("Could not run git push for %1/%2.")
                                 .arg(repo.owner, repo.name),
                             true);
                updateRepoPushButton();
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.localPath, QStringLiteral("push")});
}

void MainWindow::showRepoMenu()
{
    if (!m_repoMenuButton)
        return;
    QMenu menu(this);

    QAction *header = menu.addAction(
        QStringLiteral("Repositories (%1)").arg(formatCount(m_repoMenuEntries.size())));
    header->setEnabled(false);

    auto *searchEdit = new QLineEdit(&menu);
    searchEdit->setPlaceholderText(QStringLiteral("Search repositories") +
                                   QString::fromUtf8("\xE2\x80\xA6"));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(260);
    auto *searchAction = new QWidgetAction(&menu);
    searchAction->setDefaultWidget(searchEdit);
    menu.addAction(searchAction);
    menu.addSeparator();

    if (m_repoMenuEntries.isEmpty()) {
        QAction *empty = menu.addAction(QStringLiteral("No repositories yet"));
        empty->setEnabled(false);
    }

    QList<QAction *> repoActions;
    QStringList repoNames;
    for (const RepoMenuEntry &e : std::as_const(m_repoMenuEntries)) {
        QAction *act = menu.addAction(e.icon, e.label);
        const int index = e.index;
        const QString advertised = e.advertised;
        connect(act, &QAction::triggered, this, [this, index, advertised] {
            if (index >= 0 && index < m_repositories.size())
                openRepoDetailDeferred(index); // files + issues, with a spinner
            else if (index == -2)              // advertised mirror: temporary preview
                previewAdvertisedRepo(advertised);
            updateRepoSwitcher();
        });
        repoActions.append(act);
        repoNames.append(e.label.toLower());
    }

    menu.addSeparator();
    QAction *newAct = menu.addAction(QStringLiteral("New repository") +
                                     QString::fromUtf8("\xE2\x80\xA6"));
    connect(newAct, &QAction::triggered, this, &MainWindow::createNewRepository);
    QAction *addAct = menu.addAction(QStringLiteral("Add local repo") +
                                     QString::fromUtf8("\xE2\x80\xA6"));
    connect(addAct, &QAction::triggered, this, &MainWindow::promptAddRepository);

    connect(searchEdit, &QLineEdit::textChanged, &menu,
            [repoActions, repoNames](const QString &text) {
                const QString needle = text.trimmed().toLower();
                for (int i = 0; i < repoActions.size(); ++i)
                    repoActions.at(i)->setVisible(needle.isEmpty() ||
                                                  repoNames.at(i).contains(needle));
            });
    QTimer::singleShot(0, searchEdit, [searchEdit] { searchEdit->setFocus(); });

    menu.exec(m_repoMenuButton->mapToGlobal(
        QPoint(0, m_repoMenuButton->height())));
}

void MainWindow::showChatView()
{
    // Chat is its own top-level section now; switching to it clears the unread
    // marker for the open conversation (handled in showSection).
    showSection(2);
}

void MainWindow::updateChatButton()
{
    if (!m_chatButton)
        return;
    // The glyph stays the themed default now; unread is shown by a red count
    // badge instead of tinting the icon green.
    setOcticon(m_chatButton, "comment", 16);

    int total = 0;
    for (const int n : std::as_const(m_unreadCounts))
        total += n;

    if (m_chatUnreadBadge) {
        if (total > 0) {
            const QString text =
                total > 99 ? QStringLiteral("99+") : QString::number(total);
            m_chatUnreadBadge->setText(text);
            // Size to the text (a circle for one digit, a pill for more) and pin
            // to the button's top-right corner. The padding has to clear the 1px
            // border on each side and leave a little slack so the centred digits
            // aren't clipped on the sides.
            const int w = qMax(15, m_chatUnreadBadge->fontMetrics()
                                       .horizontalAdvance(text) + 12);
            m_chatUnreadBadge->resize(w, 15);
            m_chatUnreadBadge->move(qMax(0, m_chatButton->width() - w), 0);
            m_chatUnreadBadge->show();
            m_chatUnreadBadge->raise();
        } else {
            m_chatUnreadBadge->hide();
        }
    }

    m_chatButton->setToolTip(
        total > 0 ? QString::fromUtf8("Chat \xE2\x80\x94 %1 unread message%2")
                        .arg(total)
                        .arg(total == 1 ? QString() : QStringLiteral("s"))
                  : QStringLiteral("Chat"));
}

bool MainWindow::isChatViewVisible() const
{
    // Chat is its own section (index 2). It's "being read" only when that
    // section is active and the app window is active (not minimized / behind).
    return isActiveWindow() && m_sectionStack &&
           m_sectionStack->currentIndex() == 2;
}

QWidget *MainWindow::buildSolanaNotice()
{
    m_solanaBanner = new QWidget;
    m_solanaBanner->setObjectName("solanaBanner");
    m_solanaBannerLabel = new QLabel(
        "Add a Solana address so others can sponsor this node — it keeps "
        "the network open to donations and more sustainable.");
    m_solanaBannerLabel->setObjectName("solanaBannerLabel");
    m_solanaBannerLabel->setWordWrap(true);

    auto *addButton = new QPushButton("Add Solana address");
    addButton->setObjectName("primaryButton");
    addButton->setCursor(Qt::PointingHandCursor);
    setOcticon(addButton, "plus", 16);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::promptSetSolanaAddress);

    auto *dismissButton = new QPushButton(QString());
    dismissButton->setObjectName("ghostButton");
    dismissButton->setCursor(Qt::PointingHandCursor);
    dismissButton->setToolTip("Hide for now");
    setOcticon(dismissButton, "x", 16);
    connect(dismissButton, &QPushButton::clicked, m_solanaBanner, &QWidget::hide);

    auto *layout = new QHBoxLayout(m_solanaBanner);
    layout->setContentsMargins(16, 10, 12, 10);
    layout->setSpacing(10);
    layout->addWidget(m_solanaBannerLabel, 1);
    layout->addWidget(addButton);
    layout->addWidget(dismissButton);
    m_solanaBanner->hide();
    return m_solanaBanner;
}

void MainWindow::updateSolanaNotice()
{
    if (!m_solanaBanner)
        return;
    // Crypto is strictly opt-in, so we no longer nag every account-less node to
    // add a payout address. The only prompt to set one is the explicit "Get paid
    // to mirror" button on the node profile; the banner stays hidden here.
    m_solanaBanner->setVisible(false);
    updateWalletVerifyNotice();
    updateNavSolanaBalance();
}

QWidget *MainWindow::buildWalletVerifyNotice()
{
    m_walletVerifyBanner = new QWidget;
    m_walletVerifyBanner->setObjectName("walletVerifyBanner");

    auto *icon = new QLabel;
    icon->setObjectName("walletVerifyIcon");
    icon->setPixmap(themedOcticon("alert", QColor("#d29922"), 22).pixmap(22, 22));

    auto *title = new QLabel("Verify your payout wallet to receive payouts");
    title->setObjectName("walletVerifyTitle");
    auto *body = new QLabel(
        "This node's payout wallet isn't verified yet, so it can't receive "
        "payouts. Make a small deposit (\xE2\x89\xA5 0.001 SOL) to your wallet "
        "to prove you control it \xE2\x80\x94 then this node starts earning its "
        "share of the network rewards.");
    body->setObjectName("walletVerifyBody");
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    textCol->addWidget(title);
    textCol->addWidget(body);

    auto *verifyButton = new QPushButton("Verify wallet");
    verifyButton->setObjectName("primaryButton");
    verifyButton->setCursor(Qt::PointingHandCursor);
    setOcticon(verifyButton, "shield-check", 16);
    connect(verifyButton, &QPushButton::clicked, this, &MainWindow::verifyWallet);

    auto *dismissButton = new QPushButton(QString());
    dismissButton->setObjectName("ghostButton");
    dismissButton->setCursor(Qt::PointingHandCursor);
    dismissButton->setToolTip("Hide for now");
    setOcticon(dismissButton, "x", 16);
    connect(dismissButton, &QPushButton::clicked, m_walletVerifyBanner,
            &QWidget::hide);

    auto *layout = new QHBoxLayout(m_walletVerifyBanner);
    layout->setContentsMargins(16, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addLayout(textCol, 1);
    layout->addWidget(verifyButton, 0, Qt::AlignVCenter);
    layout->addWidget(dismissButton, 0, Qt::AlignTop);

    m_walletVerifyBanner->hide();
    return m_walletVerifyBanner;
}

void MainWindow::updateWalletVerifyNotice()
{
    if (!m_walletVerifyBanner)
        return;
    // Show only once a payout address exists (the "add an address" banner covers
    // the no-address case) and the wallet isn't verified yet.
    const bool hasAddress = !savedSolanaAddress().isEmpty();
    m_walletVerifyBanner->setVisible(hasAddress && !m_accountSolanaVerified);
}

void MainWindow::promptSetSolanaAddress()
{
    bool ok = false;
    const QString current = savedSolanaAddress();
    const QString address = QInputDialog::getText(
        this, "Solana address",
        "Enter a Solana address to receive donations:", QLineEdit::Normal,
        current, &ok);
    if (!ok)
        return;
    const QString trimmed = address.trimmed();
    saveSolanaAddress(trimmed);
    if (m_solanaEdit)
        m_solanaEdit->setText(trimmed);
    if (m_settingsSolanaEdit)
        m_settingsSolanaEdit->setText(trimmed);
    // The address is shared with peers on the next connect; the sponsor button
    // and donation notice pick it up immediately.
    updateSolanaNotice();
    updateHomeStats();
}

void MainWindow::enablePaidMirroring()
{
    // Crypto is strictly opt-in: the core flow (clone, mirror, issues, PRs, chat)
    // never routes here. This is the one place a user chooses to host their
    // mirrors on the network and earn donations, which needs two things the core
    // flow does not: (1) a Solana payout address and (2) an active account the
    // relay can verify before it accepts hosting/publishing.
    QString address = savedSolanaAddress().trimmed();
    if (address.isEmpty()) {
        promptSetSolanaAddress();
        address = savedSolanaAddress().trimmed();
        if (address.isEmpty())
            return; // user cancelled the address prompt — stay opted out
    }

    if (!hasActiveAccountSession()) {
        // Reuse the established join/activate path (reserve -> donate -> set
        // login, or log in to an existing account). On cancel/failure it surfaces
        // the reason itself; we simply stay opted out.
        if (!ensureNodeAccount(accountOwner(), address))
            return;
    }

    // Active now: bring the live hosts up and (re)publish existing mirrors so the
    // network can clone from this node and route donations to its wallet.
    startRepoHosts();
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (m_repositories.at(i).publishToNetwork)
            publishRepository(i, /*showDialogOnError=*/false);
    }

    updateSolanaNotice();
    updateHomeStats();
    // Refresh the open profile so the button flips to its "earning" state and the
    // Solana address/QR/balance section appears next to the username.
    if (m_nodeProfilePanel && m_nodeProfilePanel->isVisible())
        showNodeProfile(m_profileNodeId, m_profileNodeName);
    flashMessage(QStringLiteral("You're set up to get paid to mirror."));
}

void MainWindow::showSection(int index)
{
    // Leaving Settings (index 1) while the mic test is recording would otherwise
    // leave the recorder holding the microphone open in the background; stop it.
    if (index != 1 && m_voiceTestRecording)
        stopMicTest();
    if (m_navGroup && m_navGroup->button(index))
        m_navGroup->button(index)->setChecked(true);
    if (m_sectionStack)
        m_sectionStack->setCurrentIndex(index);
    // Capture this landing onto the Back / Forward trail. Debounced, so opening a
    // repo (which ends in showSection(0) after m_repoDetailIndex is set) records the
    // settled {section, repo} place once, not the intermediate steps.
    scheduleNavRecord();
    updateBreadcrumb();
    if (index == 0)
        updateHomeStats();
    else if (index == 2) {
        // Entering Chat clears the unread marker for the open conversation.
        if (!m_currentConversation.isEmpty() &&
            m_unread.remove(m_currentConversation)) {
            m_unreadCounts.remove(m_currentConversation);
            refreshChannelList();
            refreshDmList();
            // Unread state persists across restarts now; flush the cleared
            // marker so it doesn't come back after a restart.
            scheduleChatSave();
        }
        updateChatButton();
    } else if (index == 3) {
        refreshNotificationsTable();
    } else if (index == 4 && m_settingsLog) {
        // First visit renders the persisted history that buildLogSection()
        // deliberately skipped (see m_networkLogViewStale) — the rebuild replays
        // the whole in-memory buffer, so lines appended live since launch keep
        // their place in order.
        if (m_networkLogViewStale) {
            m_networkLogViewStale = false;
            rebuildNetworkLogView();
        }
        // Jump to the newest log line whenever the Log section opens.
        m_settingsLog->moveCursor(QTextCursor::End);
    } else if (index == 5) {
        // Pull the latest rankings each time the Leaderboards section opens.
        refreshLeaderboards();
    } else if (index == 7) {
        // Re-read the saved host list whenever the Hosts section opens.
        refreshHostsTable();
    } else if (index == 8) {
        // Re-list and re-probe the relays each time the Relays section opens.
        refreshRelaysTable();
    }
}

// --- Leaderboards (issue #11) ----------------------------------------------
// A scrollable grid of ranking cards mirroring the website's /network/
// leaderboards: mainnode uptime, top owners, most-mirrored and longest-hosted
// repositories, contributor activity, and funds received by mainnodes,
// contributors and projects. Data comes from /api/network/leaderboards.

QWidget *MainWindow::buildLeaderboardsSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Leaderboards"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *subtitle = new QLabel(QString::fromUtf8(
        "Mainnode uptime, top owners, the most-mirrored and longest-hosted "
        "repositories, contributor activity, and funds received \xE2\x80\x94 "
        "across the whole network."));
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);

    m_leaderboardsStatus = new QLabel(QString::fromUtf8("Loading leaderboards\xE2\x80\xA6"));
    m_leaderboardsStatus->setObjectName("mutedLabel");
    outer->addWidget(m_leaderboardsStatus);

    // The boards themselves live in a grid of cards inside a scroll area so a
    // tall list never forces the window taller.
    m_leaderboardsContent = new QWidget;
    auto *grid = new QGridLayout(m_leaderboardsContent);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(20);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_leaderboardsContent);
    outer->addWidget(scroll, 1);

    return page;
}

void MainWindow::refreshLeaderboards()
{
    if (m_leaderboardsStatus)
        m_leaderboardsStatus->setText(QString::fromUtf8("Loading leaderboards\xE2\x80\xA6"));

    QUrl url = catalogApiUrl(); // same relay host, http(s) scheme
    url.setPath(QStringLiteral("/api/network/leaderboards"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setRawHeader("accept", "application/json");
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (m_leaderboardsStatus)
                m_leaderboardsStatus->setText(
                    QStringLiteral("Leaderboards unavailable right now."));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        populateLeaderboards(obj);
    });
}

void MainWindow::populateLeaderboards(const QJsonObject &data)
{
    if (!m_leaderboardsContent)
        return;
    auto *grid = qobject_cast<QGridLayout *>(m_leaderboardsContent->layout());
    if (!grid)
        return;

    // Clear any boards from a previous refresh.
    QLayoutItem *old = nullptr;
    while ((old = grid->takeAt(0))) {
        if (old->widget())
            old->widget()->deleteLater();
        delete old;
    }

    // Build one board card from a JSON array, formatting each row's value.
    auto makeBoard = [](const QString &boardTitle, const QString &boardSub,
                        const QJsonArray &rows,
                        const std::function<QString(const QJsonObject &)> &fmt)
        -> QWidget * {
        auto *card = new QFrame;
        card->setObjectName("leaderboardCard");
        card->setFrameShape(QFrame::StyledPanel);
        auto *col = new QVBoxLayout(card);
        col->setContentsMargins(16, 14, 16, 14);
        col->setSpacing(2);

        auto *h = new QLabel(boardTitle);
        QFont hf = h->font();
        hf.setBold(true);
        hf.setPointSizeF(hf.pointSizeF() + 1);
        h->setFont(hf);
        col->addWidget(h);

        auto *sub = new QLabel(boardSub);
        sub->setObjectName("mutedLabel");
        sub->setWordWrap(true);
        QFont sf = sub->font();
        sf.setPointSizeF(sf.pointSizeF() - 1);
        sub->setFont(sf);
        col->addWidget(sub);
        col->addSpacing(6);

        if (rows.isEmpty()) {
            auto *empty = new QLabel(QStringLiteral("No data yet."));
            empty->setObjectName("mutedLabel");
            col->addWidget(empty);
            return card;
        }

        int rank = 0;
        for (const QJsonValue &v : rows) {
            const QJsonObject row = v.toObject();
            ++rank;
            auto *line = new QHBoxLayout;
            line->setContentsMargins(0, 2, 0, 2);
            line->setSpacing(8);

            auto *rankLabel = new QLabel(QString::number(rank));
            rankLabel->setObjectName("mutedLabel");
            rankLabel->setFixedWidth(20);
            line->addWidget(rankLabel);

            auto *name = new QLabel(row.value("name").toString(QStringLiteral("node")));
            name->setTextInteractionFlags(Qt::TextSelectableByMouse);
            line->addWidget(name, 1);

            auto *value = new QLabel(fmt(row));
            QFont vf = value->font();
            vf.setBold(true);
            value->setFont(vf);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            line->addWidget(value);

            col->addLayout(line);
        }
        return card;
    };

    auto arr = [&](const char *key) { return data.value(QLatin1String(key)).toArray(); };
    auto numVal = [](const QJsonObject &o, const char *k) {
        return o.value(QLatin1String(k)).toDouble();
    };
    auto plural = [](double n, const QString &word) {
        const long long v = static_cast<long long>(n);
        return QString::number(v) + " " + word + (v == 1 ? "" : "s");
    };
    auto fmtMinutes = [](double m) {
        const long long mins = static_cast<long long>(m);
        const long long h = mins / 60, rem = mins % 60;
        if (h && rem) return QStringLiteral("%1h %2m").arg(h).arg(rem);
        if (h) return QStringLiteral("%1h").arg(h);
        return QStringLiteral("%1m").arg(rem);
    };
    auto fmtAge = [](double ms) {
        const long long days = static_cast<long long>(ms / 86400000.0);
        if (days >= 1) return QString::number(days) + (days == 1 ? " day" : " days");
        const long long hrs = static_cast<long long>(ms / 3600000.0);
        return QString::number(hrs) + (hrs == 1 ? " hour" : " hours");
    };
    auto fmtSol = [numVal](const QJsonObject &o) {
        double sol = o.value(QLatin1String("sol")).toDouble();
        if (sol <= 0)
            sol = numVal(o, "lamports") / 1e9;
        return QString::number(sol, 'f', sol >= 1 ? 2 : 4) + " SOL";
    };

    const int hours = data.value("windowHours").toInt(48);

    struct Board {
        QString title, sub;
        QJsonArray rows;
        std::function<QString(const QJsonObject &)> fmt;
    };
    QList<Board> boards = {
        {QStringLiteral("Mainnode uptime"),
         QStringLiteral("Most minutes online \xC2\xB7 last %1h").arg(hours), arr("uptime"),
         [fmtMinutes, numVal](const QJsonObject &o) { return fmtMinutes(numVal(o, "minutes")); }},
        {QStringLiteral("Top owners"), QStringLiteral("Most public repositories"),
         arr("repos"),
         [plural, numVal](const QJsonObject &o) { return plural(numVal(o, "repos"), "repo"); }},
        {QStringLiteral("Most mirrored"),
         QStringLiteral("Repositories hosted under the most owners"), arr("mirrors"),
         [plural, numVal](const QJsonObject &o) { return plural(numVal(o, "mirrors"), "owner"); }},
        {QStringLiteral("Longest hosted"),
         QStringLiteral("Repositories online the longest"), arr("hosted"),
         [fmtAge, numVal](const QJsonObject &o) { return fmtAge(numVal(o, "ageMs")); }},
        {QStringLiteral("Contributor activity"),
         QStringLiteral("Issues + pull requests + commits"), arr("contributors"),
         [plural, numVal](const QJsonObject &o) { return plural(numVal(o, "total"), "contribution"); }},
        {QString::fromUtf8("Funds \xC2\xB7 mainnodes"),
         QStringLiteral("Most received from the node split"), arr("fundsMainnodes"),
         fmtSol},
        {QString::fromUtf8("Funds \xC2\xB7 contributors"),
         QStringLiteral("Most received from bounties"), arr("fundsContributors"),
         fmtSol},
        {QString::fromUtf8("Funds \xC2\xB7 projects"),
         QStringLiteral("Most bounty funds earned"), arr("fundsProjects"), fmtSol},
    };

    const int columns = 2;
    for (int i = 0; i < boards.size(); ++i) {
        const Board &b = boards.at(i);
        auto *card = makeBoard(b.title, b.sub, b.rows, b.fmt);
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        grid->addWidget(card, i / columns, i % columns, Qt::AlignTop);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(grid->rowCount(), 1);

    if (m_leaderboardsStatus)
        m_leaderboardsStatus->setText(QString());
}

// --- Hosts (adhoc #263) -----------------------------------------------------
//
// Provision a remote machine onto the network: enter its IP, SSH username and
// password plus the node name to give it, and run the hosted ForkMesh installer
// (curl https://<host>/install.sh | bash) on it over a plain SSH shell. The SSH
// session + install output streams live into the console below. Once the
// installer finishes the new node joins the network and appears on its own in
// the per-repo Mirror nodes list.

QString MainWindow::installScriptUrl() const
{
    QUrl url = catalogApiUrl(); // same relay host, http(s) scheme
    url.setPath(QStringLiteral("/install.sh"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

QString MainWindow::uninstallScriptUrl() const
{
    QUrl url = catalogApiUrl(); // same relay host, http(s) scheme
    url.setPath(QStringLiteral("/uninstall.sh"));
    url.setQuery(QString());
    url.setFragment(QString());
    return url.toString();
}

QWidget *MainWindow::buildHostsSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("Hosts"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    // Bulk one-click install: upload this app's own release binary to every
    // saved host in turn, instead of clicking Install (binary) on each row.
    m_hostInstallAllButton = new QPushButton(QStringLiteral("Install from binary (all hosts)"));
    m_hostInstallAllButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_hostInstallAllButton, "upload", 14);
    connect(m_hostInstallAllButton, &QPushButton::clicked, this,
            &MainWindow::runHostInstallAllFromBinary);
    titleRow->addWidget(m_hostInstallAllButton);
    // Uninstall + reinstall from binary across every saved host (adhoc #258):
    // wipe each host's existing install + data, then install a fresh copy from
    // this app's binary so a stuck/stale node comes back cleanly.
    m_hostReinstallAllButton =
        new QPushButton(QStringLiteral("Uninstall + reinstall (all hosts)"));
    m_hostReinstallAllButton->setCursor(Qt::PointingHandCursor);
    m_hostReinstallAllButton->setToolTip(QStringLiteral(
        "For every saved host: remove ForkMesh and ALL of its data, then "
        "install a fresh copy from this app's binary and re-link it to your "
        "account."));
    setOcticon(m_hostReinstallAllButton, "sync", 14);
    connect(m_hostReinstallAllButton, &QPushButton::clicked, this,
            &MainWindow::runHostReinstallAllFromBinary);
    titleRow->addWidget(m_hostReinstallAllButton);
    outer->addLayout(titleRow);

    auto *subtitle = new QLabel(QString::fromUtf8(
        "Provision a remote machine onto the network. Enter its address and SSH "
        "login and give it a node name, then click Add host to save it. With the "
        "host saved, click Install ForkMesh and it will SSH in and run the hosted "
        "installer in a plain shell. When it finishes the new node joins the "
        "network and shows up in each repository's Mirror nodes list. Install "
        "(binary) on a saved host, or Install from binary (all hosts) above, "
        "uploads this app's own release binary to the host instead of having it "
        "download the release itself."));
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget;
    auto *bodyCol = new QVBoxLayout(body);
    bodyCol->setContentsMargins(0, 0, 0, 0);
    bodyCol->setSpacing(16);

    // --- Install form ------------------------------------------------------
    auto *formCard = new QFrame;
    formCard->setObjectName("leaderboardCard");
    formCard->setFrameShape(QFrame::StyledPanel);
    auto *formCol = new QVBoxLayout(formCard);
    formCol->setContentsMargins(16, 14, 16, 14);
    formCol->setSpacing(10);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(8);

    m_hostIpEdit = new QLineEdit;
    m_hostIpEdit->setPlaceholderText(QStringLiteral("203.0.113.10"));
    form->addRow(QStringLiteral("Host IP / address"), m_hostIpEdit);

    m_hostUserEdit = new QLineEdit;
    m_hostUserEdit->setPlaceholderText(QStringLiteral("root"));
    form->addRow(QStringLiteral("SSH username"), m_hostUserEdit);

    m_hostPassEdit = new QLineEdit;
    m_hostPassEdit->setEchoMode(QLineEdit::Password);
    m_hostPassEdit->setPlaceholderText(QStringLiteral("SSH password"));
    form->addRow(QStringLiteral("SSH password"), m_hostPassEdit);

    m_hostNameEdit = new QLineEdit;
    m_hostNameEdit->setPlaceholderText(QStringLiteral("my-mirror-1"));
    form->addRow(QStringLiteral("Node name"), m_hostNameEdit);

    // Direct-upload install (adhoc #67): instead of the host downloading the
    // release from the relay, stream this app's own binary to it over the SSH
    // session. The host still curls the small install script, which installs
    // the uploaded file after checking it matches the host's OS/architecture
    // (and falls back to the normal download when it doesn't).
    m_hostUploadBinaryCheck =
        new QCheckBox(QStringLiteral("Upload the release from this app"));
    m_hostUploadBinaryCheck->setToolTip(QString::fromUtf8(
        "Stream this app's own release binary to the host over the SSH "
        "session, instead of the host downloading the release from the "
        "network. Useful when the host cannot reach the release download, or "
        "to push exactly the build you are running. If the host's OS or "
        "architecture does not match this machine, the installer falls back "
        "to the normal download."));
    form->addRow(QString(), m_hostUploadBinaryCheck);
    formCol->addLayout(form);

    auto *runRow = new QHBoxLayout;
    runRow->setContentsMargins(0, 0, 0, 0);
    // Add the host first (saves name/IP/user), then run the installer against
    // the saved host. Saving up front means the server info is remembered even
    // before — or if — the install runs.
    m_hostAddButton = new QPushButton(QStringLiteral("Add host"));
    m_hostAddButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_hostAddButton, "plus", 14);
    connect(m_hostAddButton, &QPushButton::clicked, this,
            &MainWindow::addHostFromForm);
    runRow->addWidget(m_hostAddButton);
    m_hostInstallButton = new QPushButton(QStringLiteral("Install ForkMesh"));
    m_hostInstallButton->setObjectName("primaryButton");
    m_hostInstallButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_hostInstallButton, "rocket", 14);
    connect(m_hostInstallButton, &QPushButton::clicked, this,
            [this] { runHostInstall(); });
    runRow->addWidget(m_hostInstallButton);
    m_hostInstallStatus = new QLabel;
    m_hostInstallStatus->setObjectName("mutedLabel");
    m_hostInstallStatus->setWordWrap(true);
    runRow->addWidget(m_hostInstallStatus, 1);
    formCol->addLayout(runRow);
    bodyCol->addWidget(formCard);

    // --- Live session / install output ------------------------------------
    auto *logLabel = new QLabel(QStringLiteral("Live output"));
    QFont llf = logLabel->font();
    llf.setBold(true);
    logLabel->setFont(llf);
    bodyCol->addWidget(logLabel);

    m_hostInstallLog = new QPlainTextEdit;
    m_hostInstallLog->setObjectName("actionLog");
    m_hostInstallLog->setReadOnly(true);
    m_hostInstallLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_hostInstallLog->setMinimumHeight(220);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_hostInstallLog->setFont(mono);
    m_hostInstallLog->setPlaceholderText(QString::fromUtf8(
        "The SSH session and installer output will stream here\xE2\x80\xA6"));
    bodyCol->addWidget(m_hostInstallLog);

    // --- Provisioned hosts list -------------------------------------------
    auto *hostsLabel = new QLabel(QStringLiteral("Hosts"));
    QFont hlf = hostsLabel->font();
    hlf.setBold(true);
    hostsLabel->setFont(hlf);
    bodyCol->addWidget(hostsLabel);

    auto *hostsHint = new QLabel(QString::fromUtf8(
        "Click Update on a saved host to re-run the installer and bring it up to "
        "the latest ForkMesh release. Click Uninstall to completely remove "
        "ForkMesh \xE2\x80\x94 binary, launcher and ALL data \xE2\x80\x94 from "
        "that host. Double-click a host instead to reload it into the form "
        "above for editing."));
    hostsHint->setObjectName("mutedLabel");
    hostsHint->setWordWrap(true);
    bodyCol->addWidget(hostsHint);

    m_hostsTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_hostsTable); // 3-dots per-column menu (issue #318)
    m_hostsTable->setObjectName("issueTable");
    m_hostsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Node name"), QStringLiteral("Address"),
         QStringLiteral("User"), QStringLiteral("Status"), QString()});
    m_hostsTable->verticalHeader()->setVisible(false);
    m_hostsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hostsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hostsTable->setShowGrid(false);
    // Stretch the Status column and let the trailing Update-button column size to
    // its contents.
    m_hostsTable->horizontalHeader()->setStretchLastSection(false);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_hostsTable); // spreadsheet-style draggable columns (#263)
    // Double-clicking a saved host reloads its server info into the install
    // form so the installer can be re-run. The password is never stored on
    // disk, so it is left blank for the user to re-enter.
    connect(m_hostsTable, &QTableWidget::cellDoubleClicked, this,
            &MainWindow::loadHostIntoForm);
    bodyCol->addWidget(m_hostsTable);

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    refreshHostsTable();
    return page;
}

void MainWindow::refreshHostsTable()
{
    if (!m_hostsTable)
        return;
    const QJsonArray hosts =
        QJsonDocument::fromJson(QSettings().value(kHostsSetting).toString().toUtf8())
            .array();
    m_hostsTable->setRowCount(hosts.size());
    for (int i = 0; i < hosts.size(); ++i) {
        const QJsonObject h = hosts.at(i).toObject();
        const QString status = h.value("status").toString(QStringLiteral("installed"));
        m_hostsTable->setItem(i, 0,
            new QTableWidgetItem(h.value("name").toString()));
        m_hostsTable->setItem(i, 1,
            new QTableWidgetItem(h.value("ip").toString()));
        m_hostsTable->setItem(i, 2,
            new QTableWidgetItem(h.value("user").toString()));
        m_hostsTable->setItem(i, 3, new QTableWidgetItem(status));

        // Per-row Update button: reload the saved host into the install form and
        // re-run the hosted installer against it. The installer is idempotent, so
        // re-running it pulls the latest ForkMesh release onto that host.
        auto *cell = new QWidget;
        auto *cellRow = new QHBoxLayout(cell);
        cellRow->setContentsMargins(4, 2, 4, 2);
        cellRow->setSpacing(0);
        auto *updateBtn = new QPushButton(QStringLiteral("Update"));
        updateBtn->setCursor(Qt::PointingHandCursor);
        setOcticon(updateBtn, "sync", 12);
        // Defer to the next event-loop turn: re-running the installer rebuilds
        // this table (and deletes this very button), so let the click signal
        // fully unwind first.
        connect(updateBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                loadHostIntoForm(i, 0);
                runHostInstall();
            });
        });
        cellRow->addWidget(updateBtn);

        // Per-row "Install (binary)" button: one-click direct-upload install —
        // reload the saved host into the form and run the installer with this
        // app's own release binary uploaded over the SSH session, regardless of
        // the form's "Upload the release from this app" checkbox state.
        auto *installBinaryBtn = new QPushButton(QStringLiteral("Install (binary)"));
        installBinaryBtn->setCursor(Qt::PointingHandCursor);
        setOcticon(installBinaryBtn, "upload", 12);
        connect(installBinaryBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                loadHostIntoForm(i, 0);
                runHostInstall(/*forceUploadBinary=*/true);
            });
        });
        cellRow->addWidget(installBinaryBtn);

        // Per-row Uninstall button: reload the saved host into the form and run
        // the hosted uninstaller against it, after a confirmation prompt since it
        // wipes the node's identity key and all mirrored data on that host.
        auto *uninstallBtn = new QPushButton(QStringLiteral("Uninstall"));
        uninstallBtn->setCursor(Qt::PointingHandCursor);
        setOcticon(uninstallBtn, "trash", 12);
        connect(uninstallBtn, &QPushButton::clicked, this, [this, i] {
            const QString name =
                m_hostsTable->item(i, 0) ? m_hostsTable->item(i, 0)->text() : QString();
            const auto reply = QMessageBox::question(
                this, QStringLiteral("Uninstall ForkMesh"),
                QString::fromUtf8(
                    "This completely removes ForkMesh from \"%1\": the binary, "
                    "launcher, node identity key and ALL mirrored repositories "
                    "and chat history on that host. This cannot be undone. "
                    "Continue?")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes)
                return;
            QTimer::singleShot(0, this, [this, i] {
                loadHostIntoForm(i, 0);
                runHostUninstall();
            });
        });
        cellRow->addWidget(uninstallBtn);
        m_hostsTable->setCellWidget(i, 4, cell);
    }
}

// --- Relays -----------------------------------------------------------------
//
// A live directory of the configured mainnode relays (the same ones reachable
// from the top-bar relay switcher). Each row shows the relay's host, whether it
// is currently online, the round-trip response time and the version it is
// running. The status / latency / version are filled in by probing each relay's
// lightweight /api/version endpoint (the same endpoint the top-bar radar uses).

QWidget *MainWindow::buildRelaysSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Relays"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *subtitle = new QLabel(QString::fromUtf8(
        "The mainnode relays this node knows about. Each one is probed live for "
        "its online status, round-trip response time and the version it is "
        "running. Switch the active relay from the relay dropdown in the top bar."));
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);

    // Status line + manual refresh button.
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    m_relaysStatus = new QLabel;
    m_relaysStatus->setObjectName("mutedLabel");
    controls->addWidget(m_relaysStatus, 1);
    m_relaysRefreshButton = new QPushButton(QStringLiteral("Refresh"));
    m_relaysRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_relaysRefreshButton, "sync", 14);
    connect(m_relaysRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshRelaysTable);
    controls->addWidget(m_relaysRefreshButton);
    outer->addLayout(controls);

    m_relaysTable = new QTableWidget(0, 4);
    installColumnHeaderMenu(m_relaysTable); // 3-dots per-column menu (issue #318)
    m_relaysTable->setObjectName("issueTable");
    m_relaysTable->setHorizontalHeaderLabels(
        {QStringLiteral("Relay"), QStringLiteral("Status"),
         QStringLiteral("Response time"), QStringLiteral("Version")});
    m_relaysTable->verticalHeader()->setVisible(false);
    m_relaysTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_relaysTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_relaysTable->setShowGrid(false);
    m_relaysTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_relaysTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_relaysTable); // spreadsheet-style draggable columns (#263)
    // Double-clicking a relay opens its website in the browser.
    connect(m_relaysTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { openServerWebsite(row); });
    outer->addWidget(m_relaysTable, 1);

    refreshRelaysTable();
    return page;
}

void MainWindow::refreshRelaysTable()
{
    if (!m_relaysTable)
        return;
    m_relaysTable->setRowCount(m_servers.size());
    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        const QString host = serverHost(server.url);
        auto *nameItem = new QTableWidgetItem(QIcon(faviconFor(server)),
                                              host.isEmpty() ? server.url : host);
        // Stash the host so an in-flight probe can confirm the row hasn't shifted
        // under it before writing its result.
        nameItem->setData(Qt::UserRole, host);
        if (i == m_activeServer) {
            QFont f = nameItem->font();
            f.setBold(true);
            nameItem->setFont(f);
            nameItem->setToolTip(QStringLiteral("Active relay"));
        }
        m_relaysTable->setItem(i, 0, nameItem);
        m_relaysTable->setItem(i, 1, new QTableWidgetItem(
            QString::fromUtf8("Checking\xE2\x80\xA6")));
        m_relaysTable->setItem(i, 2, new QTableWidgetItem(QString::fromUtf8("\xE2\x80\x94")));
        m_relaysTable->setItem(i, 3, new QTableWidgetItem(QString::fromUtf8("\xE2\x80\x94")));
    }

    // Count every relay as in-flight up front so the "all done" summary only
    // fires once the last probe settles (not when an early invalid URL resolves
    // synchronously).
    m_relayProbesInFlight = m_servers.size();
    if (m_relaysStatus)
        m_relaysStatus->setText(m_servers.isEmpty()
            ? QStringLiteral("No relays configured.")
            : QString::fromUtf8("Probing %1 relay(s)\xE2\x80\xA6")
                  .arg(m_servers.size()));
    for (int i = 0; i < m_servers.size(); ++i)
        probeRelayRow(i);
}

void MainWindow::probeRelayRow(int row)
{
    if (!m_relaysTable || !m_networkAccess || row < 0 || row >= m_servers.size())
        return;

    // Same relay host as the stored ws/wss URL, but over http(s) for the API.
    QUrl url(m_servers.at(row).url);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/version"));
    url.setQuery(QString());
    url.setFragment(QString());

    const QString host = serverHost(m_servers.at(row).url);
    auto markDone = [this] {
        if (--m_relayProbesInFlight <= 0) {
            m_relayProbesInFlight = 0;
            if (!m_relaysTable || !m_relaysStatus)
                return;
            int online = 0;
            for (int r = 0; r < m_relaysTable->rowCount(); ++r) {
                QTableWidgetItem *s = m_relaysTable->item(r, 1);
                if (s && s->text() == QStringLiteral("Online"))
                    ++online;
            }
            const int total = m_relaysTable->rowCount();
            m_relaysStatus->setText(
                QString::fromUtf8("%1 of %2 relay(s) online \xC2\xB7 updated %3")
                    .arg(online).arg(total)
                    .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
        }
    };

    // The row a probe writes back to is found by the host stashed in UserRole, so
    // it stays correct even if the list was re-sorted/rebuilt mid-flight.
    auto rowForHost = [this](const QString &h) -> int {
        if (!m_relaysTable)
            return -1;
        for (int r = 0; r < m_relaysTable->rowCount(); ++r) {
            QTableWidgetItem *item = m_relaysTable->item(r, 0);
            if (item && item->data(Qt::UserRole).toString() == h)
                return r;
        }
        return -1;
    };

    if (!url.isValid() || url.host().isEmpty()) {
        const int r = rowForHost(host);
        if (r >= 0) {
            if (auto *s = m_relaysTable->item(r, 1)) {
                s->setText(QStringLiteral("Offline"));
                s->setForeground(QColor(QStringLiteral("#8b949e")));
            }
        }
        markDone();
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("accept", "application/json");
    request.setTransferTimeout(10000); // no answer within 10s counts as offline

    auto *clock = new QElapsedTimer;
    clock->start();
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, clock, host, rowForHost, markDone] {
        const qint64 elapsed = clock->elapsed();
        delete clock;
        reply->deleteLater();
        const int r = rowForHost(host);
        if (r >= 0 && m_relaysTable) {
            QTableWidgetItem *status = m_relaysTable->item(r, 1);
            QTableWidgetItem *ping = m_relaysTable->item(r, 2);
            QTableWidgetItem *ver = m_relaysTable->item(r, 3);
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject obj =
                    QJsonDocument::fromJson(reply->readAll()).object();
                const QString rev = obj.value(QStringLiteral("rev"))
                                        .toString(QStringLiteral("dev"));
                if (status) {
                    status->setText(QStringLiteral("Online"));
                    status->setForeground(QColor(QStringLiteral("#3fb950")));
                }
                if (ping)
                    ping->setText(QStringLiteral("%1 ms").arg(elapsed));
                if (ver)
                    ver->setText(rev.isEmpty() ? QStringLiteral("dev") : rev);
            } else {
                if (status) {
                    status->setText(QStringLiteral("Offline"));
                    status->setForeground(QColor(QStringLiteral("#8b949e")));
                }
                if (ping)
                    ping->setText(QString::fromUtf8("\xE2\x80\x94"));
                if (ver)
                    ver->setText(QString::fromUtf8("\xE2\x80\x94"));
            }
        }
        markDone();
    });
}

void MainWindow::loadHostIntoForm(int row, int /*column*/)
{
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    auto cellText = [this, row](int col) -> QString {
        QTableWidgetItem *item = m_hostsTable->item(row, col);
        return item ? item->text() : QString();
    };
    const QString name = cellText(0);
    if (m_hostNameEdit)
        m_hostNameEdit->setText(name);
    if (m_hostIpEdit)
        m_hostIpEdit->setText(cellText(1));
    if (m_hostUserEdit)
        m_hostUserEdit->setText(cellText(2));
    // Load the saved SSH password from the settings.
    const QJsonArray hosts =
        QJsonDocument::fromJson(QSettings().value(kHostsSetting).toString().toUtf8())
            .array();
    for (int i = 0; i < hosts.size(); ++i) {
        const QJsonObject h = hosts.at(i).toObject();
        if (h.value("name").toString() == name) {
            if (m_hostPassEdit)
                m_hostPassEdit->setText(h.value("pass").toString());
            break;
        }
    }
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(QString::fromUtf8(
            "Loaded \"%1\". Click Install ForkMesh to run the installer.").arg(name));
}

void MainWindow::addHostFromForm()
{
    const QString ip = m_hostIpEdit ? m_hostIpEdit->text().trimmed() : QString();
    const QString user = m_hostUserEdit ? m_hostUserEdit->text().trimmed() : QString();
    const QString pass = m_hostPassEdit ? m_hostPassEdit->text() : QString();
    const QString node = m_hostNameEdit ? m_hostNameEdit->text().trimmed() : QString();
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "Enter the host IP, SSH username and a node name to add a host."));
        return;
    }
    // Save the server info up front with a not-yet-installed status. Running
    // the installer later flips it to "installed".
    rememberHost(node, ip, user, pass, QStringLiteral("added"));
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(QString::fromUtf8(
            "Saved \"%1\". Click Install ForkMesh to provision it.").arg(node));
}

void MainWindow::rememberHost(const QString &name, const QString &ip,
                              const QString &user, const QString &pass, const QString &status)
{
    QSettings settings;
    QJsonArray hosts =
        QJsonDocument::fromJson(settings.value(kHostsSetting).toString().toUtf8())
            .array();
    // Replace any existing row for the same node name, else append.
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("ip"), ip);
    entry.insert(QStringLiteral("user"), user);
    entry.insert(QStringLiteral("pass"), pass);
    entry.insert(QStringLiteral("status"), status);
    bool replaced = false;
    for (int i = 0; i < hosts.size(); ++i) {
        if (hosts.at(i).toObject().value("name").toString() == name) {
            hosts.replace(i, entry);
            replaced = true;
            break;
        }
    }
    if (!replaced)
        hosts.append(entry);
    settings.setValue(kHostsSetting,
                      QString::fromUtf8(QJsonDocument(hosts).toJson(QJsonDocument::Compact)));
    refreshHostsTable();
}

namespace {
// Foreground colours for the live install log's SGR codes (indices 0-7 normal,
// 8-15 bright), in two tables so the installer's colours stay legible on both
// the dark (#010409) and light (#f6f8fa) log backgrounds. Matches the
// GitHub-style palette used elsewhere in the UI.
int installLogAnsiFg(int idx, bool dark)
{
    static const int kDark[16] = {
        0x6e7681, 0xff7b72, 0x3fb950, 0xd29922, 0x58a6ff, 0xbc8cff, 0x39c5cf,
        0xb1bac4, 0x6e7681, 0xffa198, 0x56d364, 0xe3b341, 0x79c0ff, 0xd2a8ff,
        0x56d4dd, 0xf0f6fc};
    static const int kLight[16] = {
        0x24292f, 0xcf222e, 0x1a7f37, 0x9a6700, 0x0550ae, 0x8250df, 0x1b7c83,
        0x6e7781, 0x57606a, 0xa40e26, 0x116329, 0x7d4e00, 0x0969da, 0x6639ba,
        0x3192aa, 0x424a53};
    idx = qBound(0, idx, 15);
    return dark ? kDark[idx] : kLight[idx];
}

// xterm 256-colour cube / grayscale ramp for SGR 38;5;n with n >= 16.
int installLogXterm256(int n)
{
    if (n < 232) {
        n -= 16;
        const int r = (n / 36) % 6, g = (n / 6) % 6, b = n % 6;
        auto comp = [](int v) { return v ? v * 40 + 55 : 0; };
        return (comp(r) << 16) | (comp(g) << 8) | comp(b);
    }
    const int v = (n - 232) * 10 + 8;
    return (v << 16) | (v << 8) | v;
}
} // namespace

void MainWindow::appendHostInstallLog(const QString &text)
{
    if (!m_hostInstallLog || text.isEmpty())
        return;

    // The installer streams ANSI/VT escape sequences — SGR colour codes plus a
    // box-drawing banner. Render the SGR colours into the log and drop every
    // other control sequence; otherwise the raw codes show up as literal
    // "[32m"/"[0m" noise (adhoc #6). A sequence can straddle two read chunks, so
    // an unfinished tail is carried over to the next call.
    QString data = m_hostInstallLogCarry + text;
    m_hostInstallLogCarry.clear();
    const bool dark = currentThemeIsDark();

    QTextCursor cursor(m_hostInstallLog->document());
    cursor.movePosition(QTextCursor::End);

    auto currentFormat = [this]() {
        QTextCharFormat fmt;
        if (m_hostInstallLogFg >= 0)
            fmt.setForeground(QColor((m_hostInstallLogFg >> 16) & 0xFF,
                                     (m_hostInstallLogFg >> 8) & 0xFF,
                                     m_hostInstallLogFg & 0xFF));
        if (m_hostInstallLogBold)
            fmt.setFontWeight(QFont::Bold);
        return fmt;
    };

    QString run;
    auto flush = [&]() {
        if (!run.isEmpty()) {
            cursor.insertText(run, currentFormat());
            run.clear();
        }
    };

    // Apply one SGR sequence's parameters (the text between ESC[ and 'm') to the
    // running style. Only the foreground colour and bold weight are rendered;
    // background and other attributes are parsed-and-ignored so they don't leak.
    auto applySgr = [this, dark](const QString &paramStr) {
        const QStringList parts =
            paramStr.isEmpty() ? QStringList{QStringLiteral("0")}
                               : paramStr.split(QLatin1Char(';'));
        for (int k = 0; k < parts.size(); ++k) {
            bool ok = false;
            const int code = parts.at(k).toInt(&ok);
            if (!ok)
                continue;
            if (code == 0) {
                m_hostInstallLogFg = -1;
                m_hostInstallLogBold = false;
            } else if (code == 1) {
                m_hostInstallLogBold = true;
            } else if (code == 22) {
                m_hostInstallLogBold = false;
            } else if (code == 39) {
                m_hostInstallLogFg = -1;
            } else if (code >= 30 && code <= 37) {
                m_hostInstallLogFg = installLogAnsiFg(code - 30, dark);
            } else if (code >= 90 && code <= 97) {
                m_hostInstallLogFg = installLogAnsiFg(8 + code - 90, dark);
            } else if (code == 38 && k + 2 < parts.size() &&
                       parts.at(k + 1).toInt() == 5) {
                const int idx = parts.at(k + 2).toInt();
                m_hostInstallLogFg = idx < 16 ? installLogAnsiFg(idx, dark)
                                              : installLogXterm256(idx);
                k += 2;
            } else if (code == 38 && k + 4 < parts.size() &&
                       parts.at(k + 1).toInt() == 2) {
                m_hostInstallLogFg = ((parts.at(k + 2).toInt() & 0xFF) << 16) |
                                     ((parts.at(k + 3).toInt() & 0xFF) << 8) |
                                     (parts.at(k + 4).toInt() & 0xFF);
                k += 4;
            }
        }
    };

    int i = 0;
    const int len = data.size();
    while (i < len) {
        if (data.at(i).unicode() != 0x1B) { // ordinary text
            run += data.at(i);
            ++i;
            continue;
        }
        if (i + 1 >= len) { // dangling ESC: wait for the rest
            m_hostInstallLogCarry = data.mid(i);
            break;
        }
        const QChar kind = data.at(i + 1);
        if (kind == QLatin1Char('[')) { // CSI: ESC [ params... final(0x40-0x7E)
            int j = i + 2;
            while (j < len) {
                const ushort u = data.at(j).unicode();
                if (u >= 0x40 && u <= 0x7E)
                    break;
                ++j;
            }
            if (j >= len) { // sequence not finished yet
                m_hostInstallLogCarry = data.mid(i);
                break;
            }
            if (data.at(j) == QLatin1Char('m')) { // SGR: change the style
                flush();
                applySgr(data.mid(i + 2, j - (i + 2)));
            }
            // Other CSI finals (cursor moves, erases, …) are dropped.
            i = j + 1;
        } else if (kind == QLatin1Char(']')) { // OSC: ESC ] ... BEL or ST
            int j = i + 2;
            bool done = false;
            while (j < len) {
                if (data.at(j).unicode() == 0x07) { // BEL terminator
                    ++j;
                    done = true;
                    break;
                }
                if (data.at(j).unicode() == 0x1B && j + 1 < len &&
                    data.at(j + 1) == QLatin1Char('\\')) { // ST terminator
                    j += 2;
                    done = true;
                    break;
                }
                ++j;
            }
            if (!done) {
                m_hostInstallLogCarry = data.mid(i);
                break;
            }
            i = j;
        } else { // other two-byte escape (charset selection, etc.): drop both
            i += 2;
        }
    }
    flush();

    // Guard against a never-terminating sequence pinning real output in the
    // carry buffer forever: past a sane length, give up and show it literally.
    if (m_hostInstallLogCarry.size() > 256) {
        cursor.insertText(m_hostInstallLogCarry, currentFormat());
        m_hostInstallLogCarry.clear();
    }

    m_hostInstallLog->moveCursor(QTextCursor::End);
    m_hostInstallLog->ensureCursorVisible();
}

namespace {
// Sentinel line separating the (possibly sudo-consumed) password from the
// uploaded binary on the SSH session's stdin in direct-upload installs
// (adhoc #67). The remote side discards lines until it sees this marker, so
// the upload stays intact whether or not sudo actually read the password.
const QString kHostUploadMarker = QStringLiteral("__FORKMESH_UPLOAD__");
} // namespace

void MainWindow::runHostInstall(bool forceUploadBinary,
                                std::function<void(bool)> onFinished,
                                bool reinstall)
{
    if (m_hostInstallProcess &&
        m_hostInstallProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("An install is already running."));
        if (onFinished)
            onFinished(false);
        return;
    }

    const QString ip = m_hostIpEdit ? m_hostIpEdit->text().trimmed() : QString();
    const QString user = m_hostUserEdit ? m_hostUserEdit->text().trimmed() : QString();
    const QString pass = m_hostPassEdit ? m_hostPassEdit->text() : QString();
    const QString node = m_hostNameEdit ? m_hostNameEdit->text().trimmed() : QString();
    if (ip.isEmpty() || user.isEmpty() || pass.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "Enter the host IP, SSH username, password and a node name "
                "first."));
        if (onFinished)
            onFinished(false);
        return;
    }
    const QString installUrl = installScriptUrl();
    if (installUrl.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Could not resolve the installer URL."));
        if (onFinished)
            onFinished(false);
        return;
    }

    // Direct-upload mode (adhoc #67): stream this app's own release binary to
    // the host over the SSH session's stdin instead of the host downloading it
    // from the relay's release endpoint. Read the bytes up front so a locked or
    // missing binary fails here, before anything touches the remote machine.
    // forceUploadBinary is set by the per-row / install-all "Install (binary)"
    // actions (adhoc #257), which always direct-upload regardless of whether
    // the form's checkbox happens to be ticked.
    const bool uploadBinary = forceUploadBinary ||
        (m_hostUploadBinaryCheck && m_hostUploadBinaryCheck->isChecked());
    QByteArray uploadBytes;
    if (uploadBinary) {
        QFile self(QCoreApplication::applicationFilePath());
        if (!self.open(QIODevice::ReadOnly) ||
            (uploadBytes = self.readAll()).isEmpty()) {
            if (m_hostInstallStatus)
                m_hostInstallStatus->setText(
                    QString::fromUtf8("Could not read this app's binary (%1) "
                                      "to upload it.")
                        .arg(QCoreApplication::applicationFilePath()));
            if (onFinished)
                onFinished(false);
            return;
        }
    }

    // Build the remote command: curl the hosted installer and pipe it to bash
    // with the chosen node name. When the SSH user is not root, escalate the
    // whole installer to root with `sudo -S` (the password arrives on stdin, so
    // it never touches argv) the way the old playbook used become: true; the
    // installer then sees it is root and skips its own per-package sudo calls.
    auto shq = [](const QString &s) {
        QString out = s;
        out.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        return QStringLiteral("'") + out + QStringLiteral("'");
    };
    // Pass the chosen name as FORKMESH_NODE_NAME (not FORKMESH_NODE): the
    // installer uses it to name the freshly-deployed node and leaves the
    // clone-source mirror to auto-resolve to a real online one. The headless
    // installer then starts the node as a background daemon under this name so it
    // actually joins the network and shows up in the Mirror nodes list.
    // FORKMESH_OWNER carries this account's owner name so the installer can echo
    // which account the fresh node is being attached to (adhoc #258), and
    // FORKMESH_REINSTALL=1 tells it to wipe any existing install + data first.
    QString envPrefix = QStringLiteral("FORKMESH_NODE_NAME=%1").arg(shq(node));
    const QString owner = accountOwner();
    if (!owner.isEmpty())
        envPrefix += QStringLiteral(" FORKMESH_OWNER=%1").arg(shq(owner));
    if (reinstall)
        envPrefix += QStringLiteral(" FORKMESH_REINSTALL=1");
    QString pipeline =
        QStringLiteral("curl -fsSL %1 | %2 bash").arg(shq(installUrl), envPrefix);
    const bool needSudo = user != QStringLiteral("root");
    if (uploadBinary) {
        // The binary follows on the SSH session's stdin. Everything before the
        // marker line is discarded remotely: when sudo -S consumes the password
        // line the marker arrives first, and under passwordless sudo (or a
        // future keyed login) the stray password line is skipped instead of
        // corrupting the upload. `cat` then lands the bytes in a remote temp
        // file, which the installer consumes as FORKMESH_LOCAL_BINARY together
        // with this machine's platform — so a cross-platform upload degrades
        // into the installer's normal relay download instead of installing a
        // binary the host can't run. The temp file is removed either way.
        QString os = QSysInfo::kernelType(); // "linux" / "darwin" / "winnt"
        if (os == QStringLiteral("darwin"))
            os = QStringLiteral("macos");
        else if (os == QStringLiteral("winnt"))
            os = QStringLiteral("windows");
        pipeline =
            QStringLiteral(
                "up=\"$(mktemp \"${TMPDIR:-/tmp}/forkmesh-upload.XXXXXX\")\" && "
                "while IFS= read -r l; do [ \"$l\" = %1 ] && break; done && "
                "cat > \"$up\" && curl -fsSL %2 | %3 "
                "FORKMESH_LOCAL_BINARY=\"$up\" FORKMESH_LOCAL_OS=%4 "
                "FORKMESH_LOCAL_ARCH=%5 bash; st=$?; rm -f \"$up\"; exit $st")
                .arg(shq(kHostUploadMarker), shq(installUrl), envPrefix,
                     shq(os), shq(QSysInfo::currentCpuArchitecture()));
    }
    const QString remoteCmd =
        needSudo
            ? QStringLiteral("sudo -S -p '' -- bash -c %1").arg(shq(pipeline))
            : pipeline;

    const QStringList sshArgs = {
        QStringLiteral("-e"), QStringLiteral("ssh"),
        QStringLiteral("-o"), QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
        QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
        QStringLiteral("-o"), QStringLiteral("PreferredAuthentications=password"),
        QStringLiteral("-o"), QStringLiteral("PubkeyAuthentication=no"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=30"),
        user + QStringLiteral("@") + ip, remoteCmd};

    // Persist the server info before we start so it is saved even if the install
    // fails partway through; a successful run flips the status to "installed".
    rememberHost(node, ip, user, pass, QStringLiteral("installing"));

    m_hostInstallLog->clear();
    m_hostInstallLogCarry.clear();
    m_hostInstallLogFg = -1;
    m_hostInstallLogBold = false;
    m_hostInstallLinkTail.clear();
    m_hostLinkPrompted = false;
    // Echo the command we run (the password lives in the SSHPASS env / stdin, so
    // nothing here leaks it).
    appendHostInstallLog(
        QStringLiteral("$ ssh %1@%2 %3\n").arg(user, ip, remoteCmd));
    appendHostInstallLog(
        QStringLiteral("Connecting to %1 as %2 and running %3 ...\n\n")
            .arg(ip, user, installUrl));
    if (uploadBinary)
        appendHostInstallLog(
            QString::fromUtf8("Uploading this app's release binary (%1 MB) "
                              "over the SSH session\xE2\x80\xA6\n")
                .arg(QString::number(uploadBytes.size() / (1024.0 * 1024.0),
                                     'f', 1)));
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(
            uploadBinary
                ? QString::fromUtf8(
                      "Uploading the release and installing on %1\xE2\x80\xA6")
                      .arg(ip)
                : QString::fromUtf8("Installing on %1\xE2\x80\xA6").arg(ip));
    if (m_hostInstallButton)
        m_hostInstallButton->setEnabled(false);

    auto *proc = new QProcess(this);
    m_hostInstallProcess = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Hand the SSH password to sshpass via the environment so it never lands in
    // argv or on disk.
    env.insert(QStringLiteral("SSHPASS"), pass);
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        appendHostInstallLog(chunk);
        // The installer prints "FORKMESH LINK CODE: NNNNNN" on the fresh
        // machine (adhoc #53). Watch the stream for it — through a rolling
        // tail so a code split across read chunks still matches — and link the
        // new node to this account. Once per run.
        if (!m_hostLinkPrompted) {
            m_hostInstallLinkTail = (m_hostInstallLinkTail + chunk).right(512);
            static const QRegularExpression linkRe(
                QStringLiteral("FORKMESH LINK CODE:\\s*([0-9]{6})"));
            const QRegularExpressionMatch m = linkRe.match(m_hostInstallLinkTail);
            if (m.hasMatch()) {
                m_hostLinkPrompted = true;
                const QString code = m.captured(1);
                // This app provisioned the headless node, so its account is
                // exactly the one the new node should belong to — link it
                // automatically (adhoc #226) instead of making the user confirm
                // a code they can't even see on the remote screen. Fall back to
                // the manual prompt only when this app has no usable account to
                // attach it to.
                if (!accountOwner().isEmpty() && m_profileIdentity.isValid()) {
                    appendHostInstallLog(QString::fromUtf8(
                        "\nLinking node to your account (%1)\xE2\x80\xA6\n")
                        .arg(accountOwner()));
                    submitHostLinkCode(code);
                } else {
                    promptHostLinkCode(code);
                }
            }
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            appendHostInstallLog(QString::fromUtf8(
                "\n[error] Could not start sshpass/ssh. Install openssh-client "
                "and sshpass on this machine and try again.\n"));
    });
    connect(proc, &QProcess::finished, this,
            [this, ip, user, node, pass, onFinished, reinstall](int code, QProcess::ExitStatus status) {
                if (m_hostInstallButton)
                    m_hostInstallButton->setEnabled(true);
                const bool ok = status == QProcess::NormalExit && code == 0;
                const QString verb = reinstall ? QStringLiteral("Reinstall")
                                               : QStringLiteral("Install");
                if (ok) {
                    appendHostInstallLog(QString::fromUtf8(
                        "\n\xE2\x9C\x94 %1 finished. Node \"%2\" will join "
                        "the network and appear in the Mirror nodes list "
                        "shortly.\n").arg(verb, node));
                    if (m_hostInstallStatus)
                        m_hostInstallStatus->setText(QString::fromUtf8(
                            "\xE2\x9C\x94 %1ed on %2 as node \"%3\".")
                            .arg(reinstall ? QStringLiteral("Reinstall")
                                           : QStringLiteral("Install"),
                                 ip, node));
                    rememberHost(node, ip, user, pass);
                } else {
                    appendHostInstallLog(QString::fromUtf8(
                        "\n\xE2\x9C\x98 %1 failed (exit %2).\n").arg(verb).arg(code));
                    if (m_hostInstallStatus)
                        m_hostInstallStatus->setText(QString::fromUtf8(
                            "\xE2\x9C\x98 Install failed \xE2\x80\x94 see the "
                            "output above."));
                    rememberHost(node, ip, user, pass, QStringLiteral("install failed"));
                }
                if (m_hostInstallProcess) {
                    m_hostInstallProcess->deleteLater();
                    m_hostInstallProcess = nullptr;
                }
                if (onFinished)
                    onFinished(ok);
            });

    proc->start(QStringLiteral("sshpass"), sshArgs);
    // Feed sudo's password on stdin (consumed by `sudo -S`); ssh forwards it to
    // the remote shell. Closing the channel hands the installer a clean EOF.
    if (needSudo)
        proc->write((pass + QStringLiteral("\n")).toUtf8());
    // Direct-upload mode: the marker line then the release binary follow on the
    // same channel; the remote side skips to the marker and `cat`s the rest
    // into the temp file until the EOF the channel close below produces.
    // QProcess buffers the write and drains it as ssh accepts it, and
    // closeWriteChannel() only closes once everything queued has been written.
    if (uploadBinary) {
        proc->write((kHostUploadMarker + QStringLiteral("\n")).toUtf8());
        proc->write(uploadBytes);
    }
    proc->closeWriteChannel();
}

void MainWindow::runHostInstallAllFromBinary()
{
    if (!m_hostsTable || m_hostsTable->rowCount() == 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral("No saved hosts to install."));
        return;
    }
    if (m_hostInstallProcess &&
        m_hostInstallProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("An install is already running."));
        return;
    }
    QList<int> rows;
    rows.reserve(m_hostsTable->rowCount());
    for (int i = 0; i < m_hostsTable->rowCount(); ++i)
        rows.append(i);
    installNextHostFromBinary(rows);
}

void MainWindow::installNextHostFromBinary(QList<int> remainingRows)
{
    if (remainingRows.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Finished installing from binary on all hosts."));
        return;
    }
    const int row = remainingRows.takeFirst();
    loadHostIntoForm(row, 0);
    runHostInstall(/*forceUploadBinary=*/true, [this, remainingRows](bool /*ok*/) {
        installNextHostFromBinary(remainingRows);
    });
}

void MainWindow::runHostReinstallAllFromBinary()
{
    if (!m_hostsTable || m_hostsTable->rowCount() == 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral("No saved hosts to reinstall."));
        return;
    }
    if (m_hostInstallProcess &&
        m_hostInstallProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host session is already running."));
        return;
    }
    // Destructive: each host loses ALL of its ForkMesh data (identity key,
    // mirrors, chat) before the fresh install. Gate the whole run behind one
    // confirmation, the same way the per-host uninstall does.
    const int rowCount = m_hostsTable->rowCount();
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, QStringLiteral("Uninstall + reinstall all hosts"),
        QString::fromUtf8(
            "This will REMOVE ForkMesh and ALL of its data (identity key, "
            "mirrors, chat) from every one of your %1 saved host(s), then "
            "install a fresh copy from this app's binary and re-link each one "
            "to your account.\n\nThis cannot be undone. Continue?")
            .arg(rowCount),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes)
        return;
    QList<int> rows;
    rows.reserve(rowCount);
    for (int i = 0; i < rowCount; ++i)
        rows.append(i);
    reinstallNextHostFromBinary(rows);
}

void MainWindow::reinstallNextHostFromBinary(QList<int> remainingRows)
{
    if (remainingRows.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Finished reinstalling from binary on all hosts."));
        return;
    }
    const int row = remainingRows.takeFirst();
    loadHostIntoForm(row, 0);
    runHostInstall(
        /*forceUploadBinary=*/true,
        [this, remainingRows](bool /*ok*/) {
            reinstallNextHostFromBinary(remainingRows);
        },
        /*reinstall=*/true);
}

void MainWindow::runHostUninstall()
{
    if (m_hostInstallProcess &&
        m_hostInstallProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host session is already running."));
        return;
    }

    const QString ip = m_hostIpEdit ? m_hostIpEdit->text().trimmed() : QString();
    const QString user = m_hostUserEdit ? m_hostUserEdit->text().trimmed() : QString();
    const QString pass = m_hostPassEdit ? m_hostPassEdit->text() : QString();
    const QString node = m_hostNameEdit ? m_hostNameEdit->text().trimmed() : QString();
    if (ip.isEmpty() || user.isEmpty() || pass.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "Enter the host IP, SSH username, password and a node name "
                "first."));
        return;
    }
    const QString uninstallUrl = uninstallScriptUrl();
    if (uninstallUrl.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Could not resolve the uninstaller URL."));
        return;
    }

    auto shq = [](const QString &s) {
        QString out = s;
        out.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        return QStringLiteral("'") + out + QStringLiteral("'");
    };
    // FORKMESH_ASSUME_YES=1 skips the uninstaller's interactive "Type DELETE"
    // confirmation: this SSH session has no tty attached, so the script would
    // otherwise refuse to run non-interactively. The Qt-side confirmation
    // dialog (shown before this is called) is the real gate.
    const QString pipeline = QStringLiteral("curl -fsSL %1 | FORKMESH_ASSUME_YES=1 bash")
                                  .arg(shq(uninstallUrl));
    const bool needSudo = user != QStringLiteral("root");
    const QString remoteCmd =
        needSudo
            ? QStringLiteral("sudo -S -p '' -- bash -c %1").arg(shq(pipeline))
            : pipeline;

    const QStringList sshArgs = {
        QStringLiteral("-e"), QStringLiteral("ssh"),
        QStringLiteral("-o"), QStringLiteral("IdentitiesOnly=yes"),
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=no"),
        QStringLiteral("-o"), QStringLiteral("UserKnownHostsFile=/dev/null"),
        QStringLiteral("-o"), QStringLiteral("PreferredAuthentications=password"),
        QStringLiteral("-o"), QStringLiteral("PubkeyAuthentication=no"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=30"),
        user + QStringLiteral("@") + ip, remoteCmd};

    rememberHost(node, ip, user, pass, QStringLiteral("uninstalling"));

    m_hostInstallLog->clear();
    m_hostInstallLogCarry.clear();
    m_hostInstallLogFg = -1;
    m_hostInstallLogBold = false;
    appendHostInstallLog(
        QStringLiteral("$ ssh %1@%2 %3\n").arg(user, ip, remoteCmd));
    appendHostInstallLog(
        QStringLiteral("Connecting to %1 as %2 and running %3 ...\n\n")
            .arg(ip, user, uninstallUrl));
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(
            QString::fromUtf8("Uninstalling from %1\xE2\x80\xA6").arg(ip));
    if (m_hostInstallButton)
        m_hostInstallButton->setEnabled(false);

    auto *proc = new QProcess(this);
    m_hostInstallProcess = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Hand the SSH password to sshpass via the environment so it never lands in
    // argv or on disk.
    env.insert(QStringLiteral("SSHPASS"), pass);
    proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        appendHostInstallLog(QString::fromUtf8(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            appendHostInstallLog(QString::fromUtf8(
                "\n[error] Could not start sshpass/ssh. Install openssh-client "
                "and sshpass on this machine and try again.\n"));
    });
    connect(proc, &QProcess::finished, this,
            [this, ip, user, node, pass](int code, QProcess::ExitStatus status) {
                if (m_hostInstallButton)
                    m_hostInstallButton->setEnabled(true);
                const bool ok = status == QProcess::NormalExit && code == 0;
                if (ok) {
                    appendHostInstallLog(QString::fromUtf8(
                        "\n\xE2\x9C\x94 Uninstall finished. ForkMesh has been "
                        "removed from \"%1\".\n").arg(node));
                    if (m_hostInstallStatus)
                        m_hostInstallStatus->setText(QString::fromUtf8(
                            "\xE2\x9C\x94 Uninstalled from %1.").arg(ip));
                    rememberHost(node, ip, user, pass, QStringLiteral("uninstalled"));
                } else {
                    appendHostInstallLog(QString::fromUtf8(
                        "\n\xE2\x9C\x98 Uninstall failed (exit %1).\n").arg(code));
                    if (m_hostInstallStatus)
                        m_hostInstallStatus->setText(QString::fromUtf8(
                            "\xE2\x9C\x98 Uninstall failed \xE2\x80\x94 see the "
                            "output above."));
                    rememberHost(node, ip, user, pass, QStringLiteral("uninstall failed"));
                }
                if (m_hostInstallProcess) {
                    m_hostInstallProcess->deleteLater();
                    m_hostInstallProcess = nullptr;
                }
            });

    proc->start(QStringLiteral("sshpass"), sshArgs);
    // Feed sudo's password on stdin (consumed by `sudo -S`); ssh forwards it to
    // the remote shell. Closing the channel hands the uninstaller a clean EOF.
    if (needSudo)
        proc->write((pass + QStringLiteral("\n")).toUtf8());
    proc->closeWriteChannel();
}

// The freshly-installed node printed a link code (adhoc #53). Confirming here
// offers that code to the relay signed with THIS account's key, so the new
// node is attached to the user behind this account. The code is prefilled from
// the install stream but stays editable — the person at the keyboard can also
// type a code read off any machine's screen.
void MainWindow::promptHostLinkCode(const QString &code)
{
    const QString owner = accountOwner();
    if (owner.isEmpty() || !m_profileIdentity.isValid())
        return;
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Link new node to your account"));
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(
        QStringLiteral("The machine being installed shows a link code. Confirm "
                       "it below to register the new node under your account "
                       "(<b>%1</b>).").arg(owner.toHtmlEscaped()));
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *codeEdit = new QLineEdit(code);
    codeEdit->setAlignment(Qt::AlignCenter);
    codeEdit->setMaxLength(6);
    codeEdit->setPlaceholderText(QStringLiteral("6-digit code"));
    layout->addWidget(codeEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *linkButton =
        buttons->addButton(QStringLiteral("Link node"), QDialogButtonBox::AcceptRole);
    linkButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, codeEdit] {
        const QString entered = codeEdit->text().trimmed();
        static const QRegularExpression sixDigits(QStringLiteral("^[0-9]{6}$"));
        if (!sixDigits.match(entered).hasMatch())
            return;
        submitHostLinkCode(entered);
        dialog->accept();
    });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::submitHostLinkCode(const QString &code)
{
    const QString owner = accountOwner();
    if (owner.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-link-v1\n" + owner + "\n" + code + "\n" + ts).toUtf8();
    const QJsonObject body{{"nodeName", owner},
                           {"code", code},
                           {"ts", ts},
                           {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(accountsApiUrl("link-node"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        QString message;
        if (resp.value(QStringLiteral("linked")).toBool()) {
            message = QString::fromUtf8(
                "\n\xE2\x9C\x94 Node \"%1\" is now linked to your account.\n")
                .arg(resp.value(QStringLiteral("node")).toString());
        } else if (resp.value(QStringLiteral("pending")).toBool()) {
            message = QString::fromUtf8(
                "\n\xE2\x9C\x94 Link code accepted; the new node will be linked "
                "to your account as soon as it registers.\n");
        } else {
            message = QString::fromUtf8(
                "\n\xE2\x9C\x98 Could not link the new node (%1).\n")
                .arg(resp.value(QStringLiteral("error"))
                         .toString(QStringLiteral("network error")));
        }
        appendHostInstallLog(message);
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(message.trimmed());
    });
}

QWidget *MainWindow::buildHomeSection()
{
    auto *page = new QWidget;
    // Node profile is now its own section (index 9), so home just holds the repo
    // detail panel filling the full width.
    m_repoDetailSection = buildRepoDetailSection();
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_repoDetailSection);
    return page;
}

// Small helper: a labelled quick-action button for the "THIS NODE" toolbar at
// the top of the profile panel. Icon + text (not icon-only) so the action is
// clear at a glance; the tooltip carries the longer explanation.
static QPushButton *makeProfileActionButton(const QString &icon, const QString &label,
                                            const QString &tooltip)
{
    auto *button = new QPushButton(label);
    button->setObjectName("profileActionButton");
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->setFixedHeight(36);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setOcticon(button, icon, 16);
    return button;
}

// Section header label ("HOSTING", "SOLANA", …) used throughout the panel.
static QLabel *makeProfileSection(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionLabel");
    return label;
}

// QLabel word-wrap only breaks at whitespace: a long unbroken string (public
// key, wallet address) has no break points, so it reports one giant "word" as
// its minimum size and forces the whole scroll panel wider than the window
// instead of wrapping (the profile getting cut off on the right). Zero-width
// spaces give the layout break points without changing the copied text.
static QString withSoftBreaks(const QString &text, int chunkSize = 4)
{
    QString out;
    out.reserve(text.size() + text.size() / chunkSize);
    for (int i = 0; i < text.size(); ++i) {
        out += text.at(i);
        if ((i + 1) % chunkSize == 0 && i + 1 < text.size())
            out += QChar(0x200B);
    }
    return out;
}

QWidget *MainWindow::buildNodeProfileSection()
{
    // Center the profile scroll area horizontally with stretchers so the content
    // sits in a comfortable fixed-width column regardless of window width.
    auto *page = new QWidget;
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addStretch(1);
    outer->addWidget(buildNodeProfilePanel(), 0);
    outer->addStretch(1);
    return page;
}

QWidget *MainWindow::buildNodeProfilePanel()
{
    // The panel can grow tall (mirrors, hosting, Solana, QR), so it lives in a
    // scroll area; the section wrapper (buildNodeProfileSection) centers it.
    auto *scroll = new QScrollArea;
    scroll->setObjectName("nodeProfilePanel");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Width: wide enough for the two-column layout (identity/stats on the left,
    // hosting/keys/Solana on the right) side by side, narrow enough to look
    // centered on wide windows when flanked by the stretchers in
    // buildNodeProfileSection. The content's size hint comes out narrow (the
    // word-wrap labels report tiny minimums), so without a healthy minimum the
    // panel rendered ~450px wide and clipped the "THIS NODE" action row
    // (Logout), the Node ID key, the balance button and the verify-wallet
    // button on the right. Give both columns real room so everything shows.
    scroll->setMinimumWidth(760);
    scroll->setMaximumWidth(1180);
    m_nodeProfilePanel = scroll;

    auto *content = new QWidget;
    content->setObjectName("nodeProfileContent");

    auto *closeButton = new QPushButton(QString());
    closeButton->setObjectName("ghostButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip("Close");
    setOcticon(closeButton, "x", 16);
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::hideNodeProfile);
    auto *titleLabel = new QLabel("Node profile");
    titleLabel->setObjectName("sectionLabel");
    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(titleLabel);
    topRow->addStretch();
    topRow->addWidget(closeButton);

    // --- Square avatar, centered (rendered in rescaleProfileAvatar).
    m_profileAvatar = new QLabel;
    m_profileAvatar->setObjectName("profileBanner");
    m_profileAvatar->setFixedSize(96, 96);
    m_profileAvatar->setAlignment(Qt::AlignCenter);

    m_profileName = new QLabel;
    m_profileName->setObjectName("profileName");
    m_profileName->setAlignment(Qt::AlignHCenter);
    m_profileName->setWordWrap(true);
    m_profileStatus = new QLabel;
    m_profileStatus->setObjectName("statusLine");
    m_profileStatus->setAlignment(Qt::AlignHCenter);
    m_profileStatus->setTextFormat(Qt::RichText);
    m_profileNote = new QLabel;
    m_profileNote->setObjectName("statusLine");
    m_profileNote->setAlignment(Qt::AlignHCenter);
    m_profileNote->setWordWrap(true);

    m_profileMessageButton = new QPushButton("Message");
    m_profileMessageButton->setObjectName("ghostButton");
    m_profileMessageButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileMessageButton, "comment", 16);
    connect(m_profileMessageButton, &QPushButton::clicked, this, [this] {
        if (!m_profileNodeId.isEmpty())
            openDirectChat(m_profileNodeId, m_profileNodeName);
    });

    // Admin-only (adhoc #141): request ownership of someone else's node. This
    // only parks a pending marker on the target — the transfer only completes
    // once that node's own owner approves the prompt it gets on its own
    // heartbeat, so a hostile/compromised admin account still can't silently
    // seize a node.
    m_profileTakeOwnershipButton = new QPushButton("Take ownership");
    m_profileTakeOwnershipButton->setObjectName("ghostButton");
    m_profileTakeOwnershipButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileTakeOwnershipButton, "shield-check", 16);
    m_profileTakeOwnershipButton->setToolTip(
        "Request ownership of this node. It only transfers once the node's "
        "current owner approves the confirmation prompt it receives.");
    connect(m_profileTakeOwnershipButton, &QPushButton::clicked, this,
            &MainWindow::requestNodeOwnership);

    // --- "Get paid to mirror": the one opt-in entry into the crypto side, sitting
    // directly under the username on your own profile. The core flow never shows
    // it; clicking sets a Solana payout address (if unset) and activates this node
    // so it can host its mirrors and earn donations (see enablePaidMirroring).
    m_profileGetPaidButton = new QPushButton(QString::fromUtf8("Get paid to mirror"));
    m_profileGetPaidButton->setObjectName("primaryButton");
    m_profileGetPaidButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileGetPaidButton, "credit-card", 16);
    m_profileGetPaidButton->setToolTip(
        "Opt in to hosting your mirrors on the network and earning donations. "
        "Sets a Solana payout address and activates this node. Entirely optional "
        "\xE2\x80\x94 cloning, mirroring, issues and PRs work without it.");
    connect(m_profileGetPaidButton, &QPushButton::clicked, this,
            &MainWindow::enablePaidMirroring);

    // --- Node power switch: a plain on/off toggle sitting right under "Get paid
    // to mirror", since that's exactly what it controls — whether this whole
    // node is online and serving/collecting rewards, or parked offline. Used to
    // be a small pill in the top-right nav cluster; moved here so it reads as
    // the node's power switch rather than a stray status badge.
    m_profileOnlineSection = new QWidget;
    auto *onlineSectionLabel = makeProfileSection("THIS NODE'S POWER SWITCH");
    auto *nodeOnlineSwitch = new ToggleSwitch;
    m_nodeOnlineToggle = nodeOnlineSwitch;
    connect(nodeOnlineSwitch, &QAbstractButton::clicked, this,
            [this](bool checked) { setNodeOffline(!checked); });
    m_nodeOnlineStatusLabel = new QLabel;
    m_nodeRewardStatus = new QLabel;
    m_nodeRewardStatus->setObjectName("nodeRewardStatus");
    m_nodeRewardStatus->setWordWrap(true);
    m_nodeUptimeLabel = new QLabel;
    m_nodeUptimeLabel->setObjectName("nodeUptimeLabel");
    m_nodeUptimeLabel->setStyleSheet(
        QStringLiteral("color:#8b949e; font-size:10px; font-weight:600;"));
    m_nodeUptimeLabel->setToolTip(
        QStringLiteral("How long this node has been online this session"));
    auto *onlineStatusColumn = new QVBoxLayout;
    onlineStatusColumn->setContentsMargins(0, 0, 0, 0);
    onlineStatusColumn->setSpacing(0);
    onlineStatusColumn->addWidget(m_nodeOnlineStatusLabel);
    onlineStatusColumn->addWidget(m_nodeRewardStatus);
    onlineStatusColumn->addWidget(m_nodeUptimeLabel);
    auto *onlineSwitchRow = new QHBoxLayout;
    onlineSwitchRow->setContentsMargins(0, 0, 0, 0);
    onlineSwitchRow->setSpacing(10);
    onlineSwitchRow->addWidget(nodeOnlineSwitch);
    onlineSwitchRow->addLayout(onlineStatusColumn, 1);
    auto *onlineSectionLayout = new QVBoxLayout(m_profileOnlineSection);
    onlineSectionLayout->setContentsMargins(0, 0, 0, 0);
    onlineSectionLayout->setSpacing(6);
    onlineSectionLayout->addWidget(onlineSectionLabel);
    onlineSectionLayout->addLayout(onlineSwitchRow);

    // --- Self-only quick actions: a horizontal toolbar of labelled icon buttons
    // (rebuild, update, settings, logout) that used to be a stacked text menu,
    // then icon-only; labels came back so each action is clear at a glance.
    m_profileSelfActions = new QWidget;
    auto *selfLabel = makeProfileSection("THIS NODE");
    m_profileRebuildButton = makeProfileActionButton(
        "sync", "Rebuild",
        "Rebuild from the local source checkout and relaunch (fast; no update)");
    connect(m_profileRebuildButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_profileRebuildButton); quickRebuildRestart(); });
    m_profileUpdateButton = makeProfileActionButton(
        "download", "Update", "Pull the latest source, then rebuild and relaunch");
    connect(m_profileUpdateButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_profileUpdateButton); updateRebuildRestart(); });
    auto *selfSettingsButton = makeProfileActionButton("gear", "Settings",
                                                        "Open settings");
    connect(selfSettingsButton, &QPushButton::clicked, this,
            [this] { showSection(1); });
    auto *selfLogoutButton = makeProfileActionButton("sign-out", "Logout",
                                                      "Log out of this node");
    connect(selfLogoutButton, &QPushButton::clicked, this,
            [this] { leaveSession(); });
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(6);
    actionRow->addWidget(m_profileRebuildButton);
    actionRow->addWidget(m_profileUpdateButton);
    actionRow->addWidget(selfSettingsButton);
    actionRow->addWidget(selfLogoutButton);
    auto *selfLayout = new QVBoxLayout(m_profileSelfActions);
    selfLayout->setContentsMargins(0, 0, 0, 0);
    selfLayout->setSpacing(6);
    selfLayout->addWidget(selfLabel);
    selfLayout->addLayout(actionRow);

    // --- Headline stat tiles (self only): repos / mirrored / online / chats.
    m_profileStatGrid = new QWidget;
    auto makeTile = [](QLabel *&tile) {
        tile = new QLabel;
        tile->setObjectName("statTile");
        tile->setTextFormat(Qt::RichText);
        tile->setAlignment(Qt::AlignCenter);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };
    makeTile(m_profileTileRepos);
    makeTile(m_profileTileMirrored);
    makeTile(m_profileTileOnline);
    makeTile(m_profileTileChats);
    auto *tileRow = new QHBoxLayout(m_profileStatGrid);
    tileRow->setContentsMargins(0, 0, 0, 0);
    tileRow->setSpacing(6);
    tileRow->addWidget(m_profileTileRepos);
    tileRow->addWidget(m_profileTileMirrored);
    tileRow->addWidget(m_profileTileOnline);
    tileRow->addWidget(m_profileTileChats);

    // --- Details card: status / platform / version / uptime / key as a tidy
    // key:value table.
    auto *detailsLabel = makeProfileSection("DETAILS");
    m_profileDetails = new QLabel;
    m_profileDetails->setObjectName("profileCard");
    m_profileDetails->setTextFormat(Qt::RichText);
    m_profileDetails->setWordWrap(true);
    m_profileDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // --- Mirrors advertised by this node (with HEAD detail when available).
    m_profileMirrorsLabel = makeProfileSection("MIRRORS");
    m_profileMirrors = new QLabel;
    m_profileMirrors->setObjectName("profileCard");
    m_profileMirrors->setTextFormat(Qt::RichText);
    m_profileMirrors->setWordWrap(true);
    m_profileMirrors->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // --- User account: this node is key-bound on its own, but a person can own
    // many nodes. "Log in as a user" here proves the user's password so the
    // relay attaches this node to that user's account (self only).
    m_profileAccountSection = new QWidget;
    auto *accountLabel = makeProfileSection("USER ACCOUNT");
    m_profileAccountStatus = new QLabel;
    m_profileAccountStatus->setObjectName("statusLine");
    m_profileAccountStatus->setWordWrap(true);
    m_profileAccountStatus->setTextFormat(Qt::RichText);
    m_profileLinkUserButton = new QPushButton("Log in as a user");
    m_profileLinkUserButton->setObjectName("ghostButton");
    m_profileLinkUserButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileLinkUserButton, "person", 14);
    m_profileLinkUserButton->setToolTip(
        "Link this node to your user account by signing in. One user can own "
        "many nodes.");
    connect(m_profileLinkUserButton, &QPushButton::clicked, this,
            &MainWindow::promptLinkNodeToUser);
    auto *accountLayout = new QVBoxLayout(m_profileAccountSection);
    accountLayout->setContentsMargins(0, 0, 0, 0);
    accountLayout->setSpacing(6);
    accountLayout->addWidget(accountLabel);
    accountLayout->addWidget(m_profileAccountStatus);
    accountLayout->addWidget(m_profileLinkUserButton, 0, Qt::AlignLeft);

    // --- Per-repo hosting stats relocated from the repo detail view.
    m_profileHostingLabel = makeProfileSection("HOSTING");
    m_profileHosting = new QLabel;
    m_profileHosting->setObjectName("profileCard");
    m_profileHosting->setWordWrap(true);
    m_profileHosting->setTextFormat(Qt::RichText);
    m_profileHosting->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // --- Node ID = the node's Ed25519 public key. Selectable so it can be copied.
    auto *nodeKeyLabel = makeProfileSection("NODE ID (PUBLIC KEY)");
    m_profileNodeKey = new QLabel;
    m_profileNodeKey->setObjectName("profileMono");
    m_profileNodeKey->setWordWrap(true);
    m_profileNodeKey->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyKey = new QPushButton("Copy node ID");
    copyKey->setObjectName("ghostButton");
    copyKey->setCursor(Qt::PointingHandCursor);
    setOcticon(copyKey, "copy", 14);
    connect(copyKey, &QPushButton::clicked, this, [this] {
        if (!m_profileNodeId.isEmpty()) {
            QApplication::clipboard()->setText(m_profileNodeId);
            logSystem("Copied node ID to clipboard.");
        }
    });
    // Browser-based linking (adhoc #120): opens a node-signed grant URL in the
    // default browser so the user logged in on forkmesh.com takes ownership of
    // this node without typing anything.
    m_profileLinkBrowserButton =
        new QPushButton("Link this node to your account");
    m_profileLinkBrowserButton->setObjectName("ghostButton");
    m_profileLinkBrowserButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileLinkBrowserButton, "link", 14);
    m_profileLinkBrowserButton->setToolTip(
        "Open forkmesh.com in your browser and attach this node to the user "
        "account you're logged in as there. One user can own many nodes.");
    connect(m_profileLinkBrowserButton, &QPushButton::clicked, this,
            &MainWindow::openLinkNodeInBrowser);

    // --- Solana section: address, QR, on-demand balance.
    m_profileSolanaSection = new QWidget;
    auto *solanaLabel = makeProfileSection("SOLANA");
    m_profileSolanaAddr = new QLabel;
    m_profileSolanaAddr->setObjectName("profileMono");
    m_profileSolanaAddr->setWordWrap(true);
    m_profileSolanaAddr->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyAddr = new QPushButton("Copy address");
    copyAddr->setObjectName("ghostButton");
    copyAddr->setCursor(Qt::PointingHandCursor);
    setOcticon(copyAddr, "copy", 14);
    connect(copyAddr, &QPushButton::clicked, this, [this] {
        if (!m_profileSolanaValue.isEmpty()) {
            QApplication::clipboard()->setText(m_profileSolanaValue);
            logSystem("Copied Solana address to clipboard.");
        }
    });
    m_profileQr = new QLabel;
    m_profileQr->setObjectName("profileQr");
    m_profileQr->setAlignment(Qt::AlignCenter);

    m_profileBalance = new QLabel("\xE2\x80\x94"); // em dash until checked
    m_profileBalance->setObjectName("channelTitle");
    m_profileBalanceButton = new QPushButton("Check balance");
    m_profileBalanceButton->setObjectName("ghostButton");
    m_profileBalanceButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileBalanceButton, "sync", 14);
    m_profileBalanceButton->setToolTip(
        "Query the Solana network through public JSON-RPC for this wallet's "
        "balance. This sends the address to the endpoint it connects to.");
    connect(m_profileBalanceButton, &QPushButton::clicked, this,
            &MainWindow::checkNodeBalance);
    addRefreshSpin(m_profileBalanceButton);
    auto *balanceRow = new QHBoxLayout;
    balanceRow->setContentsMargins(0, 0, 0, 0);
    balanceRow->addWidget(m_profileBalance, 1);
    balanceRow->addWidget(m_profileBalanceButton);

    auto *solanaLayout = new QVBoxLayout(m_profileSolanaSection);
    solanaLayout->setContentsMargins(0, 8, 0, 0);
    solanaLayout->setSpacing(6);
    solanaLayout->addWidget(solanaLabel);
    solanaLayout->addWidget(m_profileSolanaAddr);
    solanaLayout->addWidget(copyAddr, 0, Qt::AlignLeft);
    solanaLayout->addWidget(m_profileQr, 0, Qt::AlignCenter);
    auto *balLabel = makeProfileSection("BALANCE");
    solanaLayout->addWidget(balLabel);
    solanaLayout->addLayout(balanceRow);

    // Revenue-sharing eligibility (self only): verify >=0.001 SOL has reached
    // this wallet so the network knows the address is active.
    m_profileEligibility = new QLabel;
    m_profileEligibility->setObjectName("statusLine");
    m_profileEligibility->setWordWrap(true);
    m_profileEligibility->setTextFormat(Qt::RichText);
    m_profileVerifyButton = new QPushButton("Verify wallet (deposit >= 0.001 SOL)");
    m_profileVerifyButton->setObjectName("ghostButton");
    m_profileVerifyButton->setCursor(Qt::PointingHandCursor);
    connect(m_profileVerifyButton, &QPushButton::clicked, this,
            &MainWindow::verifyWallet);
    solanaLayout->addWidget(m_profileEligibility);
    solanaLayout->addWidget(m_profileVerifyButton, 0, Qt::AlignLeft);

    // Two-column body: identity/stats on the left, hosting/keys/Solana (what
    // used to be one long stack at the bottom) on the right, side by side. Both
    // columns share a common top edge so they read as one panel rather than
    // two independently-scrolled halves.
    auto *leftColumn = new QVBoxLayout;
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(6);
    leftColumn->addWidget(m_profileAvatar, 0, Qt::AlignHCenter);
    leftColumn->addWidget(m_profileName);
    leftColumn->addWidget(m_profileGetPaidButton, 0, Qt::AlignHCenter);
    leftColumn->addWidget(m_profileOnlineSection);
    leftColumn->addWidget(m_profileStatus);
    leftColumn->addWidget(m_profileNote);
    leftColumn->addWidget(m_profileMessageButton, 0, Qt::AlignHCenter);
    leftColumn->addWidget(m_profileTakeOwnershipButton, 0, Qt::AlignHCenter);
    leftColumn->addWidget(m_profileStatGrid);
    leftColumn->addWidget(detailsLabel);
    leftColumn->addWidget(m_profileDetails);
    leftColumn->addWidget(m_profileMirrorsLabel);
    leftColumn->addWidget(m_profileMirrors);
    leftColumn->addWidget(m_profileAccountSection);
    leftColumn->addStretch();

    auto *rightColumn = new QVBoxLayout;
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(6);
    rightColumn->addWidget(m_profileHostingLabel);
    rightColumn->addWidget(m_profileHosting);
    rightColumn->addWidget(nodeKeyLabel);
    rightColumn->addWidget(m_profileNodeKey);
    auto *nodeKeyActions = new QHBoxLayout;
    nodeKeyActions->setContentsMargins(0, 0, 0, 0);
    nodeKeyActions->setSpacing(6);
    nodeKeyActions->addWidget(copyKey);
    nodeKeyActions->addWidget(m_profileLinkBrowserButton);
    nodeKeyActions->addStretch();
    rightColumn->addLayout(nodeKeyActions);
    rightColumn->addWidget(m_profileSolanaSection);
    rightColumn->addStretch();

    auto *columnsRow = new QHBoxLayout;
    columnsRow->setContentsMargins(0, 0, 0, 0);
    columnsRow->setSpacing(28);
    columnsRow->addLayout(leftColumn, 1);
    columnsRow->addLayout(rightColumn, 1);

    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(6);
    layout->addLayout(topRow);
    layout->addWidget(m_profileSelfActions); // "THIS NODE" actions pinned up top
    layout->addLayout(columnsRow);

    scroll->setWidget(content);
    // The online switch, status line and uptime label are created here rather
    // than in the always-visible top bar now, so give them their initial state
    // as soon as they exist instead of waiting for the next online/offline event.
    updateNodeOnlineControls();
    return scroll;
}

void MainWindow::hideNodeProfile()
{
    m_profileNodeId.clear();
    m_profileNodeName.clear();
    m_profileSolanaValue.clear();
    showSection(0);
}

void MainWindow::rescaleProfileAvatar()
{
    if (!m_profileAvatar)
        return;
    const int side = qMax(m_profileAvatar->width(), 1);
    QPixmap src = m_profileAvatarSource;
    if (src.isNull())
        src = letterFavicon(m_profileNodeName);
    m_profileAvatar->setPixmap(roundedRectPixmap(src, side, 20));
}

void MainWindow::showNodeProfile(const QString &nodeId, const QString &nodeName)
{
    if (!m_nodeProfilePanel)
        return;

    // Resolve the node from the live roster (by id, then by name).
    MemberInfo info;
    bool found = false;
    for (const MemberInfo &m : std::as_const(m_homeRoster)) {
        if ((!nodeId.isEmpty() && m.id == nodeId) ||
            (nodeId.isEmpty() && m.name == nodeName)) {
            info = m;
            found = true;
            break;
        }
    }
    if (!found) {
        info.id = nodeId;
        info.name = nodeName;
        // Offline / empty roster: still recognise our own node so the avatar's
        // profile keeps its restart / settings / logout actions.
        info.self = !nodeId.isEmpty() && nodeId == m_profileIdentity.publicKey();
    }
    // Self's Solana address may only live in local settings.
    QString solana = info.solanaAddress.trimmed();
    if (info.self && solana.isEmpty())
        solana = savedSolanaAddress();

    m_profileNodeId = info.id;
    m_profileNodeName = info.name;
    m_profileSolanaValue = solana;
    m_profileIsSelf = info.self;

    // Full-width avatar banner: real avatar if we have one, else a letter tile.
    QPixmap avatar = m_avatars.value(info.id);
    if (avatar.isNull())
        avatar = letterFavicon(info.name);
    m_profileAvatarSource = avatar;
    rescaleProfileAvatar();

    // Show a crown next to your own name when this node is an admin (we only
    // know our own admin status, so it's self-only). U+1F451 (👑).
    const QString crown =
        (info.self && m_isAdmin) ? QString::fromUtf8(" \xF0\x9F\x91\x91") : QString();
    m_profileName->setText(info.name.toHtmlEscaped() +
                           (info.self ? " (you)" : QString()) + crown);
    const bool online = info.self ? (m_backend != nullptr) : info.online;
    m_profileStatus->setText(
        QString::fromUtf8("<span style='color:%1'>\xE2\x97\x8F</span> %2")
            .arg(online ? "#3fb950" : "#8b949e", online ? "Online" : "Offline"));

    // Node ID = Ed25519 public key. For yourself, fall back to our own key when
    // the roster entry has no id yet.
    QString nodeKey = info.id;
    if (info.self && nodeKey.isEmpty())
        nodeKey = m_profileIdentity.publicKey();
    m_profileNodeId = nodeKey; // keep the copy button in sync with what's shown
    m_profileNodeKey->setText(nodeKey.isEmpty() ? QStringLiteral("unknown")
                                                : withSoftBreaks(nodeKey));

    // --- Headline stat tiles (self only): repos / mirrored / online / chats.
    if (info.self) {
        int repos = 0, mirrored = 0, onlineRepos = 0;
        for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
            if (repo.previewOnly)
                continue;
            ++repos;
            if (repo.lastSyncMs > 0 ||
                (!repo.mirrorPath.isEmpty() && QDir(repo.mirrorPath).exists()))
                ++mirrored;
            if (repo.publishedAtMs > 0 || repo.publishToNetwork)
                ++onlineRepos;
        }
        auto tile = [](QLabel *label, int n, const QString &caption) {
            label->setText(
                QStringLiteral(
                    "<span style='font-size:18px;font-weight:700'>%1</span>"
                    "<br><span style='font-size:9px;letter-spacing:1px;"
                    "color:#8b949e'>%2</span>")
                    .arg(n)
                    .arg(caption));
        };
        tile(m_profileTileRepos, repos, QStringLiteral("REPOS"));
        tile(m_profileTileMirrored, mirrored, QStringLiteral("MIRRORED"));
        tile(m_profileTileOnline, onlineRepos, QStringLiteral("ONLINE"));
        tile(m_profileTileChats, m_channels.size(), QStringLiteral("CHATS"));
    }
    m_profileStatGrid->setVisible(info.self);

    // --- Details card: a tidy key:value table (status, platform, version,
    // uptime/total for self, short key). Only rows we actually know are shown.
    auto detailRow = [](const QString &key, const QString &value) {
        return QStringLiteral(
                   "<tr><td style='color:#8b949e;padding:1px 14px 1px 0;"
                   "white-space:nowrap'>%1</td>"
                   "<td style='padding:1px 0'>%2</td></tr>")
            .arg(key, value);
    };
    QStringList rows;
    rows << detailRow(QStringLiteral("Status"),
                      online ? QStringLiteral("Online") : QStringLiteral("Offline"));
    if (!info.platform.isEmpty())
        rows << detailRow(QStringLiteral("Platform"), info.platform.toHtmlEscaped());
    if (!info.version.isEmpty())
        rows << detailRow(QStringLiteral("Version"),
                          QStringLiteral("v%1").arg(info.version.toHtmlEscaped()));
    if (info.self) {
        const qint64 sessionMs =
            m_connectedAtMs > 0
                ? QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs
                : 0;
        rows << detailRow(QStringLiteral("Uptime"), formatDuration(sessionMs));
        rows << detailRow(QStringLiteral("Total uptime"),
                          formatDuration(m_totalConnectionMs + sessionMs));
        rows << detailRow(QStringLiteral("Channels"),
                          QString::number(m_channels.size()));
    }
    const QString shortKey =
        nodeKey.size() > 16
            ? nodeKey.left(8) + QString::fromUtf8("\xE2\x80\xA6") + nodeKey.right(6)
            : nodeKey;
    if (!shortKey.isEmpty())
        rows << detailRow(QStringLiteral("Key"), shortKey.toHtmlEscaped());
    m_profileDetails->setText(
        QStringLiteral("<table cellspacing='0' cellpadding='0'>%1</table>")
            .arg(rows.join(QString())));

    // --- Mirrors advertised by this node, with HEAD detail when available.
    if (info.mirrors.isEmpty()) {
        m_profileMirrorsLabel->setVisible(false);
        m_profileMirrors->setVisible(false);
    } else {
        m_profileMirrorsLabel->setText(
            QStringLiteral("MIRRORS (%1)").arg(formatCount(info.mirrors.size())));
        QHash<QString, MirrorAdvert> byKey;
        for (const MirrorAdvert &d : std::as_const(info.mirrorDetails)) {
            if (!d.source.isEmpty())
                byKey.insert(d.source, d);
            if (!d.ownerName.isEmpty())
                byKey.insert(d.ownerName, d);
        }
        QStringList lines;
        for (const QString &m : std::as_const(info.mirrors)) {
            const MirrorAdvert d = byKey.value(m);
            QString detail;
            if (!d.branch.isEmpty() || !d.commit.isEmpty()) {
                QStringList parts;
                if (!d.branch.isEmpty())
                    parts << d.branch.toHtmlEscaped();
                if (!d.commit.isEmpty())
                    parts << QStringLiteral("@ %1").arg(d.commit.left(7));
                if (d.updatedMs > 0)
                    parts << formatRepoDate(d.updatedMs);
                detail = QStringLiteral(
                             "<br><span style='color:#8b949e'>%1</span>")
                             .arg(parts.join(QString::fromUtf8(" \xC2\xB7 ")));
            }
            lines << QStringLiteral("<b>%1</b>%2").arg(m.toHtmlEscaped(), detail);
        }
        m_profileMirrors->setText(lines.join(QStringLiteral("<br>")));
        m_profileMirrorsLabel->setVisible(true);
        m_profileMirrors->setVisible(true);
    }

    // "USER ACCOUNT": self only. Show whether this node is already attached to a
    // user, and offer "Log in as a user" to attach it when it isn't. Only a
    // registered (key-bound) node can be linked, so gate the button on that.
    // The browser-link button rides the node-ID card but is a self-only action
    // too; refreshProfileAccountStatus refines it (hidden again once linked).
    if (m_profileLinkBrowserButton)
        m_profileLinkBrowserButton->setVisible(info.self);
    if (m_profileAccountSection) {
        m_profileAccountSection->setVisible(info.self);
        if (info.self)
            refreshProfileAccountStatus();
    }

    // Per-repo hosting stats (served/clones/hosted-since/last-sync), self only.
    refreshProfileHostingStats();

    // Discovery note (e.g. "(discovered)"), shown only when present.
    m_profileNote->setText(info.note.toHtmlEscaped());
    m_profileNote->setVisible(!info.note.trimmed().isEmpty());

    m_profileMessageButton->setVisible(!info.self && !info.id.isEmpty());
    // Admin-only takeover request; hidden entirely for non-admins and for your
    // own profile (nothing to take ownership of there).
    if (m_profileTakeOwnershipButton)
        m_profileTakeOwnershipButton->setVisible(
            !info.self && m_isAdmin && !info.name.isEmpty());
    // Restart / settings / logout only make sense for your own node.
    if (m_profileSelfActions)
        m_profileSelfActions->setVisible(info.self);
    // The online/offline power switch only controls your own node.
    if (m_profileOnlineSection)
        m_profileOnlineSection->setVisible(info.self);

    // "Get paid to mirror" is a self-only opt-in CTA. Once this node is activated
    // (active account + a payout address set) it flips to an "earning" label so
    // the button doubles as a status line; it stays clickable to re-arm hosting.
    if (m_profileGetPaidButton) {
        const bool earning = hasActiveAccountSession() && !solana.isEmpty();
        m_profileGetPaidButton->setVisible(info.self);
        m_profileGetPaidButton->setText(
            earning ? QString::fromUtf8("\xE2\x9C\x93 Getting paid to mirror")
                    : QString::fromUtf8("Get paid to mirror"));
    }

    // Wallet verification + eligibility badge are shown only on your own profile.
    if (m_profileVerifyButton)
        m_profileVerifyButton->setVisible(info.self);
    if (m_profileEligibility) {
        m_profileEligibility->setVisible(info.self);
        if (info.self)
            m_profileEligibility->setText(
                m_accountSolanaVerified
                    ? QString::fromUtf8("<span style='color:#3fb950'>Active "
                                     "\xC2\xB7 revenue-sharing eligible</span>")
                    : QString::fromUtf8("<span style='color:#d29922'>Not yet eligible "
                                     "\xE2\x80\x94 deposit >= 0.001 SOL and "
                                     "verify.</span>"));
    }

    // Solana address + QR + reset balance.
    if (solana.isEmpty()) {
        m_profileSolanaSection->hide();
    } else {
        m_profileSolanaSection->show();
        m_profileSolanaAddr->setText(withSoftBreaks(solana));
        const QImage qr = QrCode::encodeToImage(QStringLiteral("solana:%1").arg(solana), 4, 3);
        if (!qr.isNull())
            m_profileQr->setPixmap(QPixmap::fromImage(qr));
        m_profileQr->setVisible(!qr.isNull());
        m_profileBalance->setText(info.solanaBalance.trimmed().isEmpty()
                                      ? QString::fromUtf8("\xE2\x80\x94")
                                      : info.solanaBalance.trimmed());
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Check balance");
    }

    // Show the profile as its own full page (section 9 in m_sectionStack).
    showSection(9);
}

void MainWindow::refreshProfileHostingStats()
{
    if (!m_profileHosting || !m_profileHostingLabel)
        return;
    // Only meaningful for your own node — served/clone counts are tracked locally.
    if (!m_profileIsSelf) {
        m_profileHosting->clear();
        m_profileHosting->setVisible(false);
        m_profileHostingLabel->setVisible(false);
        return;
    }
    QStringList lines;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.previewOnly)
            continue;
        const QPair<int, int> stats = m_repoStats.value(repo.owner + "/" + repo.name);
        lines << QString::fromUtf8(
                     "<b>%1</b> \xC2\xB7 %2 served \xC2\xB7 %3 clone%4<br>"
                     "<span style='color:#8b949e'>hosted since %5 \xC2\xB7 "
                     "last sync %6</span>")
                     .arg(repo.name.toHtmlEscaped())
                     .arg(stats.first)
                     .arg(stats.second)
                     .arg(stats.second == 1 ? QString() : QStringLiteral("s"),
                          formatRepoDate(repo.hostedSinceMs),
                          formatRepoDate(repo.lastSyncMs));
    }
    m_profileHosting->setText(
        lines.isEmpty() ? QStringLiteral("No hosted repositories yet.")
                        : lines.join(QStringLiteral("<br>")));
    m_profileHosting->setVisible(true);
    m_profileHostingLabel->setVisible(true);
}

// A JSON array of node names -> a clean QStringList (non-empty strings only).
static QStringList profileNodesFromJson(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        const QString name = v.toString().trimmed();
        if (!name.isEmpty())
            out << name;
    }
    return out;
}

// Render a "<b>a</b>, <b>b</b>" list of the nodes linked to this user account,
// marking the one we're viewing from ("(this node)") so the fleet is legible.
QString MainWindow::linkedNodesHtml() const
{
    if (m_profileLinkedNodes.isEmpty())
        return QString();
    const QString self = accountOwner();
    QStringList parts;
    for (const QString &n : m_profileLinkedNodes) {
        QString label = QStringLiteral("<b>%1</b>").arg(n.toHtmlEscaped());
        if (n == self)
            label += QString::fromUtf8(" <span style='color:#8b949e'>(this "
                                       "node)</span>");
        parts << label;
    }
    return parts.join(QStringLiteral(", "));
}

// Paint the "USER ACCOUNT" section from the currently-believed state (no
// network). Three shapes:
//  - this node is linked to a parent user  -> "Linked to user X" + fleet;
//  - this node IS the user account         -> list the nodes it owns;
//  - a bare key-bound node, not linked yet  -> offer "Log in as a user".
void MainWindow::renderProfileAccountStatus()
{
    if (!m_profileAccountStatus || !m_profileLinkUserButton)
        return;
    // The browser-link button is ALWAYS offered on your own profile — even when
    // this node is already linked or is itself a user account — because the
    // grant flow overrides the current association: whoever authenticates in
    // the browser takes possession of this node (adhoc #120 follow-up).
    if (m_profileLinkBrowserButton) {
        m_profileLinkBrowserButton->setVisible(true);
        m_profileLinkBrowserButton->setEnabled(true);
    }
    const QString fleet = linkedNodesHtml();
    const QString fleetLine =
        fleet.isEmpty()
            ? QString()
            : QString::fromUtf8("<br><span style='color:#8b949e'>Nodes on "
                                "this account:</span> %1").arg(fleet);
    if (!m_nodeOwnerUser.trimmed().isEmpty()) {
        // A child node attached to a separate user account.
        m_profileAccountStatus->setText(
            QString::fromUtf8(
                "<span style='color:#3fb950'>\xE2\x9C\x94 Linked to user "
                "<b>%1</b></span>%2")
                .arg(m_nodeOwnerUser.toHtmlEscaped(), fleetLine));
        m_profileLinkUserButton->setText("Linked to a user");
        m_profileLinkUserButton->setVisible(false);
    } else if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty()) {
        // This account is itself a user (it has login credentials); it can't
        // be "linked to a user" — instead it OWNS nodes. Show them and hide
        // the login button (linking is driven from each child node's app).
        const QString body =
            m_profileLinkedNodes.isEmpty()
                ? QString::fromUtf8(
                      "No other nodes are linked yet \xE2\x80\x94 open "
                      "another node's app and use \"Log in as a user\" or "
                      "\"Link this node to your account\" there to attach it "
                      "to this account.")
                : QString::fromUtf8(
                      "<span style='color:#8b949e'>Nodes linked to this "
                      "account (%1):</span> %2")
                      .arg(m_profileLinkedNodes.size())
                      .arg(fleet);
        m_profileAccountStatus->setText(
            QString::fromUtf8(
                "<span style='color:#3fb950'>\xE2\x9C\x94 This is your user "
                "account</span><br>%1").arg(body));
        m_profileLinkUserButton->setVisible(false);
    } else {
        // While a browser link grant is being watched (adhoc #120), say so
        // instead of "isn't linked yet" — the repaint on every poll would
        // otherwise clobber the context of what the user just started.
        m_profileAccountStatus->setText(
            m_linkGrantPollsLeft > 0
                ? QString::fromUtf8(
                      "Finishing in your browser \xE2\x80\xA6 this node will "
                      "be attached to the user logged in on the website.")
                : QString::fromUtf8(
                      "This node isn't linked to a user account yet. One user "
                      "can own many nodes \xE2\x80\x94 log in to attach this "
                      "node."));
        m_profileLinkUserButton->setText("Log in as a user");
        m_profileLinkUserButton->setEnabled(true);
        m_profileLinkUserButton->setVisible(true);
    }
}

void MainWindow::refreshProfileAccountStatus()
{
    if (!m_profileAccountStatus || !m_profileLinkUserButton)
        return;
    // Repaint from cached state, then refresh from the relay so a link made on
    // another device shows here.
    const QString node = accountOwner();
    // A node must be a registered, key-bound account before the relay can attach
    // it to a user (it links by node name + this node's key). Until then, nudge
    // the user to register/join and disable the button.
    if (node.isEmpty() || !hasActiveAccountSession()) {
        m_nodeOwnerUser.clear();
        m_profileLinkedNodes.clear();
        m_profileIsUserAccount = false;
        m_profileAccountStatus->setText(QString::fromUtf8(
            "Register this node first (see \"Get paid to mirror\") to link it to "
            "a user account."));
        m_profileLinkUserButton->setText("Log in as a user");
        m_profileLinkUserButton->setEnabled(false);
        m_profileLinkUserButton->setVisible(true);
        if (m_profileLinkBrowserButton) {
            m_profileLinkBrowserButton->setVisible(true);
            m_profileLinkBrowserButton->setEnabled(false);
        }
        return;
    }
    renderProfileAccountStatus();

    QNetworkReply *reply =
        m_networkAccess->get(QNetworkRequest(accountsApiUrl(node)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, node]() {
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        // Ignore a stale reply if the profile has since moved off this node.
        if (!m_profileIsSelf || accountOwner() != node)
            return;
        if (!resp.value(QStringLiteral("exists")).toBool()) {
            renderProfileAccountStatus();
            return;
        }
        m_nodeOwnerUser = resp.value(QStringLiteral("owner")).toString();
        m_profileIsUserAccount =
            resp.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("user");
        m_profileLinkedNodes =
            profileNodesFromJson(resp.value(QStringLiteral("nodes")));
        // A child node only knows its own account; fetch the owning user to list
        // the sibling nodes too, so the whole fleet shows on any node's profile.
        // (This second hop only repaints — it must NOT re-enter the GET above, or
        // this node's empty own-nodes list would re-trigger the fetch forever.)
        if (m_profileLinkedNodes.isEmpty() && !m_nodeOwnerUser.trimmed().isEmpty())
            fetchLinkedNodesFromOwner(node, m_nodeOwnerUser);
        renderProfileAccountStatus();
    });
}

// Second-hop lookup for a child node: the owning user's account carries the
// full nodes list (this node + its siblings). Repaints (only) once it lands.
void MainWindow::fetchLinkedNodesFromOwner(const QString &node,
                                           const QString &owner)
{
    QNetworkReply *reply =
        m_networkAccess->get(QNetworkRequest(accountsApiUrl(owner)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, node]() {
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (!m_profileIsSelf || accountOwner() != node)
            return;
        if (resp.value(QStringLiteral("exists")).toBool())
            m_profileLinkedNodes =
                profileNodesFromJson(resp.value(QStringLiteral("nodes")));
        renderProfileAccountStatus();
    });
}

void MainWindow::promptLinkNodeToUser()
{
    if (accountOwner().isEmpty() || !hasActiveAccountSession()) {
        logSystem("Register this node before linking it to a user account.");
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Log in as a user"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(QString::fromUtf8(
        "Log in with your ForkMesh user account to attach this node "
        "(<b>%1</b>) to it. One user can own many nodes.")
        .arg(accountOwner().toHtmlEscaped()));
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *idEdit = new QLineEdit;
    idEdit->setPlaceholderText(QStringLiteral("Email or username"));
    layout->addWidget(idEdit);
    auto *pwEdit = new QLineEdit;
    pwEdit->setEchoMode(QLineEdit::Password);
    pwEdit->setPlaceholderText(QStringLiteral("Password"));
    layout->addWidget(pwEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *linkButton =
        buttons->addButton(QStringLiteral("Link node"), QDialogButtonBox::AcceptRole);
    linkButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog,
            [this, dialog, idEdit, pwEdit] {
                const QString identifier = idEdit->text().trimmed();
                const QString password = pwEdit->text();
                if (identifier.isEmpty() || password.isEmpty())
                    return;
                submitLinkNodeToUser(identifier, password);
                dialog->accept();
            });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::submitLinkNodeToUser(const QString &identifier,
                                      const QString &password)
{
    const QString node = accountOwner();
    if (node.isEmpty() || !m_profileIdentity.isValid())
        return;
    const QString id = identifier.trimmed().toLower();
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    // Sign with THIS node's key: the relay checks the signature against the
    // node's recorded pubkey and the password against the user, so holding both
    // secrets is the whole authorization (no confirmation code needed).
    const QByteArray canonical =
        ("forkmesh-link-self-v1\n" + node + "\n" + id + "\n" + ts).toUtf8();
    const QJsonObject body{{"nodeName", node},
                           {"identifier", id},
                           {"password", password},
                           {"ts", ts},
                           {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(accountsApiUrl("link-self"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (m_profileLinkUserButton) {
        m_profileLinkUserButton->setEnabled(false);
        m_profileLinkUserButton->setText(QString::fromUtf8("Linking\xE2\x80\xA6"));
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (resp.value(QStringLiteral("linked")).toBool()) {
            m_nodeOwnerUser = resp.value(QStringLiteral("user")).toString();
            m_profileLinkedNodes =
                profileNodesFromJson(resp.value(QStringLiteral("nodes")));
            logSystem("Account: this node is now linked to user \"" +
                      m_nodeOwnerUser + "\".");
            // refreshProfileAccountStatus repaints to the linked state (and picks
            // up the fleet) from the values just set above.
            refreshProfileAccountStatus();
            return;
        }
        // A failed link previously vanished into the system log, so it "seemed to
        // do nothing". Surface the reason right in the account section and reset
        // the button so it can be retried.
        const QString code = resp.value(QStringLiteral("error"))
                                 .toString(QStringLiteral("network error"));
        m_profileAccountStatus->setText(QString::fromUtf8(
            "<span style='color:#f85149'>\xE2\x9C\x98 Couldn't link this node: "
            "%1</span>").arg(linkErrorMessage(code)));
        logSystem("Account: could not link this node to a user (" + code + ").");
        if (m_profileLinkUserButton) {
            m_profileLinkUserButton->setText("Log in as a user");
            m_profileLinkUserButton->setEnabled(true);
            m_profileLinkUserButton->setVisible(true);
        }
    });
}

// Turn a link-self error code from the relay into a one-line explanation for the
// account section (the raw code still goes to the system log for diagnostics).
QString MainWindow::linkErrorMessage(const QString &code) const
{
    if (code == QStringLiteral("invalid_credentials"))
        return QStringLiteral("wrong email/username or password.");
    if (code == QStringLiteral("cannot_link_self"))
        return QStringLiteral("this node is already your user account \xE2\x80\x94 "
                              "no linking needed.");
    if (code == QStringLiteral("node_already_owned"))
        return QStringLiteral("this node is already linked to another account.");
    if (code == QStringLiteral("not_a_node"))
        return QStringLiteral("that account can log in on its own, so it can't be "
                              "attached as a node.");
    if (code == QStringLiteral("no_such_node"))
        return QStringLiteral("this node isn't registered with the relay yet.");
    if (code == QStringLiteral("bad_signature") ||
        code == QStringLiteral("unauthorized"))
        return QStringLiteral("this node's key couldn't be verified.");
    return code + QStringLiteral(".");
}

// "Link this node to your account" (adhoc #120): sign a short-lived grant with
// this node's key and open it as a dashboard URL in the default browser. The
// signature proves node-key control and consents to the link, so whichever
// user is logged in on forkmesh.com there takes ownership without typing
// anything (the in-browser counterpart of "Log in as a user").
void MainWindow::openLinkNodeInBrowser()
{
    const QString node = accountOwner();
    if (node.isEmpty() || !hasActiveAccountSession() ||
        (!m_profileIdentity.isValid() && !m_profileIdentity.load())) {
        logSystem("Register this node first (see \"Get paid to mirror\") to "
                  "link it to a user account.");
        return;
    }
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-link-grant-v1\n" + node + "\n" + ts).toUtf8();
    QUrl url = catalogApiUrl(); // http(s) on the mainnode host
    url.setPath(QStringLiteral("/dashboard"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("link_node"), node);
    query.addQueryItem(QStringLiteral("link_ts"), ts);
    query.addQueryItem(QStringLiteral("link_sig"),
                       m_profileIdentity.signData(canonical));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
    logSystem("Account: opened the browser to link node \"" + node +
              "\" to the user logged in there.");
    // Watch the account for a couple of minutes so the profile flips to the
    // new owner by itself once the browser side completes. The grant can
    // re-link an already-owned node, so completion = the owner changed from
    // what it was when the browser was opened.
    m_linkGrantBaselineOwner = m_nodeOwnerUser.trimmed();
    m_linkGrantPollsLeft = 24;
    pollLinkNodeGrant();
}

void MainWindow::pollLinkNodeGrant()
{
    if (m_linkGrantPollsLeft <= 0)
        return;
    if (m_nodeOwnerUser.trimmed() != m_linkGrantBaselineOwner ||
        !m_profileIsSelf) {
        // Re-linked (done) or the profile moved off this node: stop watching,
        // and zero the countdown so a later repaint doesn't resurrect the
        // "finishing in your browser" message.
        m_linkGrantPollsLeft = 0;
        return;
    }
    --m_linkGrantPollsLeft;
    refreshProfileAccountStatus();
    QTimer::singleShot(5000, this, &MainWindow::pollLinkNodeGrant);
}

void MainWindow::checkNodeBalance()
{
    const QString addr = m_profileSolanaValue.trimmed();
    if (addr.isEmpty())
        return;
    m_profileBalanceButton->setEnabled(false);
    m_profileBalanceButton->setText("Checking\xE2\x80\xA6");
    m_profileBalance->setText(QString::fromUtf8("\xE2\x80\xA6"));

    if (!isLikelySolanaAddress(addr)) {
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Check balance");
        m_profileBalance->setText("Invalid address");
        return;
    }
    querySolanaBalance(addr, 0);
}

void MainWindow::querySolanaBalance(const QString &addr, int endpointIndex)
{
    const int count = int(sizeof(kSolanaRpcEndpoints) / sizeof(kSolanaRpcEndpoints[0]));
    if (endpointIndex >= count) {
        if (m_profileSolanaValue.trimmed() == addr) {
            m_profileBalanceButton->setEnabled(true);
            m_profileBalanceButton->setText("Refresh balance");
            m_profileBalance->setText("Unavailable");
        }
        return;
    }

    QNetworkRequest request(QUrl(QString::fromLatin1(kSolanaRpcEndpoints[endpointIndex])));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QJsonObject body{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "getBalance"},
        {"params", QJsonArray{addr}},
    };
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, addr, endpointIndex]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (m_profileSolanaValue.trimmed() != addr)
            return;  // panel moved to another node meanwhile
        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject result = root.value("result").toObject();
        if (netError != QNetworkReply::NoError || !result.contains("value")) {
            querySolanaBalance(addr, endpointIndex + 1);
            return;
        }
        const qint64 lamports = result.value("value").toVariant().toLongLong();
        m_profileBalanceButton->setEnabled(true);
        m_profileBalanceButton->setText("Refresh balance");
        m_profileBalance->setText(formatSolanaBalance(lamports));
    });
}

void MainWindow::selectNode(const QString &node)
{
    if (m_selectedNode == node)
        return;
    m_selectedNode = node;
    // Mark the switch in flight before rebuilding the lists so refreshRepositoryList
    // leaves the first-repo open/clear to this function's deferred load below.
    m_nodeSwitching = true;
    // Clear the repo dropdown instantly and spin it: its open repos belong to the
    // previous node, so blank them and show a spinner rather than flashing stale
    // entries while the new node's first repo loads (see updateRepoSwitcher, which
    // renders a "Loading…" state while m_nodeSwitching). The deferred load below
    // calls stopRepoSwitchSpin once the first repo is open.
    m_repoMenuEntries.clear();
    startRepoSwitchSpin();
    updateRepoSwitcher();
    refreshRepositoryList();
    // Opening the first repo runs a cascade of *synchronous* git commands
    // (branches, commits, the file-search index, object size, README, …), each
    // able to block for up to runGitCapture's 8s timeout. Doing that inline —
    // while the node dropdown is still closing — freezes the UI thread long
    // enough for the window manager to flag the app as "Not Responding".
    // Defer it to the next event-loop turn so the menu closes and the node
    // profile paints first, and coalesce rapid switches by re-checking the
    // selection when the deferred load actually fires.
    //
    // Show busy feedback for the (potentially multi-second) load: a spinner on
    // the node button, a wait cursor, and a running log of what it's doing.
    const QString label = node.isEmpty() ? QStringLiteral("nodes") : node;
    logSystem(QStringLiteral("Switching to %1 — loading its repositories…")
                  .arg(label));
    startNodeSwitchSpin();
    // Show the progress pill right away so the switch reads as in-flight even
    // before the deferred load's first step lands.
    showLoadStatus(QStringLiteral("Switching to %1…").arg(label));
    QApplication::setOverrideCursor(Qt::BusyCursor);
    QTimer::singleShot(0, this, [this, node, label] {
        // Always balance this call's setOverrideCursor push, even when a newer
        // switch superseded us — otherwise rapid switching leaks override cursors
        // and the busy cursor gets stuck on. The spinner and m_nodeSwitching are
        // owned by whichever switch is current, so the superseded path leaves
        // those for the newer switch's lambda to clear.
        if (m_selectedNode != node) {
            QApplication::restoreOverrideCursor();
            return;
        }
        m_repoLoadActive = true;
        QElapsedTimer timer;
        timer.start();
        // Show the selected node's first real repository, or blank the panel if
        // it has none, so stale info from the previous node isn't left behind.
        int firstRepo = -1;
        int repoCount = 0;
        for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
            if (entry.index >= 0) {
                ++repoCount;
                if (firstRepo < 0)
                    firstRepo = entry.index;
            }
        if (firstRepo >= 0)
            openRepoDetail(firstRepo);
        else
            clearRepoDetail();
        m_nodeSwitching = false;
        m_repoLoadActive = false;
        finishLoadStepTiming(); // log the final step's duration
        // Stop the repo spinner first so updateRepoSwitcher (called from
        // stopRepoSwitchSpin, now that m_nodeSwitching is false) reveals the
        // freshly-opened first repo and its count.
        stopRepoSwitchSpin();
        stopNodeSwitchSpin();
        QApplication::restoreOverrideCursor();
        // Confirm the result in the top bar (green toast supersedes the blue
        // progress pill) so the switch reads as done, not just silently finished.
        flashMessage(firstRepo >= 0
                         ? QStringLiteral("Switched to %1 (%2 repos) in %3 ms.")
                               .arg(label)
                               .arg(repoCount)
                               .arg(timer.elapsed())
                         : QStringLiteral("Switched to %1 — no repositories (%2 ms).")
                               .arg(label)
                               .arg(timer.elapsed()));
    });
}

void MainWindow::clearRepoDetail()
{
    m_repoDetailIndex = -1;
    m_repoInfo = RepoInfo();
    m_repoBranch.clear();
    if (m_repoHeaderTitle)
        m_repoHeaderTitle->clear();
    // The loaders below all key off repoGitDir(), which is empty with no repo,
    // so they render empty states (no commits, no files, no issues/PRs).
    loadBranchesAndTags();
    updateRepoCodeSize();
    updateRepoDetailStatus();
    updateFooterGitIdentity();
    reloadIssues();
    reloadAgents();
    updateRepoIssueCount();
    m_currentDiscussions.clear();
    reloadDiscussions();
    updateRepoDiscussionCount();
    m_currentPulls.clear();
    refreshPullList();
    updateRepoPullCount();
    loadCommits();
    loadRepoOverview(QString());
    if (m_commitBar)
        m_commitBar->setText(
            "<span style='color:#8b949e'>This node has no repositories.</span>");
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(0); // Code/overview
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    updateBreadcrumb();
}

