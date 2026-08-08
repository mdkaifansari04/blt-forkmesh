#include "ForkMeshVersion.h"
#include "AgentPromptImages.h"
#include "MainWindow.h"
#include "CrashHandler.h"
#include "MainWindowInternal.h"
#include "NetworkReplyError.h"
#include "StartupSplash.h"
#include "WorldSpeechBridge.h"

using namespace forkmesh::ui;

namespace {

constexpr int kSplashedDeferredStartupDelayMs = 250;

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
        {"/api/metrics", "traffic metrics"},
        {"/api/network", "network stats"},
        {"/api/security", "security report"},
        {"/api/forkbot", "forkbot chat"},
        {"/api/chat", "chat"},
        {"/api/notifications", "ping inbox"},
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
    clearAccountSessionMemory();

    forkmesh::BackgroundActivity::setListener(nullptr);

    const QList<QProcess *> processChildren = findChildren<QProcess *>();
    for (QProcess *process : processChildren)
        process->disconnect();

    if (m_worldSpeechBridge)
        m_worldSpeechBridge->stop();

    QList<QPointer<QProcess>> processes;
    const QList<QProcess *> childProcesses = findChildren<QProcess *>();
    processes.reserve(childProcesses.size());
    for (QProcess *process : childProcesses)
        processes.append(process);
    for (const QPointer<QProcess> &guardedProcess : std::as_const(processes)) {
        QProcess *process = guardedProcess.data();
        if (!process)
            continue;
        QObject::disconnect(process, nullptr, nullptr, nullptr);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(2000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    }

    for (QProcess *process :
         {m_cloudflareBootstrapProcess,
          m_cloudflareTunnelBootstrapProcess,
          m_cloudflaredInstallProcess, m_cloudflaredProcess,
          m_mirrorGatewayProcess}) {
        if (!process)
            continue;
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

void MainWindow::restartMirrorSyncTimer()
{
    if (!m_mirrorSyncTimer)
        return;
    const qint64 baseIntervalMs = mirrorSyncIntervalMs();
    const qint64 jitterSpanMs = baseIntervalMs * kMirrorSyncJitterPercent / 100;
    const qint64 nextIntervalMs =
        baseIntervalMs +
        QRandomGenerator::global()->bounded(-jitterSpanMs, jitterSpanMs + 1);
    m_mirrorSyncTimer->start(int(qMax<qint64>(1000, nextIntervalMs)));
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    logStartup(QStringLiteral("MainWindow ctor begin"));
    const bool externalMirrorBridge =
        qEnvironmentVariableIsSet("FORKMESH_EXTERNAL_MIRROR_NODE");
    const auto traceStep = [](const QString &name, auto &&work) {
        forkmesh::StartupTraceStep step(
            QStringLiteral("MainWindow: %1").arg(name));
        std::forward<decltype(work)>(work)();
    };
#ifndef FORKMESH_WINDOW_TESTS
    traceStep(QStringLiteral("start durable action telemetry"), [] {
        forkmesh::ActionTelemetry::initialize(
            QDir::homePath() +
            QStringLiteral("/.forkmesh/diagnostics/actions.jsonl"));
    });
#endif
    QElapsedTimer chromeTimer;
    chromeTimer.start();
    startupStep(QStringLiteral("Installing shortcuts and window chrome"));
    if (qApp)
        qApp->installEventFilter(this);
    auto *searchShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    searchShortcut->setContext(Qt::ApplicationShortcut);
    connect(searchShortcut, &QShortcut::activated, this,
            &MainWindow::openGlobalSearch);
    setWindowTitle("ForkMesh v" FORKMESH_VERSION);
    setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    setWindowFlag(Qt::FramelessWindowHint, true);
    if (qApp && qApp->styleSheet().isEmpty())
        applyTheme();
    resize(1060, 700);
    const QByteArray savedGeometry =
        QSettings().value(kWindowGeometrySetting).toByteArray();
    if (!savedGeometry.isEmpty())
        restoreGeometry(savedGeometry);

    startupDetail(savedGeometry.isEmpty()
                      ? QStringLiteral("No saved geometry; opening at 1060x700")
                      : QStringLiteral("Restored the last window geometry"));

    startupStep(QStringLiteral("Registering the system tray icon"));
    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_trayIcon->setToolTip("ForkMesh");
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->show();
    logStartup(
        QStringLiteral("DONE  MainWindow: configure window chrome, shortcuts, "
                       "geometry and tray icon (%1ms)")
            .arg(chromeTimer.elapsed()));

    connect(qApp, &QCoreApplication::aboutToQuit, this,
            &MainWindow::saveChatHistory);
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            &MainWindow::clearAccountSessionMemory);

    QElapsedTimer networkTimer;
    networkTimer.start();
    startupStep(QStringLiteral("Bringing up networking and the request firewall"));
    auto *network = new BackoffNetworkAccessManager(this);
    const bool firewallOn =
        QSettings().value(kRequestFirewallEnabledSetting, true).toBool();
    startupDetail(firewallOn
                      ? QStringLiteral("Request firewall: on")
                      : QStringLiteral("Request firewall: off"));
    network->setFirewallEnabled(firewallOn);
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
    connect(m_networkAccess, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) {
                if (!reply ||
                    !QSettings()
                         .value(kVerboseNetworkLogSetting, false)
                         .toBool())
                    return;
                if (reply->url().path() == QStringLiteral("/favicon.ico"))
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
                    status = QStringLiteral("ERR ") +
                             forkmesh::networkFailureText(reply);
                    const QString snippet =
                        forkmesh::networkResponseSnippet(reply->peek(512), 0);
                    if (!snippet.isEmpty())
                        status += QStringLiteral(" [body: ") + snippet +
                                  QStringLiteral("]");
                } else {
                    const QVariant code = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute);
                    status = code.isValid() ? code.toString()
                                            : QStringLiteral("done");
                    const QString snippet =
                        forkmesh::networkResponseSnippet(reply->peek(512), 0);
                    if (!snippet.isEmpty())
                        status += QStringLiteral(" [body: ") + snippet +
                                  QStringLiteral("]");
                }
                logSystem(QString::fromUtf8("net %1 %2 %3 \xC2\xB7 %4")
                              .arg(verb, status, reply->url().toString(),
                                   networkRequestEventFor(reply->url())));
            });
    logStartup(
        QStringLiteral("DONE  MainWindow: configure shared network manager and "
                       "firewall (%1ms)")
            .arg(networkTimer.elapsed()));
    traceStep(QStringLiteral("read connection state and cached model list"),
              [this] {
                  m_totalConnectionMs =
                      QSettings().value(kConnectionTotalSetting).toLongLong();
                  m_nodeOffline =
                      QSettings().value(kNodeOfflineSetting, false).toBool();
                  m_liveClaudeModels =
                      QJsonDocument::fromJson(
                          QSettings()
                              .value(kClaudeModelsCacheSetting)
                              .toByteArray())
                          .array();
              });

    startupStep(QStringLiteral("Loading relays, favicons and the network log"));
    loadServers();
    loadCachedFavicons();
    traceStep(QStringLiteral("restore persisted network log"),
              [this] { loadNetworkLog(); });
    traceStep(QStringLiteral("restore filed pings"),
              [this] { loadNotificationJournal(); });
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logSystem(QStringLiteral("Session started - ForkMesh v" FORKMESH_VERSION "."));
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logStartup(QStringLiteral("servers + favicons loaded"));
    startupDetail(QStringLiteral("%1 relay(s) known").arg(m_servers.size()));

    startupStep(QStringLiteral("Restoring the profile and node name"));

    traceStep(QStringLiteral("restore profile name and avatar"), [this] {
        m_userAvatar = QSettings().value(kAvatarSetting).toByteArray();
        {
            const QString envName =
                accountNameFromInput(qEnvironmentVariable("FORKMESH_NODE_NAME"),
                                     QString());
            if (isValidNodeName(envName) &&
                envName.compare(savedProfileName(), Qt::CaseInsensitive) != 0)
                QSettings().setValue(kAccountNameSetting, envName);
        }
        if (savedProfileName().isEmpty()) {
            m_freshInstall = true;
            const QString generated = randomFunNodeName();
            QSettings().setValue(kAccountNameSetting, generated);
            QSettings().setValue(kGeneratedNodeNameSetting, generated);
        }
        if (const QString saved = savedProfileName().toLower(); !saved.isEmpty())
            m_userName = saved;
    });
    startupDetail(m_freshInstall
                      ? QStringLiteral("First run — this node is now \"%1\"")
                            .arg(savedProfileName())
                      : QStringLiteral("Node name: %1").arg(savedProfileName()));

    startupStep(QStringLiteral("Building the account surface"));
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSetupPage());
    logStartup(QStringLiteral("setup page built"));
    startupStep(QStringLiteral("Building the application shell"));
    m_stack->addWidget(buildChatPage());
    logStartup(QStringLiteral("chat/app page built"));
    m_stack->setCurrentIndex(1); // open in the app shell, always
    setCentralWidget(m_stack);
    if (!externalMirrorBridge) {
        startupStep(QStringLiteral("Warming the Code and Issues surfaces"));
        ensureRepoDetailSectionBuilt();
        ensureRepoDetailTabBuilt(0); // Code (also owns Branches/Worktrees panels)
        ensureRepoDetailTabBuilt(2); // Issues
        logStartup(QStringLiteral("core repository surfaces warmed"));
    }
    startupStep(QStringLiteral("Loading repositories"));
    if (!externalMirrorBridge)
        initializeWorldSpeechBridge();
    loadRepositories();
    if (!externalMirrorBridge)
        refreshRepositoryList();
    logStartup(QStringLiteral("repositories loaded"));
    startupDetail(
        QStringLiteral("%1 repositor%2 on this node")
            .arg(m_repositories.size())
            .arg(m_repositories.size() == 1 ? QStringLiteral("y")
                                            : QStringLiteral("ies")));
    if (!externalMirrorBridge) {
        startupStep(QStringLiteral("Restoring actions and agent sessions"));
        initActions();
        initAgents();
        logStartup(QStringLiteral("actions + agents initialized"));
    }
    if (!m_agentQueue.isEmpty())
        startupDetail(QStringLiteral("%1 agent session(s) queued to resume")
                          .arg(m_agentQueue.size()));
    startupStep(QStringLiteral("Choosing the repository to reopen"));
    const QString lastRepository =
        QSettings().value(kLastRepositorySetting).toString();
    if (!externalMirrorBridge && !lastRepository.isEmpty()) {
        const int slash = lastRepository.indexOf('/');
        if (slash > 0) {
            const int index =
                repoIndexFor(lastRepository.left(slash),
                             lastRepository.mid(slash + 1));
            if (index >= 0) {
                m_pendingRestoreRepoIndex = index;
            }
        }
    }
    logStartup(QStringLiteral("last repository restore scheduled"));
    startupDetail(m_pendingRestoreRepoIndex >= 0
                      ? QStringLiteral("Will reopen %1").arg(lastRepository)
                      : QStringLiteral("Nothing to reopen"));
    startupStep(QStringLiteral("Selecting the active relay"));
    loadActiveServerIntoEdits();
    if (!externalMirrorBridge)
        updateBreadcrumb();
    logStartup(QStringLiteral("active server + breadcrumb"));
    startupStep(QStringLiteral("Fetching relay favicons"));
    if (!externalMirrorBridge)
        for (int i = 0; i < m_servers.size(); ++i)
            fetchFavicon(i);
    logStartup(QStringLiteral("favicons fetched"));

    startupStep(QStringLiteral("Arming background timers"));
    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this] {
        sendTypingState(false);
    });
    m_homeStatsTimer = new QTimer(this);
    connect(m_homeStatsTimer, &QTimer::timeout, this,
            &MainWindow::refreshRepositoryList);
    if (!externalMirrorBridge)
        m_homeStatsTimer->start(60000);
    logStartup(QStringLiteral("  timer armed: home/repository stats every 60s"));

    m_repoChangeBadgeTimer = new QTimer(this);
    connect(m_repoChangeBadgeTimer, &QTimer::timeout, this, [this] {
        if (m_railGitButton && m_railGitButton->isVisible())
            refreshRepoChangeBadge();
    });
    if (!externalMirrorBridge)
        m_repoChangeBadgeTimer->start(10000);
    logStartup(QStringLiteral("  timer armed: Git rail status every 10s"));

    QTimer::singleShot(0, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: diagnostics and UI-stall watchdog"));
        startDiagnostics();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: diagnostics on first event-loop turn"));

    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this,
            &MainWindow::autoSyncMirrorsIfRelayHealthy);
    restartMirrorSyncTimer();
    logStartup(QStringLiteral("  timer armed: mirror safety sync every %1ms")
                   .arg(m_mirrorSyncTimer->interval()));
