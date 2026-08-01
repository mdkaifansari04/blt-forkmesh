#include "ForkMeshVersion.h"
#include "AgentPromptImages.h"
#include "MainWindow.h"
#include "CrashHandler.h"
#include "MainWindowInternal.h"
#include "WorldSpeechBridge.h"

using namespace forkmesh::ui;

namespace {

// ForkMesh is event-driven: every HTTP request fires in response to some
// action/event (a relay sync frame, a catalog publish, an agent poll…). The
// finished() choke point only sees the reply, so we recover *what drove it*
// from the endpoint it hit — the most reliable signal available there — and
// tag the verbose network-log line with it so the traffic reads as events
// rather than opaque URLs (adhoc #19). Order most-specific first.
QString networkRequestEventFor(const QUrl &url)
{
    const QString path = url.path();
    struct Route {
        const char *needle;
        const char *event;
    };
    static const Route routes[] = {
        {"/api/sync", "relay sync"},
        {"/api/repositories", "catalog publish"},
        {"/agents", "agent poll"},
        {"/issues", "issue fetch"},
        {"/pulls", "pull fetch"},
        {"/releases", "release fetch"},
        {"/mirrors", "mirror sync"},
        {"/api/repo/", "repo fetch"},
        {"/api/version", "version check"},
        {"/api/network", "network stats"},
        {"/api/security", "security report"},
        {"/api/forkbot", "forkbot chat"},
        {"/api/chat", "chat"},
        {"/api/accounts", "account"},
        {"/api/oauth", "account"},
    };
    for (const Route &r : routes) {
        if (path.contains(QLatin1String(r.needle)))
            return QString::fromLatin1(r.event);
    }
    return QStringLiteral("request");
}

} // namespace

