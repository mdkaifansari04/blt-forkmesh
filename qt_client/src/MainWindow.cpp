#include "ForkMeshVersion.h"
#include "AgentPromptImages.h"
#include "MainWindow.h"
#include "CrashHandler.h"
#include "MainWindowInternal.h"
#include "WorldSpeechBridge.h"

using namespace forkmesh::ui;

namespace {







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

}

MainWindow::~MainWindow()
{



    forkmesh::BackgroundActivity::setListener(nullptr);



    if (m_worldSpeechBridge)
        m_worldSpeechBridge->stop();






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
#ifndef FORKMESH_WINDOW_TESTS


    forkmesh::ActionTelemetry::initialize(
        QDir::homePath() +
        QStringLiteral("/.forkmesh/diagnostics/actions.jsonl"));
#endif
    logStartup(QStringLiteral("MainWindow ctor begin"));




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

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_trayIcon->setToolTip("ForkMesh");
    if (QSystemTrayIcon::isSystemTrayAvailable())
        m_trayIcon->show();




    connect(qApp, &QCoreApplication::aboutToQuit, this,
            &MainWindow::saveChatHistory);






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





                    const QVariant code = reply->attribute(
                        QNetworkRequest::HttpStatusCodeAttribute);
                    status = code.isValid()
                                 ? QStringLiteral("ERR %1 %2")
                                       .arg(code.toString(), reply->errorString())
                                 : QStringLiteral("ERR ") + reply->errorString();






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
    m_totalConnectionMs = QSettings().value(kConnectionTotalSetting).toLongLong();
    m_nodeOffline = QSettings().value(kNodeOfflineSetting, false).toBool();




    m_liveClaudeModels =
        QJsonDocument::fromJson(
            QSettings().value(kClaudeModelsCacheSetting).toByteArray())
            .array();

    loadServers();
    loadCachedFavicons();





    loadNetworkLog();
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logSystem(QStringLiteral("Session started - ForkMesh v" FORKMESH_VERSION "."));
    logSystem(QStringLiteral("════════════════════════════════════════════════════════════"));
    logStartup(QStringLiteral("servers + favicons loaded"));







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

    m_stack = new QStackedWidget(this);





    m_stack->addWidget(buildSetupPage());
    logStartup(QStringLiteral("setup page built"));
    m_stack->addWidget(buildChatPage());
    logStartup(QStringLiteral("chat/app page built"));
    m_stack->setCurrentIndex(1);
    setCentralWidget(m_stack);






    ensureRepoDetailSectionBuilt();
    ensureRepoDetailTabBuilt(0);
    ensureRepoDetailTabBuilt(2);
    logStartup(QStringLiteral("core repository surfaces warmed"));
    initializeWorldSpeechBridge();
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


    connect(m_homeStatsTimer, &QTimer::timeout, this,
            &MainWindow::refreshRepositoryList);
    m_homeStatsTimer->start(60000);








    m_repoChangeBadgeTimer = new QTimer(this);
    connect(m_repoChangeBadgeTimer, &QTimer::timeout, this, [this] {
        if (m_railGitButton && m_railGitButton->isVisible())
            refreshRepoChangeBadge();
    });
    m_repoChangeBadgeTimer->start(10000);



    QTimer::singleShot(0, this, [this] { startDiagnostics(); });













    m_mirrorSyncTimer = new QTimer(this);
    connect(m_mirrorSyncTimer, &QTimer::timeout, this,
            &MainWindow::autoSyncMirrorsIfRelayHealthy);
    const int mirrorJitterSpanMs =
        int(kMirrorSyncIntervalMs * kMirrorSyncJitterPercent / 100);
    m_mirrorSyncTimer->start(int(kMirrorSyncIntervalMs) +
                             QRandomGenerator::global()->bounded(
                                 -mirrorJitterSpanMs, mirrorJitterSpanMs + 1));
    QTimer::singleShot(15000, this, &MainWindow::autoSyncMirrors);









    m_inboxPollTimer = new QTimer(this);
    connect(m_inboxPollTimer, &QTimer::timeout, this, &MainWindow::performRelaySync);
    m_inboxPollTimer->start(5 * 60 * 1000);
    QTimer::singleShot(20000, this, &MainWindow::performRelaySync);





    m_autoUpdateTimer = new QTimer(this);
    connect(m_autoUpdateTimer, &QTimer::timeout, this, &MainWindow::maybeAutoUpdate);




    m_autoUpdateTimer->start(
        60 * 60 * 1000 +
        int(QRandomGenerator::global()->bounded(-10 * 60 * 1000,
                                                10 * 60 * 1000 + 1)));
    QTimer::singleShot(
        5 * 60 * 1000 +
            int(QRandomGenerator::global()->bounded(10 * 60 * 1000)),
        this, &MainWindow::maybeAutoUpdate);



    m_chatExpiryTimer = new QTimer(this);
    connect(m_chatExpiryTimer, &QTimer::timeout, this,
            &MainWindow::pruneExpiredChatHistory);
    m_chatExpiryTimer->start(60 * 60 * 1000);



    m_relayLatencyTimer = new QTimer(this);
    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::probeRelayLatency);



    connect(m_relayLatencyTimer, &QTimer::timeout, this,
            &MainWindow::updateNodeOnlineControls);
    m_relayLatencyTimer->start(60 * 1000);
    QTimer::singleShot(2500, this, &MainWindow::probeRelayLatency);



    initRelayReachabilityWatch();




    restoreOrganizationTaskBadge();
    QTimer::singleShot(25000, this, &MainWindow::refreshOrganizationTaskBadge);


    QTimer::singleShot(3000, this, &MainWindow::ensureFlagshipRepo);

    logStartup(QStringLiteral("timers started"));
    if (!m_profileIdentity.load()) {
        m_setupError->setText(m_profileIdentity.errorString());
        m_setupError->show();
    } else {



        if (m_pubkeyLabel) {
            m_pubkeyLabel->setText("Ed25519 public key: " +
                                   m_profileIdentity.shortPublicKey());
            m_pubkeyLabel->setToolTip(m_profileIdentity.publicKey());
        }







        m_pendingSilentAuth = true;
    }
    logStartup(QStringLiteral("identity loaded"));
    // A provisioned headless mirror never builds or opens the Control Node
    // page, which used to be the only path that armed its gateway/tunnel
    // services. Defer until construction and setHeadlessMode() have completed;
    // the helper still requires the persisted hostname plus an owner-only
    // connector token, so ordinary clients with no endpoint are a no-op.
    QTimer::singleShot(
        0, this, &MainWindow::maybeAutoStartDirectMirrorServices);
    updateHomeStats();


    refreshHostsTable();
    refreshRelaysTable();
    logStartup(QStringLiteral("home stats updated (ctor end)"));
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_deferredStartupStarted)
        return;
    m_deferredStartupStarted = true;








    QTimer::singleShot(5000, this, &MainWindow::runDeferredStartup);
}