#ifndef FORKMESH_WINDOW_TESTS
    QTimer::singleShot(1000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: initial mirror synchronization"));
        autoSyncMirrors();
    });
#endif
    logStartup(QStringLiteral(
        "  startup job scheduled: initial mirror synchronization in 1000ms"));
#ifndef FORKMESH_WINDOW_TESTS
    QTimer::singleShot(20000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: initial relay inbox sync"));
        performRelaySync();
    });
#endif
    logStartup(QStringLiteral(
        "  startup job scheduled: initial relay inbox sync in 20000ms"));
    m_autoUpdateTimer = new QTimer(this);
    connect(m_autoUpdateTimer, &QTimer::timeout, this, &MainWindow::maybeAutoUpdate);
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
    m_chatExpiryTimer = new QTimer(this);
    connect(m_chatExpiryTimer, &QTimer::timeout, this,
            &MainWindow::pruneExpiredChatHistory);
    m_chatExpiryTimer->start(60 * 60 * 1000);
    logStartup(QStringLiteral("  timer armed: expired chat pruning every 3600000ms"));
    m_relayLatencyTimer = new QTimer(this);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::probeRelayLatency);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::updateNodeOnlineControls);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::refreshNodeDotMatrix);
    // The footer status dots use the same minute cadence as the public status
    // sampler and its edge cache. This is a single compact `view=world` read,
    // independent of whether the websocket supplied a fresh latency sample.
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::refreshFooterWebsiteStatus);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::refreshDesktopWebsiteProbes);
    m_relayLatencyTimer->start(60 * 1000);
    logStartup(QStringLiteral("  timer armed: relay latency and uptime every 60000ms"));
