#include "MainWindow.h"
#include "MainWindowInternal.h"

using namespace forkmesh::ui;

namespace {

// A crash kills the process before it can log anything about itself, so the
// only record was ever CrashHandler's ~/.forkmesh/diagnostics/crashes.log (plus
// stderr/journalctl) — invisible unless someone went looking there. Surface it
// as a line in the *next* session's own log instead, the same log the user
// actually reads (adhoc #200). Returns a one-line summary of what's new since
// `seenOffset`, or empty if nothing new; *newSize is always set to the file's
// current size so the caller can advance the stored offset unconditionally.
QString describeNewCrashes(const QString &path, qint64 seenOffset, qint64 *newSize)
{
    QFile f(path);
    *newSize = seenOffset;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const qint64 size = f.size();
    *newSize = size;
    qint64 from = seenOffset;
    if (from < 0 || from > size)
        from = 0; // log rotated/cleared since we last checked
    if (from >= size)
        return QString();
    f.seek(from);
    const QString tail = QString::fromUtf8(f.readAll());

    static const QString marker = QStringLiteral("===== ForkMesh crash =====");
    static const QRegularExpression sigRe(QStringLiteral("signal: ([^\\n]+)"));
    static const QRegularExpression whenRe(QStringLiteral("when \\(epoch\\): (\\d+)"));
    int count = 0;
    QString lastSignal, lastWhen;
    for (int pos = tail.indexOf(marker); pos >= 0;
         pos = tail.indexOf(marker, pos + marker.size())) {
        ++count;
        const auto sigMatch = sigRe.match(tail, pos);
        if (sigMatch.hasMatch())
            lastSignal = sigMatch.captured(1);
        const auto whenMatch = whenRe.match(tail, pos);
        if (whenMatch.hasMatch())
            lastWhen = whenMatch.captured(1);
    }
    if (count == 0)
        return QString();

    QString when;
    bool ok = false;
    const qint64 epoch = lastWhen.toLongLong(&ok);
    if (ok)
        when = QDateTime::fromSecsSinceEpoch(epoch)
                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    QString msg = QStringLiteral("Previous session failed to exit cleanly and "
                                 "crashed (%1)")
                      .arg(lastSignal.isEmpty() ? QStringLiteral("unknown signal")
                                                : lastSignal);
    if (!when.isEmpty())
        msg += QStringLiteral(" at %1").arg(when);
    if (count > 1)
        msg += QStringLiteral(" \xE2\x80\x94 %1 crash(es) recorded").arg(count);
    msg += QStringLiteral(". See ~/.forkmesh/diagnostics/crashes.log for the backtrace.");
    return msg;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    logStartup(QStringLiteral("MainWindow ctor begin"));
    // App-wide filter so right-click on ANY selected text (transcript, diff,
    // README, logs — not just the prompt boxes themselves) can offer "Send to
    // Prompt", without wiring a custom context menu into every text widget
    // individually (adhoc #126). See eventFilter's QEvent::ContextMenu branch.
    if (qApp)
        qApp->installEventFilter(this);
    // Ctrl+K opens the global search overlay over the currently-open repo (issue
    // #360). Application-wide so it fires from any tab; the handler no-ops with a
    // notice when no repo detail is open.
    auto *searchShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    searchShortcut->setContext(Qt::ApplicationShortcut);
    connect(searchShortcut, &QShortcut::activated, this,
            &MainWindow::openGlobalSearch);
    setWindowTitle("ForkMesh v" FORKMESH_VERSION);
    setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    if (qApp && qApp->styleSheet().isEmpty())
        applyTheme();
    resize(1060, 700);
    // Restore the last window size/position so it reopens where it was left.
    const QByteArray savedGeometry =
        QSettings().value(kWindowGeometrySetting).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_trayIcon->setToolTip("ForkMesh");
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->show();

    // The self-update relaunch exits via QCoreApplication::quit(), which never
    // reaches closeEvent(); flush chat history (messages + unread state) on
    // every quit path so a restart can't resurrect stale unread badges.
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            &MainWindow::saveChatHistory);