void MainWindow::runDeferredStartup()
{
    if (m_deferredStartupRun)
        return;
    m_deferredStartupRun = true;
    if (QWindow *handle = windowHandle())
        handle->removeEventFilter(this);

    bool headlessBootstrapQueued = false;
    auto runHeadlessBootstrap = [this, &headlessBootstrapQueued] {
        if (!m_headless)
            return;
        headlessBootstrapQueued = true;
        logSystem(QStringLiteral(
            "Startup: checking forkmesh/forkmesh mirror bootstrap."));
        ensureFlagshipRepo();
        QTimer::singleShot(10000, this, [this] {
            logSystem(QStringLiteral(
                "Startup: rechecking forkmesh/forkmesh mirror bootstrap."));
            ensureFlagshipRepo();
        });
    };










    auto startNetworking = [&] {
        if (m_pendingSilentAuth) {
            m_pendingSilentAuth = false;
            const QString name = m_nameEdit
                                     ? m_nameEdit->text().trimmed().toLower()
                                     : QString();
            if (!name.isEmpty() && isValidNodeName(name)) {
                authenticateSilently(name);
                startSession();
                runHeadlessBootstrap();
            }
        }





        if (m_headless && !headlessBootstrapQueued) {
            const QString name =
                accountNameFromInput(savedProfileName(), QString());
            if (!name.isEmpty() && isValidNodeName(name)) {
                if (!m_backend) {
                    if (m_nameEdit)
                        m_nameEdit->setText(name);
                    authenticateSilently(name);
                    startSession();
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








    if (m_headless)
        startNetworking();



    if (m_pendingRestoreRepoIndex >= 0 &&
        m_pendingRestoreRepoIndex < m_repositories.size()) {
        const int index = m_pendingRestoreRepoIndex;
        logStartup(QStringLiteral("restoring last repository (deferred)"));
        m_selectedNode = m_repositories.at(index).owner;
        refreshRepositoryList();
        openRepoDetail(index);





        const int savedTab =
            QSettings().value(kLastRepoDetailTabSetting, -1).toInt();
        if (savedTab >= 0) {





            ensureRepoDetailTabBuilt(savedTab);
            NavPlace target;
            target.section = 0;
            target.repoIndex = index;
            target.detailTab = savedTab;
            applyNavDetailTab(target);
        }
        logStartup(QStringLiteral("last repository detail loaded"));


        reloadIssuesInBackground();
    } else if (m_repoDetailIndex < 0) {


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





    AgentPromptImages::migrateLegacy();







    if (!m_agentQueue.isEmpty()) {
        m_agentQuietResume = true;
        processAgentQueue();
        m_agentQuietResume = false;
        logStartup(QStringLiteral("agent sessions resumed (deferred)"));
    }




    startNetworking();








    refreshClaudeCodeUsage();





    startAutoBackups();








    QTimer::singleShot(10000, this,
                       &MainWindow::maybeAutoStartDirectMirrorServices);
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
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(kWindowGeometrySetting, saveGeometry());



    if (m_connectedAtMs > 0)
        QSettings().setValue(kConnectionTotalSetting,
                             m_totalConnectionMs +
                                 QDateTime::currentMSecsSinceEpoch() -
                                 m_connectedAtMs);
    saveChatHistory();

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