#ifndef FORKMESH_WINDOW_TESTS
    QTimer::singleShot(2500, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: first relay latency probe"));
        probeRelayLatency();
    });
#endif
    logStartup(QStringLiteral(
        "  startup job scheduled: first relay latency probe in 2500ms"));
    traceStep(QStringLiteral("start operating-system reachability watch"),
              [this] { initRelayReachabilityWatch(); });
    traceStep(QStringLiteral("restore cached organization task badge"),
              [this] { restoreOrganizationTaskBadge(); });
#ifndef FORKMESH_WINDOW_TESTS
    QTimer::singleShot(25000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: refresh organization task badge"));
        refreshOrganizationTaskBadge();
    });
#endif
    logStartup(QStringLiteral(
        "  startup job scheduled: organization task refresh in 25000ms"));
#ifndef FORKMESH_WINDOW_TESTS
    QTimer::singleShot(3000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: ensure flagship repository"));
        ensureFlagshipRepo();
    });
#endif
    logStartup(QStringLiteral(
        "  startup job scheduled: flagship repository check in 3000ms"));

    logStartup(QStringLiteral("timers started"));
    startupDetail(QStringLiteral(
        "Mirror sync, relay sync, chat expiry, latency, auto-update"));

    startupStep(QStringLiteral("Loading the node signing key"));
    if (!m_profileIdentity.load()) {
        startupStepFailed(m_profileIdentity.errorString());
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
    } else {
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
        startupDetail(QStringLiteral("Ed25519 key %1")
                          .arg(m_profileIdentity.shortPublicKey()));
    }
    logStartup(QStringLiteral("identity loaded"));
    QTimer::singleShot(
        0, this, &MainWindow::maybeAutoStartDirectMirrorServices);
    startupStep(QStringLiteral("Computing home statistics"));
    updateHomeStats();
    traceStep(QStringLiteral("populate Hosts navigation count"),
              [this] { refreshHostsTable(); });
    traceStep(QStringLiteral("populate Relays navigation count"),
              [this] { refreshRelaysTable(); });
    if (QSettings().value(kMirrorFleetEnabledSetting, false).toBool()) {
        QTimer::singleShot(0, this, [this] {
            ensureSectionBuilt(kNetworkDiagnosticsSectionIndex);
            reconcileDesiredMirrorFleet();
        });
    }
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
#ifndef FORKMESH_WINDOW_TESTS
    // An Expose event is not proof that Qt has painted the first frame (notably
    // with the offscreen QPA and some compositors). Starting silent auth from
    // that event let key/account work run ahead of paint and restored the black
    // startup pause this deferral was meant to prevent. Give the event loop a
    // bounded five-second interactive runway so silent authentication and session
    // restoration cannot collide with the user's first repository/tab navigation,
    // then run the idempotent startup regardless of platform. Headless launches
    // use the same deterministic fallback. A launch with the splash up beats
    // this timer to it from paintEvent — there, the frame is proven and there
    // is no interactive runway to protect because the card is over the app.
    QTimer::singleShot(5000, this, &MainWindow::runDeferredStartup);