MainWindow::~MainWindow()
{
    // Detach the background-activity strip first: git reads and network replies
    // keep announcing themselves during teardown below, and the listener holds
    // a raw `this`.
    forkmesh::BackgroundActivity::setListener(nullptr);

    // Revoke the browser's memory-only voice capability and stop any local
    // capture while MainWindow's voice state is still alive.
    if (m_worldSpeechBridge)
        m_worldSpeechBridge->stop();

    // Deployment children may still hold a session API token or Tunnel
    // connector token in their private process environment. Stop all of them
    // before QObject teardown, drop the retained QProcess environments, and
    // overwrite our short-lived redaction copy. No token is persisted in
    // settings or argv.
    for (QProcess *process :
         {m_cloudflareBootstrapProcess,
          m_cloudflareTunnelBootstrapProcess,
          m_cloudflaredInstallProcess, m_cloudflaredProcess,
          m_mirrorGatewayProcess}) {
        if (!process)
            continue;
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(2000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
        process->setProcessEnvironment(QProcessEnvironment());
    }
    m_cloudflareActiveSecret.fill(QChar(u'\0'));
    m_cloudflareActiveSecret.clear();
    for (QString &password : m_hostSessionPasswords)
        password.fill(QChar(u'\0'));
    m_hostSessionPasswords.clear();

    forkmesh::setCrashContext(
        QStringLiteral("MainWindow teardown\nregistered diff views: %1")
            .arg(m_diffViews.size()));

    // Diff viewers are QObject children. During QMainWindow/QObject teardown they
    // emit destroyed() after MainWindow's QList members are already being
    // destroyed, so the registerDiffView() destroyed-lambda must not fire then.
    const QList<QTextEdit *> views = m_diffViews;
    for (QTextEdit *view : views) {
        if (!view)
            continue;
        if (QWidget *vp = view->viewport())
            vp->removeEventFilter(this);
        QObject::disconnect(view, nullptr, this, nullptr);
    }
    m_diffViews.clear();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    logStartup(QStringLiteral("MainWindow ctor begin"));
    const auto startupStep = [](const QString &name, auto &&work) {
        forkmesh::StartupTraceStep step(
            QStringLiteral("MainWindow: %1").arg(name));
        std::forward<decltype(work)>(work)();
    };
#ifndef FORKMESH_WINDOW_TESTS
    // Start the durable action journal before initialization launches network,
    // git, agent, or worker activity. The writer owns all later disk I/O.
    startupStep(QStringLiteral("start durable action telemetry"), [] {
        forkmesh::ActionTelemetry::initialize(
            QDir::homePath() +
            QStringLiteral("/.forkmesh/diagnostics/actions.jsonl"));
    });
#endif
    QElapsedTimer chromeTimer;
    chromeTimer.start();
    logStartup(QStringLiteral("BEGIN MainWindow: configure window chrome, "
                              "shortcuts, geometry and tray icon"));
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
    // The app owns its top chrome so navigation/search can live on the same row
    // as the window controls instead of under the native title bar.
    setWindowFlag(Qt::FramelessWindowHint, true);
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
    logStartup(
        QStringLiteral("DONE  MainWindow: configure window chrome, shortcuts, "
                       "geometry and tray icon (%1ms)")
            .arg(chromeTimer.elapsed()));

    // The self-update relaunch exits via QCoreApplication::quit(), which never
    // reaches closeEvent(); flush chat history (messages + unread state) on
    // every quit path so a restart can't resurrect stale unread badges.
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            &MainWindow::saveChatHistory);

    // BackoffNetworkAccessManager gates every /api/* request through an
    // exponential per-host backoff, so a rate-limited relay (Cloudflare 429s)
    // doesn't get hammered by every independent call site's own retry. It also
    // hosts the app-level whitelist firewall: default-on, with user-approved
    // rules persisted in QSettings.
    QElapsedTimer networkTimer;
    networkTimer.start();
    logStartup(QStringLiteral(
        "BEGIN MainWindow: configure shared network manager and firewall"));
    auto *network = new BackoffNetworkAccessManager(this);
    network->setFirewallEnabled(
        QSettings().value(kRequestFirewallEnabledSetting, true).toBool());
    network->setFirewallRules(requestFirewallWhitelistWithDefaults());
    network->setFirewallPrompt(
        [this](const QString &method, const QUrl &url, QString *allowRuleOut) {
            return promptFirewallRequest(method, url, allowRuleOut);
        });
    connect(network, &BackoffNetworkAccessManager::firewallRequestDecided, this,
            [this, network](const QString &method, const QUrl &url,
                            const QString &rule, bool allowed) {
                if (allowed)
                    QSettings().setValue(kRequestFirewallWhitelistSetting,
                                         network->firewallRules());
                recordFirewallRequest(method, url, rule, allowed);
            });
    connect(network, &BackoffNetworkAccessManager::endpointStatsChanged, this, [this] {
        if (m_sectionStack &&
            m_sectionStack->currentIndex() == kNetworkDiagnosticsSectionIndex)
            refreshNetworkDiagnostics();
    });
    m_networkAccess = network;
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
                    // Qt's errorString() for an HTTP error is generic ("server
                    // replied: <url>") and omits the payload the server actually
                    // sent — which for a worker 503 is exactly the explanation a
                    // user needs. peek() (not read()) the first chunk of the body
                    // so we surface the server's own words without consuming the
                    // buffer out from under the real reply consumer (adhoc #68).
                    const QByteArray body = reply->peek(512);
                    if (!body.isEmpty()) {
                        const QString snippet = QString::fromUtf8(body).simplified();
                        if (!snippet.isEmpty())
                            status += QStringLiteral(" [body: ") + snippet +
                                      QStringLiteral("]");
                    }
                } else {
                    const QVariant code = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute);
                    status = code.isValid() ? code.toString()
                                            : QStringLiteral("done");
                    // Surface the response payload on success too (not just
                    // the status code) so the verbose log answers "what did
                    // this request actually return?" without needing
                    // devtools. peek() (not read()) so the real reply
                    // consumer still gets the full body.
                    const QByteArray body = reply->peek(512);
                    if (!body.isEmpty()) {
                        const QString snippet = QString::fromUtf8(body).simplified();
                        if (!snippet.isEmpty())
                            status += QStringLiteral(" [body: ") + snippet +
                                      QStringLiteral("]");
                    }
                }
                logSystem(QStringLiteral("net %1 %2 %3 \xC2\xB7 %4")
                              .arg(verb, status, reply->url().toString(),
                                   networkRequestEventFor(reply->url())));
            });
    logStartup(
        QStringLiteral("DONE  MainWindow: configure shared network manager and "
                       "firewall (%1ms)")
            .arg(networkTimer.elapsed()));
    startupStep(QStringLiteral("read connection state and cached model list"),
                [this] {
                    m_totalConnectionMs =
                        QSettings().value(kConnectionTotalSetting).toLongLong();
                    m_nodeOffline =
                        QSettings().value(kNodeOfflineSetting, false).toBool();
    // Seed the live Claude model cache from disk *before* buildChatPage() builds
    // the composer's model combo, so it lists the real models on the first frame
    // instead of just "Auto" while this session's live /v1/models fetch is still
    // in flight (refreshClaudeModelCombo overwrites this once that lands).
                    m_liveClaudeModels =
                        QJsonDocument::fromJson(
                            QSettings()
                                .value(kClaudeModelsCacheSetting)
                                .toByteArray())
                            .array();
                });

    startupStep(QStringLiteral("load persisted relay servers"),
                [this] { loadServers(); });
    startupStep(QStringLiteral("load cached site favicons"),
                [this] { loadCachedFavicons(); });
    // Restore the network log from disk *before* the log section is built so the
    // history (and prior sessions' start/stop markers) renders on the first
    // frame, then record this session's start time. Crash records from a prior
    // unclean exit are already in the loaded log (the crash handler writes to
    // network_log.txt directly), so no separate crash-file scan is needed.
    startupStep(QStringLiteral("restore persisted network log"),
                [this] { loadNetworkLog(); });
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logSystem(QStringLiteral("Session started - ForkMesh v" FORKMESH_VERSION "."));
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logStartup(QStringLiteral("startup session marker written to network log"));

    // Load the persisted profile state (custom avatar + node name) *before* the
    // UI is built, so the top-right avatar renders correctly on the very first
    // frame. Otherwise buildChatPage()'s initial updateAvatarButton() runs with
    // these still empty and paints a generic placeholder, which then visibly
    // swaps to the real avatar a few seconds later when silent-auth/startSession
    // finally loads the same values.
    startupStep(QStringLiteral("restore profile name and avatar"), [this] {
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
            const QString generated = randomFunNodeName();
            QSettings().setValue(kAccountNameSetting, generated);
            // Record that this name was handed out, not chosen. While it is still
            // the account name (and no user account is linked), chat speaks as a
            // guest — the generated name stays the machine's node identity, but it
            // is not the person's username (see chatIdentityIsGuest()).
            QSettings().setValue(kGeneratedNodeNameSetting, generated);
        }
        if (const QString saved = savedProfileName().toLower(); !saved.isEmpty())
            m_userName = saved;
    });

    startupStep(QStringLiteral("create root page stack"), [this] {
        m_stack = new QStackedWidget(this);
    });
    // The setup page is no longer a screen anyone sees (adhoc #115): it only ever
    // asked for a username and a relay host that both already have working
    // defaults, so it loaded straight into the app anyway. It stays in the stack
    // purely as the data holder for m_nameEdit / m_serverUrlEdit / m_setupError,
    // which the session, settings and update paths all still read and write.
    startupStep(QStringLiteral("build hidden setup data page"), [this] {
        m_stack->addWidget(buildSetupPage());
    });
    startupStep(QStringLiteral("build chat and application shell"), [this] {
        m_stack->addWidget(buildChatPage());
    });
    startupStep(QStringLiteral("install application shell as central widget"),
                [this] {
                    m_stack->setCurrentIndex(1); // app shell, always
                    setCentralWidget(m_stack);
                });
    // Build the two largest, most frequently visited repository surfaces before
    // the window becomes interactive. QWidget construction cannot legally run
    // on a worker thread; paying this one-time cost here keeps the first repo
    // click and the first Code/Issues switch below a frame-scale budget instead
    // of freezing an already-visible window for ~230ms each. Less common repo
    // tabs remain lazy.
    startupStep(QStringLiteral("build repository detail container"),
                [this] { ensureRepoDetailSectionBuilt(); });
    startupStep(QStringLiteral("warm Code, Branches and Worktrees UI"),
                [this] { ensureRepoDetailTabBuilt(0); });
    startupStep(QStringLiteral("warm Issues UI"),
                [this] { ensureRepoDetailTabBuilt(2); });
    startupStep(QStringLiteral("initialize local speech bridge"),
                [this] { initializeWorldSpeechBridge(); });
    startupStep(QStringLiteral("load repository catalog from settings"),
                [this] { loadRepositories(); });
    startupStep(QStringLiteral("render repository catalog and rail counts"),
                [this] { refreshRepositoryList(); });
    startupStep(QStringLiteral("initialize Actions state"),
                [this] { initActions(); });
    startupStep(QStringLiteral("load and queue saved agent sessions"),
                [this] { initAgents(); });
    startupStep(QStringLiteral("resolve last repository for deferred restore"),
                [this] {
                    const QString lastRepository =
                        QSettings().value(kLastRepositorySetting).toString();
                    if (lastRepository.isEmpty())
                        return;
                    const int slash = lastRepository.indexOf('/');
                    if (slash <= 0)
                        return;
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
                });
    startupStep(QStringLiteral("load active relay into controls"),
                [this] { loadActiveServerIntoEdits(); });
    startupStep(QStringLiteral("build top breadcrumb and usage controls"),
                [this] { updateBreadcrumb(); });
    startupStep(QStringLiteral("schedule relay favicon refreshes"), [this] {
        for (int i = 0; i < m_servers.size(); ++i)
            fetchFavicon(i);
    });

    QElapsedTimer timerSetupTimer;
    timerSetupTimer.start();
    logStartup(QStringLiteral(
        "BEGIN MainWindow: configure periodic and delayed startup jobs"));
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
    logStartup(QStringLiteral("  timer armed: home/repository stats every 60s"));

    // The activity rail's Git badge has to be right whichever repo tab is on
    // screen — the working tree moves under us constantly here (an agent
    // session, an external editor, a sync), and the changes panel is usually
    // not the visible view when it does. Poll it while the rail is showing;
    // refreshRepoChangeBadge() is one detached `git status`, so this costs the
    // GUI thread nothing, and the guard makes it a no-op everywhere but a repo
    // page (isVisible() is false for a stacked page that isn't current).
    m_repoChangeBadgeTimer = new QTimer(this);
    connect(m_repoChangeBadgeTimer, &QTimer::timeout, this, [this] {
        if (m_railGitButton && m_railGitButton->isVisible())
            refreshRepoChangeBadge();
    });
    m_repoChangeBadgeTimer->start(10000);
    logStartup(QStringLiteral("  timer armed: Git rail status every 10s"));

    // Footer diagnostics + UI-stall watchdog. Deferred one event-loop turn so the
    // heartbeat starts measuring a real, interactive loop (not constructor work).
    QTimer::singleShot(0, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: diagnostics and UI-stall watchdog"));
        startDiagnostics();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: diagnostics on first event-loop turn"));

    // Keep mirrors fresh: periodically fetch each repo so a mirror tracks the
    // owner's repo as it updates. Push events are the primary signal now —
    // since bf6323d0 mirror peers are notified the instant a push lands on the
    // source's bare mirror — so this timer is only a safety net for dropped
    // events. One minute (kMirrorSyncIntervalMs) with ±15% jitter keeps the
    // dropped-event recovery window short without making a fleet fetch in
    // lockstep. The existing per-repository in-flight guard prevents a timer
    // tick from duplicating an immediate push/roster-driven sync, and the job
    // remains gated on relay health (autoSyncMirrorsIfRelayHealthy) because
    // the git subprocesses never pass through BackoffNetworkAccessManager's
    // 429 cooldown. A first pass runs shortly after startup to catch up on
    // pushes missed offline.
    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this,
            &MainWindow::autoSyncMirrorsIfRelayHealthy);
    const int mirrorJitterSpanMs =
        int(kMirrorSyncIntervalMs * kMirrorSyncJitterPercent / 100);
    m_mirrorSyncTimer->start(int(kMirrorSyncIntervalMs) +
                             QRandomGenerator::global()->bounded(
                                 -mirrorJitterSpanMs, mirrorJitterSpanMs + 1));
    logStartup(QStringLiteral("  timer armed: mirror safety sync every %1ms")
                   .arg(m_mirrorSyncTimer->interval()));
    QTimer::singleShot(15000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: initial mirror synchronization"));
        autoSyncMirrors();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: initial mirror synchronization in 15000ms"));
    // Source-of-truth nodes pick up issues/PRs/comments/agent-prompts filed by
    // other nodes through the relay's event push: a minimal frame on the
    // per-owner node event socket (NodeEventSocket -> ForkMeshNodes DO)
    // triggers one consolidated GET /api/sync (see performRelaySync). This
    // timer is only the slow safety net for dropped events and reconnect gaps
    // — it used to be a 60s poll of four endpoints per owned repo. It relaxes
    // to 15 minutes while the event socket is connected (startNodeEventSocket)
    // and returns to 5 minutes when the push channel drops. First pass shortly
    // after launch covers anything queued while the app was closed.
    m_inboxPollTimer = new QTimer(this);
    connect(m_inboxPollTimer, &QTimer::timeout, this, &MainWindow::performRelaySync);
    m_inboxPollTimer->start(5 * 60 * 1000);
    logStartup(QStringLiteral("  timer armed: relay inbox safety sync every 300000ms"));
    QTimer::singleShot(20000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: initial relay inbox sync"));
        performRelaySync();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: initial relay inbox sync in 20000ms"));
    // Settings → "Automatically update ForkMesh" (off by default on desktop, on
    // by default headless — see kAutoUpdateSetting): hourly check for a new
    // tagged release, plus one shortly after launch so a stale headless install
    // catches up quickly. maybeAutoUpdate() is a no-op whenever the setting is
    // off.
    m_autoUpdateTimer = new QTimer(this);
    connect(m_autoUpdateTimer, &QTimer::timeout, this, &MainWindow::maybeAutoUpdate);
    // Both checks are jittered so a fleet doesn't discover a fresh tag in
    // lockstep and pile onto the relay (and the one live source mirror) at
    // the same moment — publishing v0.6.2 turned every node's updater loose
    // within the same hour.
    m_autoUpdateTimer->start(
        60 * 60 * 1000 +
        int(QRandomGenerator::global()->bounded(-10 * 60 * 1000,
                                                10 * 60 * 1000 + 1)));
    const int initialUpdateDelayMs =
        5 * 60 * 1000 +
        int(QRandomGenerator::global()->bounded(10 * 60 * 1000));
    QTimer::singleShot(initialUpdateDelayMs, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: automatic update check"));
        maybeAutoUpdate();
    });
    logStartup(QStringLiteral(
                   "  timer armed: automatic update every %1ms; first check in %2ms")
                   .arg(m_autoUpdateTimer->interval())
                   .arg(initialUpdateDelayMs));
    // Chat messages are retained for 7 days (kChatMessageRetentionMs); sweep
    // local history hourly so a node left running that long doesn't keep
    // showing/serving messages the relay has already dropped (adhoc #49).
    m_chatExpiryTimer = new QTimer(this);
    connect(m_chatExpiryTimer, &QTimer::timeout, this,
            &MainWindow::pruneExpiredChatHistory);
    m_chatExpiryTimer->start(60 * 60 * 1000);
    logStartup(QStringLiteral("  timer armed: expired chat pruning every 3600000ms"));
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
    logStartup(QStringLiteral("  timer armed: relay latency and uptime every 60000ms"));
    QTimer::singleShot(2500, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: first relay latency probe"));
        probeRelayLatency();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: first relay latency probe in 2500ms"));
    // React to the OS's own connectivity signal so the radar flips to
    // offline/online the moment the link changes, instead of lagging the
    // minute cadence (adhoc #41).
    startupStep(QStringLiteral("start operating-system reachability watch"),
                [this] { initRelayReachabilityWatch(); });
    // Tasks rail badge: the count is only produced by the Tasks page, which is
    // built lazily, so before this a restart left the rail blank until someone
    // opened it (adhoc #79). Paint the persisted count right away and re-read
    // the board once the account session has had time to come up.
    startupStep(QStringLiteral("restore cached organization task badge"),
                [this] { restoreOrganizationTaskBadge(); });
    QTimer::singleShot(25000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: refresh organization task badge"));
        refreshOrganizationTaskBadge();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: organization task refresh in 25000ms"));
    // Bootstrap the flagship ForkMesh mirror shortly after launch so a freshly
    // installed client shows the project repo without manual setup.
    QTimer::singleShot(3000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: ensure flagship repository"));
        ensureFlagshipRepo();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: flagship repository check in 3000ms"));

    logStartup(
        QStringLiteral("DONE  MainWindow: configure periodic and delayed startup "
                       "jobs (%1ms)")
            .arg(timerSetupTimer.elapsed()));
    startupStep(QStringLiteral("load or create Ed25519 node identity"), [this] {
        if (!m_profileIdentity.load()) {
            m_setupError->setText(m_profileIdentity.errorString());
            m_setupError->show();
        } else {
        // The stack already opens on the app shell, so there is no login/setup
        // screen left to flash past here — the persisted-capability check that
        // used to decide it is gone with the screen (adhoc #115).
            if (m_pubkeyLabel) {
                m_pubkeyLabel->setText("Ed25519 public key: " +
                                       m_profileIdentity.shortPublicKey());
                m_pubkeyLabel->setToolTip(m_profileIdentity.publicKey());
            }
        // A first run already got a generated fun name saved above, so
        // m_nameEdit is never blank here — the deferred auto-connect below
        // treats every launch the same way instead of stopping first runs on
        // the welcome screen. Auto-connect, but only after the first frame is painted (see
        // runDeferredStartup) — authenticateSilently()/startSession() block the
        // GUI thread, so running them before the window is exposed shows a black
        // frame on launch.
            m_pendingSilentAuth = true;
        }
    });
    // A provisioned headless mirror never builds or opens the Control Node
    // page, which used to be the only path that armed its gateway/tunnel
    // services. Defer until construction and setHeadlessMode() have completed;
    // the helper still requires the persisted hostname plus an owner-only
    // connector token, so ordinary clients with no endpoint are a no-op.
    QTimer::singleShot(0, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: inspect direct mirror services"));
        maybeAutoStartDirectMirrorServices();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: direct mirror service inspection on first "
        "event-loop turn"));
    startupStep(QStringLiteral("calculate initial home and uptime stats"),
                [this] { updateHomeStats(); });
    // Populate the Hosts/Relays nav button counts up front — Nodes' count
    // follows the roster and updates itself via updateNodeSwitcher().
    startupStep(QStringLiteral("populate Hosts navigation count"),
                [this] { refreshHostsTable(); });
    startupStep(QStringLiteral("populate Relays navigation count"),
                [this] { refreshRelaysTable(); });
    logStartup(QStringLiteral("MainWindow ctor complete"));
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_deferredStartupStarted)
        return;
    m_deferredStartupStarted = true;
    logStartup(QStringLiteral(
        "first window show observed; deferred authentication and repository "
        "restore intentionally scheduled in 5000ms to protect first paint"));
    // An Expose event is not proof that Qt has painted the first frame (notably
    // with the offscreen QPA and some compositors). Starting silent auth from
    // that event let key/account work run ahead of paint and restored the black
    // startup pause this deferral was meant to prevent. Give the event loop a
    // bounded five-second interactive runway so silent authentication and session
    // restoration cannot collide with the user's first repository/tab navigation,
    // then run the idempotent startup regardless of platform. Headless launches
    // use the same deterministic fallback.
    QTimer::singleShot(5000, this, &MainWindow::runDeferredStartup);
}

void MainWindow::runDeferredStartup()
{
    if (m_deferredStartupRun) {
        logStartup(QStringLiteral(
            "deferred startup requested again; ignored because it already ran"));
        return;
    }
    m_deferredStartupRun = true;
    forkmesh::StartupTraceStep deferredStep(
        QStringLiteral("deferred startup: restore repository, session and agents"));
    if (QWindow *handle = windowHandle())
        handle->removeEventFilter(this);

    bool headlessBootstrapQueued = false;
    auto runHeadlessBootstrap = [this, &headlessBootstrapQueued] {
        if (!m_headless)
            return;
        headlessBootstrapQueued = true;
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: headless flagship bootstrap"));
        logSystem(QStringLiteral(
            "Startup: checking forkmesh/forkmesh mirror bootstrap."));
        ensureFlagshipRepo();
        QTimer::singleShot(10000, this, [this] {
            forkmesh::StartupTraceStep step(QStringLiteral(
                "delayed startup: recheck headless flagship bootstrap"));
            logSystem(QStringLiteral(
                "Startup: rechecking forkmesh/forkmesh mirror bootstrap."));
            ensureFlagshipRepo();
        });
    };

    // Auto-enter the app whenever this machine has a node name — which now
    // includes a first run, since one was generated for it above if needed.
    // No account is required: the node drops straight into the app shell.
    // Silent auth is best-effort — it restores an existing active account's
    // hosting/payout state when this key owns one, but its absence no longer
    // keeps the node on the welcome screen. A missing/invalid name (e.g. name
    // generation somehow failed) no longer falls back to the retired setup
    // screen either; the app stays put and the top-bar sign-in pill is the way
    // in (adhoc #115).
    auto startNetworking = [&] {
        forkmesh::StartupTraceStep networkingStep(
            QStringLiteral("deferred startup: authenticate and start networking"));
        if (m_pendingSilentAuth) {
            m_pendingSilentAuth = false;
            const QString name = m_nameEdit
                                     ? m_nameEdit->text().trimmed().toLower()
                                     : QString();
            if (!name.isEmpty() && isValidNodeName(name)) {
                {
                    forkmesh::StartupTraceStep step(
                        QStringLiteral("deferred startup: silent authentication"));
                    authenticateSilently(name);
                }
                {
                    forkmesh::StartupTraceStep step(
                        QStringLiteral("deferred startup: start mesh session"));
                    startSession();
                }
                runHeadlessBootstrap();
            } else {
                logStartup(QStringLiteral(
                    "  silent authentication skipped: no valid saved node name"));
            }
        } else {
            logStartup(QStringLiteral(
                "  silent authentication skipped: no pending authentication"));
        }

        // Headless/offscreen launches should never depend on the setup-page
        // widgets or a GUI label existing. If the normal pending-silent-auth
        // path did not run for any reason, use the persisted node name directly
        // and still start the backend + flagship mirror bootstrap.
        if (m_headless && !headlessBootstrapQueued) {
            const QString name =
                accountNameFromInput(savedProfileName(), QString());
            if (!name.isEmpty() && isValidNodeName(name)) {
                if (!m_backend) {
                    if (m_nameEdit)
                        m_nameEdit->setText(name);
                    {
                        forkmesh::StartupTraceStep step(QStringLiteral(
                            "deferred startup: headless silent authentication"));
                        authenticateSilently(name);
                    }
                    {
                        forkmesh::StartupTraceStep step(QStringLiteral(
                            "deferred startup: start headless mesh session"));
                        startSession();
                    }
                }
                runHeadlessBootstrap();
            }
        }

        // Silent auth has now had its say, so the top-bar pill can offer "Log in
        // / Sign up" (or stay hidden) knowing whether a user account is attached.
        // This is intentionally separate from m_deferredStartupRun: restoring a
        // view can update the chrome before this lookup happens.
        m_startupAuthResolved = true;
        updateSignInButton();
    };

    // An unattended node must publish/host no matter what the restore below
    // does: openRepoDetail() pumps the event loop while it waits on git and can
    // park there indefinitely (a wedged fetch, an agent modal), and mirror6 sat
    // exactly like that for a day — content fully repaired yet its catalog
    // lease dead because startSession() was sequenced after the restore. A
    // desktop keeps the restore-first order so silent auth never competes with
    // the user's first navigation.
    if (m_headless)
        startNetworking();

    // Restore the last open repository (for a desktop this comes first,
    // matching the old scheduling order).
    if (m_pendingRestoreRepoIndex >= 0 &&
        m_pendingRestoreRepoIndex < m_repositories.size()) {
        const int index = m_pendingRestoreRepoIndex;
        logStartup(QStringLiteral("restoring saved repository index %1")
                       .arg(index));
        m_selectedNode = m_repositories.at(index).owner;
        {
            forkmesh::StartupTraceStep step(QStringLiteral(
                "deferred startup: refresh repository list before restore"));
            refreshRepositoryList();
        }
        {
            forkmesh::StartupTraceStep step(
                QStringLiteral("deferred startup: open saved repository detail"));
            openRepoDetail(index);
        }
        // Land back on whichever tab was actually open last (adhoc #101) — a
        // relaunch should stay on whatever page it's on. Nothing overrides that
        // any more: openRepoDetail() above lands on the Code overview, and the
        // preferred-tab setting that used to detour every launch through Agents
        // is gone (adhoc #119).
        const int savedTab =
            QSettings().value(kLastRepoDetailTabSetting, -1).toInt();
        if (savedTab >= 0) {
            // applyNavDetailTab()'s Agents (tab 3) branch sets the stack index
            // directly rather than driving it through a button click, so — unlike
            // every other tab — it never lazily builds the real page itself. Build
            // it explicitly here so restoring the Agents tab can't leave it showing
            // its unbuilt placeholder.
            {
                forkmesh::StartupTraceStep step(QStringLiteral(
                    "deferred startup: build saved repository tab %1")
                                                    .arg(savedTab));
                ensureRepoDetailTabBuilt(savedTab);
            }
            NavPlace target;
            target.section = 0;
            target.repoIndex = index;
            target.detailTab = savedTab;
            {
                forkmesh::StartupTraceStep step(QStringLiteral(
                    "deferred startup: restore saved repository navigation"));
                applyNavDetailTab(target);
            }
        }
        logStartup(QStringLiteral("saved repository detail restore complete"));
        // Issue metadata is worker-loaded; applyLoadedIssues resumes the looper
        // only after its backlog has arrived.
        logStartup(QStringLiteral(
            "  scheduling background issue metadata reload after restore"));
        reloadIssuesInBackground();
    } else if (m_repoDetailIndex < 0) {
        // No saved repository to restore: land on the selected node's first repo
        // (if any) so a fresh session opens on real content, not an empty panel.
        int firstRepo = -1;
        for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
            if (entry.index >= 0) {
                firstRepo = entry.index;
                break;
            }
        if (firstRepo >= 0) {
            forkmesh::StartupTraceStep step(
                QStringLiteral("deferred startup: open first available repository"));
            openRepoDetail(firstRepo);
        } else {
            logStartup(QStringLiteral(
                "  repository restore skipped: no saved or available repository"));
        }
    }
    m_pendingRestoreRepoIndex = -1;

    // Rescue screenshot attachments still sitting in the old temp directory so
    // the transcripts that reference them keep their thumbnails past the next
    // reboot (adhoc #66). Deferred: it touches the disk and nothing on screen
    // needs it before the first frame.
    {
        forkmesh::StartupTraceStep step(QStringLiteral(
            "deferred startup: migrate legacy prompt image attachments"));
        AgentPromptImages::migrateLegacy();
    }

    // Resume the agent sessions initAgents() re-queued after the restart, only
    // now that the first frame is up and the last repository is restored.
    // Draining inside the constructor started each resumed Claude transcript
    // with its assign-time jump to the Agents tab — a full cold openRepoDetail()
    // (~2s of git reads) before the window could paint, which the restore above
    // then redid. Quiet mode keeps the resumed runs from stealing the view.
    if (!m_agentQueue.isEmpty()) {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: resume %1 queued agent session(s)")
                .arg(m_agentQueue.size()));
        m_agentQuietResume = true;
        processAgentQueue();
        m_agentQuietResume = false;
    } else {
        logStartup(QStringLiteral(
            "  agent resume skipped: no queued sessions from the prior run"));
    }

    // Desktop: connect only now, after the restore, so silent auth never
    // collides with the user's first repository/tab navigation. (On headless
    // this already ran above and is a no-op here.)
    startNetworking();

    // adhoc #73: adhoc #20 dropped every launch-time trigger for the top-bar
    // usage charts in favour of hover-only refreshes, so a restart kept showing
    // whatever percentages were cached before the app closed — stale if usage
    // moved while it was shut down. Do one live Claude Code check here (Codex
    // has no equivalent oauth/usage-style endpoint; its chart already
    // recomputes from the locally tracked window on every launch via
    // buildBreadcrumb's refreshCodexUsageRemaining() call).
    {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: refresh Claude Code usage"));
        refreshClaudeCodeUsage();
    }

    // Hourly snapshot of the live database to the local drive (Settings -> Data
    // -> Automatic backups). Armed for every launch, headless included, but it
    // only starts a timer where backups are actually on: control nodes by
    // default, anyone who ticked the box.
    {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: arm automatic backups"));
        startAutoBackups();
    }

    // A provisioned direct HTTPS mirror (hostname configured + owner-only
    // connector token on disk) used to stay dark after every restart until
    // someone clicked "Start mirror services" — a headless VPS has nobody to
    // click it, so its Cloudflare Tunnel never came back. Auto-start the
    // gateway/Tunnel/registration chain, deferred a further beat so spawning
    // the gateway and cloudflared doesn't compete with the startup sync burst
    // (autoSyncMirrors/performRelaySync fire in this same window).
    QTimer::singleShot(10000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: auto-start direct mirror services"));
        maybeAutoStartDirectMirrorServices();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: direct mirror service auto-start in 10000ms"));
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
    // Bank the still-running uptime clock exactly: the periodic persist in
    // updateHomeStats() is throttled, so quitting mid-session would otherwise
    // drop the minutes since its last write.
    if (m_connectedAtMs > 0)
        QSettings().setValue(kConnectionTotalSetting,
                             m_totalConnectionMs +
                                 QDateTime::currentMSecsSinceEpoch() -
                                 m_connectedAtMs);
    saveChatHistory();
    // Record this session's stop time, then flush+trim the persisted log.
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logSystem(QStringLiteral("Session ended."));
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
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
    // The rail's change badge is on screen for every repo tab, not just the
    // changes panel, so it gets its own (detached) rescan on focus — otherwise
    // a repo opened on Code shows a stale count until the panel is opened.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
        m_railGitButton && m_railGitButton->isVisible())
        refreshRepoChangeBadge();
    // Regaining focus while already parked on the open conversation counts as
    // reading it too — messages that arrived while the window was in the
    // background otherwise leave the unread badge stuck until the user
    // switches away and back.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        clearActiveConversationUnread();
}