    // BackoffNetworkAccessManager gates every /api/* request through an
    // exponential per-host backoff, so a rate-limited relay (Cloudflare 429s)
    // doesn't get hammered by every independent call site's own retry.
    m_networkAccess = new BackoffNetworkAccessManager(this);
    // Opt-in full request logging (Settings → "Log every network request").
    // When on, every HTTP request that completes through the shared manager is
    // written to the network log with its verb, status and URL so a user can
    // see exactly what background traffic the app is generating (adhoc #74).
    // Off by default, so the common case pays nothing but the settings read.
    connect(m_networkAccess, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) {
                if (!reply ||
                    !QSettings()
                         .value(kVerboseNetworkLogSetting, false)
                         .toBool())
                    return;
                static const char *const verbs[] = {"HEAD",   "GET", "PUT",
                                                     "POST",   "DELETE",
                                                     "CUSTOM"};
                const int op = static_cast<int>(reply->operation());
                const QString verb = (op >= 1 && op <= 6)
                                         ? QLatin1String(verbs[op - 1])
                                         : QStringLiteral("REQ");
                QString status;
                if (reply->error() != QNetworkReply::NoError) {
                    // Lead with the HTTP status code when the server answered
                    // (e.g. "ERR 429 …" for a rate-limit) so the log shows *why*
                    // a request failed, not just that it did. A pure transport
                    // failure (offline, DNS) has no code — fall back to the
                    // Qt error string alone.
                    const QVariant code = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute);
                    status = code.isValid()
                                 ? QStringLiteral("ERR %1 %2")
                                       .arg(code.toString(), reply->errorString())
                                 : QStringLiteral("ERR ") + reply->errorString();
                } else {
                    const QVariant code = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute);
                    status = code.isValid() ? code.toString()
                                            : QStringLiteral("done");
                }
                logSystem(QStringLiteral("net %1 %2 %3")
                              .arg(verb, status, reply->url().toString()));
            });
    m_totalConnectionMs = QSettings().value(kConnectionTotalSetting).toLongLong();
    m_nodeOffline = QSettings().value(kNodeOfflineSetting, false).toBool();
    // Seed the live Claude model cache from disk *before* buildChatPage() builds
    // the composer's model combo, so it lists the real models on the first frame
    // instead of just "Auto" while this session's live /v1/models fetch is still
    // in flight (refreshClaudeModelCombo overwrites this once that lands).
    m_liveClaudeModels =
        QJsonDocument::fromJson(
            QSettings().value(kClaudeModelsCacheSetting).toByteArray())
            .array();

    loadServers();
    loadCachedFavicons();
    // Restore the network log from disk *before* the log section is built so the
    // history (and prior sessions' start/stop markers) renders on the first
    // frame, then record this session's start time.
    loadNetworkLog();
    logSystem(QStringLiteral("Session started - ForkMesh v" FORKMESH_VERSION "."));
    // If the previous run ended in a crash, say so here instead of leaving it
    // silently sitting in crashes.log (adhoc #200).
    {
        QSettings crashSettings;
        const QString crashPath =
            QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics/crashes.log");
        qint64 newSize = 0;
        const QString notice = describeNewCrashes(
            crashPath, crashSettings.value(kCrashLogSeenOffsetSetting, 0).toLongLong(),
            &newSize);
        crashSettings.setValue(kCrashLogSeenOffsetSetting, newSize);
        if (!notice.isEmpty())
            logSystem(notice);
    }
    logStartup(QStringLiteral("servers + favicons loaded"));

    // Load the persisted profile state (custom avatar + node name) *before* the
    // UI is built, so the top-right avatar renders correctly on the very first
    // frame. Otherwise buildChatPage()'s initial updateAvatarButton() runs with
    // these still empty and paints a generic placeholder, which then visibly
    // swaps to the real avatar a few seconds later when silent-auth/startSession
    // finally loads the same values.
    m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();
    // A headless mirror deployed via the installer arrives with the operator's
    // chosen name in FORKMESH_NODE_NAME. That typed name is the source of truth,
    // so adopt it whenever the installer supplied a valid one that differs from
    // whatever is saved here — this includes re-running the installer on a box
    // that already ran under an old name, which must actually rename the node
    // (previously the old saved name silently won, so a host installed as
    // "mirror1" kept showing up as its earlier "vm1"). Persist it before
    // buildSetupPage() seeds m_nameEdit from savedProfileName(), so the deferred
    // auto-connect path picks it up and the node re-joins the network under the
    // chosen name unattended. FORKMESH_NODE_NAME is only present on the
    // installer-launched process (a plain relaunch leaves it unset), so a normal
    // launch — where envName is empty or already matches — is a no-op and never
    // touches an existing name.
    {
        const QString envName =
            accountNameFromInput(qEnvironmentVariable("FORKMESH_NODE_NAME"),
                                 QString());
        if (isValidNodeName(envName) &&
            envName.compare(savedProfileName(), Qt::CaseInsensitive) != 0)
            QSettings().setValue(kAccountNameSetting, envName);
    }
    // True first run: no saved name, no installer-supplied env override. Rather
    // than leave m_nameEdit blank and strand the node on the welcome screen
    // until someone picks a name, hand it a fun generated one now — before
    // buildSetupPage() seeds the field and before the deferred auto-connect
    // below decides whether to enter the app shell — so a first launch can
    // register and start mirroring on its own. The name is still editable from
    // Settings afterwards.
    if (savedProfileName().isEmpty()) {
        m_freshInstall = true;
        QSettings().setValue(kAccountNameSetting, randomFunNodeName());
    }
    if (const QString saved = savedProfileName().toLower(); !saved.isEmpty())
        m_userName = saved;

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSetupPage());
    logStartup(QStringLiteral("setup page built"));
    m_stack->addWidget(buildChatPage());
    logStartup(QStringLiteral("chat/app page built"));
    setCentralWidget(m_stack);
    // Avoid a flash of the login/setup screen on restart: if this machine has
    // already authenticated a node account, open straight onto the app shell.
    // The deferred auto-start (below) connects it; if silent auth ultimately
    // fails it falls back to the setup page.
    if (!QSettings().value(kAuthedAccountSetting).toString().trimmed().isEmpty())
        m_stack->setCurrentIndex(1);
    loadRepositories();
    refreshRepositoryList();
    logStartup(QStringLiteral("repositories loaded"));
    initActions();
    initAgents();
    logStartup(QStringLiteral("actions + agents initialized"));
    const QString lastRepository =
        QSettings().value(kLastRepositorySetting).toString();
    if (!lastRepository.isEmpty()) {
        const int slash = lastRepository.indexOf('/');
        if (slash > 0) {
            const int index =
                repoIndexFor(lastRepository.left(slash),
                             lastRepository.mid(slash + 1));
            if (index >= 0) {
                // Defer the (heavy, git-backed) repo-detail load until the window
                // has painted its first frame (see runDeferredStartup), so the
                // themed UI appears immediately instead of a black, unpainted
                // frame while git work blocks the GUI thread.
                m_pendingRestoreRepoIndex = index;
            }
        }
    }
    logStartup(QStringLiteral("last repository restore scheduled"));
    loadActiveServerIntoEdits();
    updateBreadcrumb();
    logStartup(QStringLiteral("active server + breadcrumb"));
    for (int i = 0; i < m_servers.size(); ++i)
        fetchFavicon(i);
    logStartup(QStringLiteral("favicons fetched"));

    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this] {
        sendTypingState(false);
    });
    m_homeStatsTimer = new QTimer(this);
    // Rebuild the nodes/repos list each minute so the self node's uptime line
    // stays current (this also persists accumulated uptime via updateHomeStats).
    connect(m_homeStatsTimer, &QTimer::timeout, this,
            &MainWindow::refreshRepositoryList);
    m_homeStatsTimer->start(60000);

    // Footer diagnostics + UI-stall watchdog. Deferred one event-loop turn so the
    // heartbeat starts measuring a real, interactive loop (not constructor work).
    QTimer::singleShot(0, this, [this] { startDiagnostics(); });

    // Keep mirrors fresh: periodically fetch each repo so a mirror tracks the
    // owner's repo as it updates. A first pass runs shortly after startup.
    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this, &MainWindow::autoSyncMirrors);
    m_mirrorSyncTimer->start(5 * 60 * 1000);
    QTimer::singleShot(15000, this, &MainWindow::autoSyncMirrors);
    // Source-of-truth nodes poll their inboxes so issues/PRs/comments filed by
    // other nodes show up automatically (with a notification), without a manual
    // "Sync inbox". First pass shortly after launch, then on a short interval.
    m_inboxPollTimer = new QTimer(this);
    connect(m_inboxPollTimer, &QTimer::timeout, this, &MainWindow::pollOwnedInboxes);
    m_inboxPollTimer->start(60 * 1000);
    QTimer::singleShot(20000, this, &MainWindow::pollOwnedInboxes);
    // Settings → "Automatically update ForkMesh" (off by default on desktop, on
    // by default headless — see kAutoUpdateSetting): hourly check for a new
    // tagged release, plus one shortly after launch so a stale headless install
    // catches up quickly. maybeAutoUpdate() is a no-op whenever the setting is
    // off.
    m_autoUpdateTimer = new QTimer(this);
    connect(m_autoUpdateTimer, &QTimer::timeout, this, &MainWindow::maybeAutoUpdate);
    m_autoUpdateTimer->start(60 * 60 * 1000);
    QTimer::singleShot(5 * 60 * 1000, this, &MainWindow::maybeAutoUpdate);
    // Chat messages are retained for 7 days (kChatMessageRetentionMs); sweep
    // local history hourly so a node left running that long doesn't keep
    // showing/serving messages the relay has already dropped (adhoc #49).
    m_chatExpiryTimer = new QTimer(this);
    connect(m_chatExpiryTimer, &QTimer::timeout, this,
            &MainWindow::pruneExpiredChatHistory);
    m_chatExpiryTimer->start(60 * 60 * 1000);
    // Radar: probe the active relay's round-trip latency once a minute (issue
    // #144), plus a first reading shortly after launch so the dish isn't stuck
    // on "…" while the UI settles.
    m_relayLatencyTimer = new QTimer(this);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::probeRelayLatency);
    // Piggy-back the profile panel's "online Xh" uptime + reward status refresh on
    // the same once-a-minute tick (minute granularity is plenty for an hours-online
    // readout).
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::updateNodeOnlineControls);
    m_relayLatencyTimer->start(60 * 1000);
    QTimer::singleShot(2500, this, &MainWindow::probeRelayLatency);
    // React to the OS's own connectivity signal so the radar flips to
    // offline/online the moment the link changes, instead of lagging the
    // minute cadence (adhoc #41).
    initRelayReachabilityWatch();
    // Bootstrap the flagship ForkMesh mirror shortly after launch so a freshly
    // installed client shows the project repo without manual setup.
    QTimer::singleShot(3000, this, &MainWindow::ensureFlagshipRepo);

    logStartup(QStringLiteral("timers started"));
    if (!m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
    } else if (m_pubkeyLabel) {
        m_pubkeyLabel->setText("Ed25519 public key: " +
                               m_profileIdentity.shortPublicKey());
        m_pubkeyLabel->setToolTip(m_profileIdentity.publicKey());
        // A first run already got a generated fun name saved above, so
        // m_nameEdit is never blank here — the deferred auto-connect below
        // treats every launch the same way instead of stopping first runs on
        // the welcome screen. Auto-connect, but only after the first frame is painted (see
        // runDeferredStartup) — authenticateSilently()/startSession() block the
        // GUI thread, so running them before the window is exposed shows a black
        // frame on launch.
        m_pendingSilentAuth = true;
    }
    logStartup(QStringLiteral("identity loaded"));
    updateHomeStats();
    logStartup(QStringLiteral("home stats updated (ctor end)"));
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_deferredStartupStarted)
        return;
    m_deferredStartupStarted = true;
    // Run the heavy, git-backed startup (silent auth + last-repo restore) only
    // once the window's first frame is actually on screen. A singleShot(0) would
    // fire before the compositor exposes/paints the window, blocking the GUI
    // thread on git work and leaving an unpainted black frame for ~a second.
    if (QWindow *handle = windowHandle()) {
        if (handle->isExposed())
            QTimer::singleShot(0, this, &MainWindow::runDeferredStartup);
        else
            handle->installEventFilter(this); // wait for the first expose
    }
    // Safety net so startup still runs if no expose ever arrives (e.g. headless
    // / offscreen platforms). runDeferredStartup is idempotent.
    QTimer::singleShot(250, this, &MainWindow::runDeferredStartup);
}