#endif
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    if (m_firstFramePainted)
        return;
    m_firstFramePainted = true;
    startupWindowPainted(QStringLiteral("Main window painted"));
    if (!activeStartupSplash())
        return;
    QTimer::singleShot(kSplashedDeferredStartupDelayMs, this, [this] {
        if (!activeStartupSplash())
            return;
        runDeferredStartup();
    });
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

    const bool externalMirrorBridge =
        m_headless && qEnvironmentVariableIsSet(
                          "FORKMESH_EXTERNAL_MIRROR_NODE");
    bool headlessBootstrapQueued = false;
    auto runHeadlessBootstrap = [this, &headlessBootstrapQueued] {
        if (!m_headless || qEnvironmentVariableIsSet(
                               "FORKMESH_EXTERNAL_MIRROR_NODE"))
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
                    startupStep(QStringLiteral("Signing in as %1").arg(name));
                    authenticateSilently(name);
                }
                {
                    forkmesh::StartupTraceStep step(
                        QStringLiteral("deferred startup: start mesh session"));
                    if (!externalMirrorBridge) {
                        startupStep(QStringLiteral("Connecting to the mesh"));
                        startSession();
                    }
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
                    if (!externalMirrorBridge) {
                        forkmesh::StartupTraceStep step(QStringLiteral(
                            "deferred startup: start headless mesh session"));
                        startSession();
                    }
                }
                runHeadlessBootstrap();
            }
        }

        m_startupAuthResolved = true;
        updateSignInButton();
    };

    if (m_headless)
        startNetworking();

    if (!externalMirrorBridge && m_pendingRestoreRepoIndex >= 0 &&
        m_pendingRestoreRepoIndex < m_repositories.size()) {
        const int index = m_pendingRestoreRepoIndex;
        logStartup(QStringLiteral("restoring saved repository index %1")
                       .arg(index));
        m_selectedNode = m_repositories.at(index).owner;
        startupStep(QStringLiteral("Reopening %1/%2")
                        .arg(m_repositories.at(index).owner,
                             m_repositories.at(index).name));
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
        const int savedTab =
            QSettings().value(kLastRepoDetailTabSetting, -1).toInt();
        if (savedTab >= 0) {
            startupStep(QStringLiteral("Restoring the last open tab"));
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
        logStartup(QStringLiteral(
            "  scheduling background issue metadata reload after restore"));
        reloadIssuesInBackground();
    } else if (!externalMirrorBridge && m_repoDetailIndex < 0) {
        int firstRepo = -1;
        for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries))
            if (entry.index >= 0) {
                firstRepo = entry.index;
                break;
            }
        if (firstRepo >= 0) {
            forkmesh::StartupTraceStep step(
                QStringLiteral("deferred startup: open first available repository"));
            startupStep(QStringLiteral("Opening %1/%2")
                            .arg(m_repositories.at(firstRepo).owner,
                                 m_repositories.at(firstRepo).name));
            openRepoDetail(firstRepo);
        } else {
            logStartup(QStringLiteral(
                "  repository restore skipped: no saved or available repository"));
        }
    }
    m_pendingRestoreRepoIndex = -1;

    if (!externalMirrorBridge) {
        forkmesh::StartupTraceStep step(QStringLiteral(
            "deferred startup: migrate legacy prompt image attachments"));
        startupStep(QStringLiteral("Checking prompt attachments"));
        AgentPromptImages::migrateLegacy();
    }

    if (!externalMirrorBridge && !m_agentQueue.isEmpty()) {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: resume %1 queued agent session(s)")
                .arg(m_agentQueue.size()));
        startupStep(QStringLiteral("Resuming %1 agent session(s)")
                        .arg(m_agentQueue.size()));
        m_agentQuietResume = true;
        processAgentQueue();
        m_agentQuietResume = false;
    } else {
        logStartup(QStringLiteral(
            "  agent resume skipped: no queued sessions from the prior run"));
    }

    startNetworking();

    if (!externalMirrorBridge) {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: refresh Claude Code usage"));
        startupStep(QStringLiteral("Refreshing Claude Code usage"));
        refreshClaudeCodeUsage();
    }

    {
        forkmesh::StartupTraceStep step(
            QStringLiteral("deferred startup: arm automatic backups"));
        startupStep(QStringLiteral("Arming automatic backups"));
        startAutoBackups();
    }

    QTimer::singleShot(10000, this, [this] {
        forkmesh::StartupTraceStep step(
            QStringLiteral("delayed startup: auto-start direct mirror services"));
        maybeAutoStartDirectMirrorServices();
    });
    logStartup(QStringLiteral(
        "  startup job scheduled: direct mirror service auto-start in 10000ms"));

    // This is the end of startup as a user experiences it: signed in, session
    // up, last repository open on its last tab, agents resumed. The splash has
    // narrated the whole way here rather than bowing out at first paint, so
    // this is where it says Ready and fades. What is left below this line is
    // long-fuse background work (the ten-second mirror auto-start above, the
    // sync timers armed in the constructor) that the app is fully usable
    // without. No-op when no splash is up — headless, or the user turned it off.
    finishStartupSplash(QStringLiteral("Ready"));
}

void MainWindow::applyTheme()
{
    qApp->setStyleSheet(Theme::styleSheetForDark(currentThemeIsDark()));
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *window = qobject_cast<MainWindow *>(widget)) {
            window->refreshThemedIcons();
            window->styleFooterUpdateLog();
        }
    }
}

void MainWindow::refreshThemedIcons()
{
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        applyStoredOcticon(button);
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels)
        applyStoredLabelOcticon(label);
    const auto tabWidgets = findChildren<QTabWidget *>();
    for (QTabWidget *tabs : tabWidgets)
        refreshTabOcticons(tabs);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_closingDown = true;
    setCloudLogMonitorEnabled(false);
    QSettings().setValue(kWindowGeometrySetting, saveGeometry());
    savePromptOverlayPlacement();
    if (m_connectedAtMs > 0)
        QSettings().setValue(kConnectionTotalSetting,
                             m_totalConnectionMs +
                                 QDateTime::currentMSecsSinceEpoch() -
                                 m_connectedAtMs);
    saveChatHistory();
    saveNotificationJournal();
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logSystem(QStringLiteral("Session ended."));
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    saveNetworkLog();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
        m_scmTree && m_scmTree->isVisible()) {
        refreshSourceControl();
        refreshCommitMarkersIfStale();
    }
    if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
        m_railGitButton && m_railGitButton->isVisible())
        refreshRepoChangeBadge();
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        clearActiveConversationUnread();
}