void MainWindow::runDeferredStartup()
{
    if (m_deferredStartupRun)
        return;
    m_deferredStartupRun = true;
    if (QWindow *handle = windowHandle())
        handle->removeEventFilter(this);

    // Restore the last open repository first (matches the old scheduling order).
    if (m_pendingRestoreRepoIndex >= 0 &&
        m_pendingRestoreRepoIndex < m_repositories.size()) {
        const int index = m_pendingRestoreRepoIndex;
        logStartup(QStringLiteral("restoring last repository (deferred)"));
        m_selectedNode = m_repositories.at(index).owner;
        refreshRepositoryList();
        openRepoDetail(index);
        logStartup(QStringLiteral("last repository detail loaded"));
        // Issues are loaded synchronously by openRepoDetail, so the looper has a
        // populated backlog to resume against (adhoc #125).
        maybeRestoreIssueLooper();
    } else if (m_repoDetailIndex < 0) {
        // No saved repository to restore: land on the selected node's first repo
        // (if any) so a fresh session opens on real content, not an empty panel.
        int firstRepo = -1;
        for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
            if (entry.index >= 0) {
                firstRepo = entry.index;
                break;
            }
        if (firstRepo >= 0)
            openRepoDetail(firstRepo);
    }
    m_pendingRestoreRepoIndex = -1;

    // Resume the agent sessions initAgents() re-queued after the restart, only
    // now that the first frame is up and the last repository is restored.
    // Draining inside the constructor started each resumed Claude transcript
    // with its assign-time jump to the Agents tab — a full cold openRepoDetail()
    // (~2s of git reads) before the window could paint, which the restore above
    // then redid. Quiet mode keeps the resumed runs from stealing the view.
    if (!m_agentQueue.isEmpty()) {
        m_agentQuietResume = true;
        processAgentQueue();
        m_agentQuietResume = false;
        logStartup(QStringLiteral("agent sessions resumed (deferred)"));
    }

    // Then auto-enter the app whenever this machine has a node name — which now
    // includes a first run, since one was generated for it above if needed.
    // No account is required: the node drops straight into the app shell.
    // Silent auth is best-effort — it restores an existing active account's
    // hosting/payout state when this key owns one, but its absence no longer
    // keeps the node on the welcome screen. The empty-name branch below is now
    // just a safety net (e.g. name-generation somehow failed).
    if (m_pendingSilentAuth) {
        m_pendingSilentAuth = false;
        const QString name = m_nameEdit ? m_nameEdit->text().trimmed().toLower()
                                        : QString();
        if (!name.isEmpty() && isValidNodeName(name)) {
            authenticateSilently(name);
            if (m_stack)
                m_stack->setCurrentIndex(1); // app shell
            startSession();
        } else if (m_stack) {
            m_stack->setCurrentIndex(0); // first run / no saved name: show setup
        }
    }

    // Opt-in only (off by default): send the previous session's crash + stall
    // records to the mainnode's triage queue. No-op unless the user enabled it in
    // Settings; fire-and-forget so it never delays the first interactive frame.
    maybeUploadDiagnostics();
}

void MainWindow::applyTheme()
{
    // Honour the user's override; otherwise follow the OS color scheme.
    qApp->setStyleSheet(Theme::styleSheetForDark(currentThemeIsDark()));
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            window->refreshThemedIcons();
            // The always-on footer log line is inline-styled too — repaint it for
            // the new theme so its text/border/canvas track the switch.
            window->styleFooterUpdateLog();
        }
    }
}

void MainWindow::refreshThemedIcons()
{
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        applyStoredOcticon(button);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(kWindowGeometrySetting, saveGeometry());
    saveChatHistory();
    // Record this session's stop time, then flush+trim the persisted log.
    logSystem(QStringLiteral("Session ended."));
    saveNetworkLog();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // Coming back to the window (e.g. after a background agent edited the working
    // tree in a terminal): re-scan the changes panel and refresh the commit
    // markers so they're current the moment the app regains focus. Only when the
    // Commits tab is actually on screen — isVisible() is false for a stacked page
    // that isn't current, so this is a no-op everywhere else.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
        m_scmTree && m_scmTree->isVisible()) {
        refreshSourceControl();
        refreshCommitMarkersIfStale();
    }
}
