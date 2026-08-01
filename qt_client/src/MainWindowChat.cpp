// MainWindowChat: MainWindow feature methods, split out of MainWindow.cpp.
// Peer chat: the server rail, favicons, and the chat page (messages, rooms, DMs).
//
// These are MainWindow member functions defined in their own translation unit;
// the class itself is declared in MainWindow.h. Shared helpers live in
// MainWindowInternal.h / MainWindowShared.cpp (namespace forkmesh::ui).

#include "ForkMeshVersion.h"
#include "MainWindow.h"
#include "MainWindowInternal.h"
#include "ActionStore.h"
#include "ControlNode.h"
#include "CurrentPageStack.h"
#include "KebabHeaderView.h"
#include "PrivateMirrorStore.h"
#include "PublicMirrorRuntime.h"
#include "RepoSecurity.h"
#include "RewardPoolSigner.h"
#include "ScreenCaptureOverlay.h"
#include "ScreenshotMarkupWindow.h"
#include "TerminalWidget.h"
#include "WorldSpeechBridge.h"

#include <QBrush>
#include <QCryptographicHash>
#include <QDialog>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QInputDialog>
#include <QNetworkInformation>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScreen>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUuid>

#include <algorithm>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <signal.h>
#endif

using namespace forkmesh::ui;

namespace {
// Background strip geometry (adhoc #421): five one-word rows are visible, the
// sixth kind of work scrolls.
constexpr int kBackgroundTaskVisibleRows = 5;
constexpr int kBackgroundTaskRowSpacing = 3;
// Work that finishes inside this window never gets a row. Almost every git read
// lands well under it, so the strip shows genuinely slow jobs and no widget is
// created (let alone destroyed) for the hundreds of fast ones.
constexpr qint64 kBackgroundTaskShowAfterMs = forkmesh::kBackgroundShowAfterMs;
// Idle ticks kept before the spin timer stands down, so a burst of short jobs
// doesn't start/stop it repeatedly. The panel itself stays on screen either way
// (adhoc #419) — only the spinner animation stands down.
constexpr int kBackgroundTaskIdleTicksBeforeStop = 12;
// Completed tickets are logged as one execution-aware summary per kind instead
// of one line each: the hot async git/network paths open hundreds of them and
// the log is persisted line by line. A pending summary is flushed once its
// first ticket is this old, or as soon as the strip goes quiet.
constexpr qint64 kBackgroundTaskFastFlushMs = 2000;
} // namespace

// -------------------------------------------------------------- server rail

void MainWindow::loadServers()
{
    // One-way migration from legacy host records: passwords used to be stored
    // inside hosts/list. Keep them only for this process lifetime, and rewrite
    // QSettings before any controller UI or fleet operation can read them.
    QSettings hostSettings;
    forkmesh::control::loadSavedHosts(
        hostSettings, kHostsSetting, &m_hostSessionPasswords);

    m_servers.clear();
    bool migratedDefaultRoom = false;
    const QString json = QSettings().value(kServersArray).toString();
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QString url = obj.value("url").toString().trimmed();
        if (url.isEmpty())
            continue;
        ServerConfig server;
        server.url = canonicalServerUrl(url);
        server.room = obj.value("room").toString(kDefaultRoomName);
        migratedDefaultRoom |=
            forkmesh::mainnode::migrateSavedDefaultRoom(
                &server.url, &server.room);
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
        migratedDefaultRoom |=
            forkmesh::mainnode::migrateSavedDefaultRoom(
                &server.url, &server.room);
        m_servers.append(server);
    }

    m_activeServer = QSettings().value(kActiveServerSetting, 0).toInt();
    if (m_activeServer < 0 || m_activeServer >= m_servers.size())
        m_activeServer = 0;
    // An older install can have a current servers/items array beside stale
    // legacy single-server keys. Detect that split state too; saveServers()
    // below reconciles the compatibility keys to the selected current entry.
    QString legacySingleUrl =
        QSettings().value(kServerUrlSetting).toString().trimmed();
    QString legacySingleRoom =
        QSettings().value(kRoomNameSetting, kDefaultRoomName).toString();
    migratedDefaultRoom |=
        forkmesh::mainnode::migrateSavedDefaultRoom(
            &legacySingleUrl, &legacySingleRoom);
    // Write the one-way migration immediately. Other startup paths still read
    // the legacy single-server keys, and a crash before the user opens Settings
    // must not put the next launch back onto the retired room.
    if (migratedDefaultRoom)
        saveServers();
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
    server.url = canonicalServerUrl(server.url);
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
    fetchFaviconFromUrl(serverHost(m_servers.at(index).url),
                        faviconUrl(m_servers.at(index).url));
}

// Fetch the favicon for a bare host (as it appears in a network-log URL), so
// the log can lead each request line with the site's icon (adhoc #190).
void MainWindow::fetchFaviconForHost(const QString &host)
{
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(host);
    url.setPath(QStringLiteral("/favicon.ico"));
    fetchFaviconFromUrl(host, url);
}

// Shared favicon download: caches to memory + disk keyed by host, de-duplicates
// concurrent fetches via m_faviconFetching, and notifies the breadcrumb rail
// and the network log once the icon lands.
void MainWindow::fetchFaviconFromUrl(const QString &host, const QUrl &url)
{
    if (host.isEmpty() || m_faviconCache.contains(host) ||
        m_faviconFetching.contains(host) || m_faviconMissing.contains(host))
        return;

    // Hosts with a hardcoded mark (API endpoints such as api.anthropic.com and
    // api.mainnet-beta.solana.com) never hit the network: their /favicon.ico
    // requests fail and otherwise appear as error lines in the log.
    // Cached like a downloaded icon (but never written to the disk cache) so the
    // breadcrumb rail and both log views pick it up the same way; no breadcrumb
    // rebuild from here, since this runs while a log line is being rendered.
    const QPixmap builtin = builtinFavicon(host);
    if (!builtin.isNull()) {
        m_faviconCache.insert(host, builtin);
        refreshLogFavicon(host);
        return;
    }

    // Reuse a previously downloaded icon on disk before hitting the network,
    // so a host seen in a past session doesn't re-fetch on every launch.
    QPixmap disk;
    const QString cached = faviconCachePath(host);
    if (QFileInfo::exists(cached) && disk.load(cached) && !disk.isNull()) {
        m_faviconCache.insert(host, disk);
        refreshLogFavicon(host);
        return;
    }

    if (!url.isValid() || !m_networkAccess)
        return;

    m_faviconFetching.insert(host);
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, host] {
        reply->deleteLater();
        m_faviconFetching.remove(host);
        // Remember hosts that have no usable favicon so the next log line from
        // the same host doesn't fire another doomed request.
        if (reply->error() != QNetworkReply::NoError) {
            m_faviconMissing.insert(host);
            return;
        }
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll()) || pix.isNull()) {
            m_faviconMissing.insert(host);
            return;
        }
        if (pix.width() > 64)
            pix = pix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_faviconCache.insert(host, pix);
        QDir().mkpath(faviconCacheDir());
        pix.save(faviconCachePath(host), "PNG");
        updateBreadcrumb();
        refreshLogFavicon(host);
    });
}

// ----------------------------------------------------------------- chat page

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;
    auto *shell = new QWidget;

    // One page per "place": Home holds the repos, quest board and chat all at
    // once (no nav bar — you click a server to see everything). Repo detail and
    // Settings are opened on demand (clicking a repo / the server-rail gear).
    m_sectionStack = new CurrentPageStack;
    // The quick-add Enter target depends on the Agents tab being on screen
    // (quickAddShouldFollowUpAgent), which includes being on the Home section
    // at all — refresh the "new"/"add" styling when the section changes too.
    connect(m_sectionStack, &QStackedWidget::currentChanged, this,
            [this](int) {
                updateQuickAddEnterTarget();
                updateRepoActivityRail();
            });
    // Home now hosts the nodes column, repositories column and the repo detail
    // panel (with Chat as a tab) all at once, so there is no separate repo-detail
    // section any more.
    m_sectionStack->addWidget(buildHomeSection());       // 0 Home (nodes + repos + detail)
    logStartup(QStringLiteral("  buildChatPage: home section built"));
    auto addDeferredSection = [this] {
        auto *placeholder = new QWidget;
        placeholder->setProperty("forkmeshDeferredSection", true);
        m_sectionStack->addWidget(placeholder);
    };
    // Only Home participates in the first frame. The other substantial pages
    // are built on their first navigation; their fixed stack indexes remain
    // unchanged, and showSection() performs the replacement before display.
    addDeferredSection();                              // 1 Settings
    // Session startup resets the chat widgets immediately after first paint,
    // so keep this comparatively small page ready.
    m_sectionStack->addWidget(buildChatSection());      // 2 Chat
    for (int index = 3; index <= 8; ++index)
        addDeferredSection();
    m_sectionStack->addWidget(new QWidget);              // 9 retired Firewall redirect
    for (int index = 10; index <= 15; ++index)
        addDeferredSection();
    logStartup(QStringLiteral("  buildChatPage: secondary sections deferred"));

    auto *content = new QWidget;
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_sectionStack, 1);

    // Optional public payout-address notice. It is hidden outside explicit
    // reward settings and never gates entry or core repository features.
    // The complete header is laid out above the rail below, so the custom
    // window-chrome line spans edge-to-edge instead of starting after the rail.
    QWidget *header = buildBreadcrumb();
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSolanaNotice());
    layout->addWidget(buildWalletVerifyNotice());
    // No page-wide QScrollArea around the sections any more (adhoc #108).
    // Each section scrolls its own content (QScrollArea panels, tables and
    // lists), so the outer wrapper only added a second scroll surface plus its
    // sizeHint-driven overflow spacing. The Ignored vertical policy keeps a
    // tall page from growing the window's minimum height (CurrentPageStack
    // already sizes the stack to the current page, not the tallest sibling).
    content->setMinimumHeight(0);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    // The main content and the always-on prompt/log footer sit one above the
    // other with the footer pinned to a fixed height (adhoc #86). Before this,
    // the footer was a stretch-0 strip whose height tracked its own contents,
    // so anything that changed its size — the "Agents:" status strip
    // appearing, an attachment thumbnail, a growing prompt — reflowed the
    // strip and dragged the whole toolbar up or down as the interface
    // "shifted". A user-draggable splitter (adhoc #19) fixed that but its
    // handle flashed a bright, saturated blue on hover/drag; since the footer
    // no longer needs to be resizable, a fixed-height widget with the same
    // subtle divider line gives the definite height without the loud handle.
    auto *bodyLayout = new QVBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(content, 1); // content absorbs window growth
    auto *logDock = buildNetworkLogDock();
    // buildNetworkLogDock() pins its own fixed height to fit the compact prompt
    // card (adhoc #107); the footer still keeps a constant height so the toolbar
    // never reflows, it is just no longer the old oversized 240px.
    bodyLayout->addWidget(logDock, 0);
    layout->addLayout(bodyLayout, 1);

    // One persistent VS Code-style rail owns app navigation. It begins below
    // the edge-to-edge header and remains visible beside every app view.
    auto *rail = new QWidget;
    rail->setObjectName(QStringLiteral("appNavigationRailContent"));
    rail->setMinimumWidth(railItemWidth());
    m_appNavigationRailLayout = new QVBoxLayout(rail);
    m_appNavigationRailLayout->setContentsMargins(0, 4, 0, 4);
    m_appNavigationRailLayout->setSpacing(1);
    // Every rail destination is the same item now (adhoc #117): one
    // ActivityRailButton — a 20px octicon SVG over a 10px caption at
    // railItemWidth() x kRailItemHeight — so icons, words, hover and the
    // checked accent line all read identically down the rail.
    //
    // Agents heads the rail (adhoc #70) — a regular destination like the rest,
    // badged with the running-session count. Only its fleet matrix stayed on the
    // window-chrome line (see buildBreadcrumb). The contextual Code and Git
    // entries are inserted directly below it by buildRepoDetail().
    // Log and Tasks live in the bottom utility group instead of here; Tasks sits
    // directly above Pings there (adhoc #97).
    for (QPushButton *button :
         {m_agentsNavButton, m_reposNavButton, m_chatButton,
          m_controlNodeNavButton, m_networkNavButton})
        m_appNavigationRailLayout->addWidget(button, 0, Qt::AlignLeft);
    // Repo is redundant with the contextual Code entry. Keep the hidden button
    // as section 0's QButtonGroup state carrier for programmatic navigation.
    m_repoViewButton->setParent(header);
    m_repoViewButton->hide();
    m_appNavigationRailLayout->addStretch();

    // Settings and the screen/dev tools form the bottom utility group — the
    // same full rail items as the primary destinations above the stretch, in
    // the established order: Settings, Log, Capture, Resize, then Tasks
    // directly above Pings (adhoc #97).
    for (QPushButton *button :
         {m_settingsNavButton, m_logNavButton, m_navScreenshotButton,
          m_navResizeButton, m_tasksNavButton, m_notificationButton})
        m_appNavigationRailLayout->addWidget(button, 0, Qt::AlignLeft);

    // The account avatar is intentionally the bottom-most rail destination. It
    // keeps the round user picture (not an octicon), sized and captioned like
    // every other item.
    auto *accountLabel = new QLabel(QStringLiteral("Account"));
    accountLabel->setObjectName(QStringLiteral("railItemLabel"));
    accountLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *accountHost = new QWidget;
    accountHost->setFixedSize(railItemWidth(), kRailItemHeight);
    auto *accountLayout = new QVBoxLayout(accountHost);
    accountLayout->setContentsMargins(0, 2, 0, 0);
    accountLayout->setSpacing(2);
    accountLayout->addWidget(m_userAvatarNavButton, 0, Qt::AlignHCenter);
    accountLayout->addWidget(accountLabel, 0, Qt::AlignHCenter);
    m_appNavigationRailLayout->addWidget(accountHost, 0, Qt::AlignLeft);
    updateNotificationButton();

    // A short window can scroll the rail without forcing the whole app taller.
    // At normal heights every caption and count remains simultaneously visible.
    auto *railScroll = new QScrollArea;
    railScroll->setObjectName(QStringLiteral("appNavigationRail"));
    railScroll->setWidget(rail);
    railScroll->setWidgetResizable(true);
    railScroll->setFrameShape(QFrame::NoFrame);
    railScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    railScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    railScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    railScroll->setFixedWidth(railWidth());
    railScroll->setMinimumHeight(0);
    railScroll->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);

    auto *lower = new QWidget;
    auto *lowerLayout = new QHBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(0);
    lowerLayout->addWidget(railScroll);
    lowerLayout->addWidget(shell, 1);

    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addWidget(lower, 1);
    // Thin one-line strip under everything else, spanning the rail as well as
    // the content shell so it reads as the window's own bottom edge (adhoc #2).
    root->addWidget(buildStatusBar());
    return page;
}

// A single text line tall: the branch switcher and the repo's git identity (both
// of which used to sit inside the repo Code overview) plus the on-disk location
// of the running executable. The widgets are created here, not in the repo pages
// they came from, because those pages build lazily on first navigation while the
// strip has to be populated from the first frame; setRepoBranch /
// loadBranchesAndTags / updateFooterGitIdentity keep filling them in as before,
// and updateFooterCommitInfo adds the commit that branch is on.
QWidget *MainWindow::buildStatusBar()
{
    auto *bar = new QWidget;
    bar->setObjectName("appStatusBar");

    m_branchButton = new QPushButton("main");
    m_branchButton->setObjectName("ghostButton");
    m_branchButton->setCursor(Qt::PointingHandCursor);
    m_branchButton->setToolTip("Switch branch");
    setOcticon(m_branchButton, "git-branch", 12);

    m_footerGitIdentity = new QLabel;
    m_footerGitIdentity->setObjectName("footerGitIdentity");
    m_footerGitIdentity->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_footerGitIdentity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_footerGitIdentity->setToolTip(
        "Git author identity configured for the repository you're viewing");

    m_footerCommitInfo = new QLabel;
    m_footerCommitInfo->setObjectName("footerCommitInfo");
    m_footerCommitInfo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_footerCommitInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_footerCommitInfo->setToolTip("Commit the browsed branch points at");

    // Elided up front rather than on every resize: the path never changes while
    // the app runs, and a full path left unelided would drag the window's
    // minimum width out with it. The tooltip keeps the untruncated value.
    const QString appPath =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    m_statusAppPath = new QLabel;
    m_statusAppPath->setObjectName("statusAppPath");
    m_statusAppPath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusAppPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusAppPath->setText(m_statusAppPath->fontMetrics().elidedText(
        appPath, Qt::ElideMiddle, 420));
    m_statusAppPath->setToolTip(
        QStringLiteral("Running app: %1\nWorking directory: %2")
            .arg(appPath, QDir::toNativeSeparators(QDir::currentPath())));

    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(10);
    row->addWidget(m_branchButton);
    row->addWidget(m_footerGitIdentity);
    row->addWidget(m_footerCommitInfo);
    row->addStretch(1);
    row->addWidget(m_statusAppPath);

    // One line, nothing more: the tallest child (the branch button) is capped to
    // the strip so the menu indicator can't push the bar taller.
    const int rowHeight = qMax(20, bar->fontMetrics().height() + 6);
    bar->setFixedHeight(rowHeight);
    m_branchButton->setMaximumHeight(rowHeight - 2);
    return bar;
}

// kFooterLogSeedLines (MainWindowInternal.h) bounds both the startup seed and
// the live buffer: well below kNetworkLogLimit so the corner widget (unlike the
// full Log tab, which defers its own render until first visit) stays cheap to
// populate on every launch while still giving a real scrollback to search.

QWidget *MainWindow::buildNetworkLogDock()
{
    // Full-width, grey-bordered quick-add bar: the issue input expands on the
    // left, then a flexible gap pushes the donate/Reddit/X cluster to the far
    // right.
    auto *dock = new QWidget;
    m_footerDock = dock;
    dock->setObjectName("logDock");

    // A three-line wrapping box (adhoc #12, #107), not a single-line edit, so the
    // typed prompt is actually visible on three lines. Enter sends / Shift+Enter
    // adds a newline (handled in the event filter); Up/Down walk prompt history.
    m_issueQuickAdd = new QPlainTextEdit;
    m_issueQuickAdd->setObjectName("issueQuickAdd");
    m_issueQuickAdd->setPlaceholderText("enter prompt");
    m_issueQuickAdd->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_issueQuickAdd->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_issueQuickAdd->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Pin the field to a fixed number of prompt lines (adhoc #107) so it stays
    // compact instead of stretching to fill the whole footer; longer prompts
    // scroll within it. Moving the send column out to the side (adhoc #115) freed
    // the vertical space the toolbar used to reserve for the stacked buttons, and
    // dropping the surrounding card and the "Agents:" strip (adhoc #60) freed two
    // more rows, so the box now shows six lines and the prompt frame fills the
    // footer top to bottom the way the log panel beside it does.
    m_issueQuickAdd->document()->setDocumentMargin(3);
    // 6 rows + the QSS vertical padding (8px top/bottom) + document margins.
    const int kQuickAddRowH = m_issueQuickAdd->fontMetrics().lineSpacing();
    m_issueQuickAdd->setFixedHeight(kQuickAddRowH * 6 + 16 + 6);
    m_issueQuickAdd->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

    m_quickAddAgentProvider = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddAgentProvider->setObjectName("quickAddAgentSelector");
    // "Manual" (adhoc #29): the no-agent choice that replaces the old Agent /
    // Create-issue checkboxes — picking it files an issue from the typed prompt
    // instead of starting a coding agent. Every item is short (adhoc #38) so the
    // four dropdowns fit the composer row side by side; the tooltip carries what
    // the labels no longer spell out.
    m_quickAddAgentProvider->addItem(QStringLiteral("Manual"),
                                     QStringLiteral("manual"));
    m_quickAddAgentProvider->addItem(QStringLiteral("Codex"), kCodexProvider);
    m_quickAddAgentProvider->addItem(QStringLiteral("OpenAI"),
                                     QStringLiteral("openai"));
    m_quickAddAgentProvider->addItem(QStringLiteral("Claude API"),
                                     QStringLiteral("claude-api"));
    // "Claude Code" drives the real `claude` CLI headlessly (no input) in a
    // tracked agent session, working until ForkMesh can open a PR from its diff.
    m_quickAddAgentProvider->addItem(QStringLiteral("CC"),
                                     QStringLiteral("claude-code"));
    selectQuickAddAgentProvider(m_quickAddAgentProvider);
    m_quickAddAgentProvider->setToolTip(
        "What picks this prompt up: CC (Claude Code) or Codex run the CLI agents, "
        "OpenAI/Claude API run the headless API agents, and Manual files an issue "
        "instead of starting one.");
    // No fixed width band (adhoc #72): FullPopupComboBox sizes itself to the
    // label it is showing, so the four dropdowns take only the room they need.
    // Show the whole list at once rather than a scrollable popup (adhoc #99).
    m_quickAddAgentProvider->setMaxVisibleItems(30);
    m_quickAddAgentProvider->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Model chooser (adhoc #261/#349): Claude Code uses the live Claude model
    // list; Codex uses the ChatGPT-backed Codex CLI's supported model list.
    m_quickAddClaudeModel = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddClaudeModel->setObjectName("quickAddModelSelector");
    // Show the whole model list at once rather than a scrollable popup, even
    // once the live provider list-up fills in more than a handful (adhoc #99).
    m_quickAddClaudeModel->setMaxVisibleItems(30);
    auto refreshQuickAddModelPicker = [this]() {
        if (!m_quickAddAgentProvider || !m_quickAddClaudeModel)
            return;
        const QString provider = m_quickAddAgentProvider->currentData().toString();
        const QSignalBlocker block(m_quickAddClaudeModel);
        if (provider == QLatin1String("claude-code")) {
            populateClaudeModelCombo(m_quickAddClaudeModel);
            m_quickAddClaudeModel->setProperty("claudeModelCombo", true);
            m_quickAddClaudeModel->setToolTip(
                "Claude model the Claude Code agent runs as (passed to the CLI as --model).");
            selectModelComboValue(
                m_quickAddClaudeModel,
                QSettings().value(kClaudeCodeModelSetting).toString().trimmed());
            applyLiveClaudeModelsToCombos();
        } else if (agentIsCodexProvider(provider)) {
            populateCodexModelCombo(m_quickAddClaudeModel);
            m_quickAddClaudeModel->setProperty("claudeModelCombo", false);
            m_quickAddClaudeModel->setToolTip(
                "Codex model passed to the Codex CLI.");
            selectModelComboValue(
                m_quickAddClaudeModel,
                codexChatGptModelId(
                    QSettings().value(kCodexModelSetting).toString().trimmed()));
        } else {
            m_quickAddClaudeModel->setProperty("claudeModelCombo", false);
        }
    };
    m_quickAddClaudeModel->view()->installEventFilter(this);
    refreshQuickAddModelPicker();
    auto persistQuickAddModel = [this]() {
        // A Codex model change can change the effort ladder itself (adhoc #38).
        refreshQuickAddSpeedSelector();
        if (!m_quickAddAgentProvider || !m_quickAddClaudeModel)
            return;
        const QString provider = m_quickAddAgentProvider->currentData().toString();
        const QString model = selectedModelComboValue(m_quickAddClaudeModel);
        if (provider == QLatin1String("claude-code")) {
            QSettings().setValue(kClaudeCodeModelSetting, model);
        } else if (agentIsCodexProvider(provider)) {
            const QString safeModel = codexChatGptModelId(model);
            QSettings().setValue(kCodexModelSetting, safeModel);
            if (m_codexModelEdit)
                m_codexModelEdit->setText(safeModel);
        }
    };
    connect(m_quickAddClaudeModel, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [persistQuickAddModel](int) { persistQuickAddModel(); });
    connect(m_quickAddClaudeModel, &QComboBox::currentTextChanged, this,
            [persistQuickAddModel](const QString &) { persistQuickAddModel(); });
    // Permission/sandbox mode for both structured CLI integrations. Claude maps
    // this to its skip-permissions switch; Codex app-server maps every option to
    // a distinct approval policy and sandbox, including interactive requests.
    m_quickAddModeSelector = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddModeSelector->setObjectName("quickAddModeSelector");
    m_quickAddModeSelector->addItem(kAgentAskModeLabel, false);
    m_quickAddModeSelector->addItem(QStringLiteral("Edit"), false);
    m_quickAddModeSelector->addItem(QStringLiteral("Plan"), false);
    m_quickAddModeSelector->addItem(kClaudeAutoModeLabel, true);
    m_quickAddModeSelector->setMaxVisibleItems(30);
    m_quickAddModeSelector->setToolTip(
        "How much freedom the agent has to make changes without asking first.");
    {
        const QString savedMode =
            QSettings().value(kAgentModeSetting).toString().trimmed();
        int idx = savedMode.isEmpty() ? -1 : m_quickAddModeSelector->findText(savedMode);
        if (idx < 0)
            idx = m_quickAddModeSelector->findData(
                QSettings().value(kClaudeAutoModeSetting, true).toBool());
        m_quickAddModeSelector->setCurrentIndex(
            idx >= 0 ? idx : m_quickAddModeSelector->count() - 1);
    }
    connect(m_quickAddModeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                QSettings().setValue(kClaudeAutoModeSetting,
                                     m_quickAddModeSelector->currentData().toBool());
                QSettings().setValue(kAgentModeSetting,
                                     m_quickAddModeSelector->currentText());
            });
    // Speed (reasoning effort) beside the mode selector (adhoc #38): the same
    // setting the "/" popup's effort dots write, promoted to the composer so the
    // choice is visible where prompts are launched. The item list is per
    // provider — Codex reports supportedReasoningEfforts per model, the `claude`
    // CLI is probed for what it accepts — so filling it lives in
    // refreshQuickAddSpeedSelector() and re-runs whenever either changes.
    m_quickAddSpeedSelector = new FullPopupComboBox; // no scroll arrows (issue #348)
    m_quickAddSpeedSelector->setObjectName("quickAddSpeedSelector");
    m_quickAddSpeedSelector->setMaxVisibleItems(30);
    m_quickAddSpeedSelector->setToolTip(
        "Speed: how hard the model thinks about each turn (the CLI's reasoning "
        "effort). Higher is slower and more thorough.");
    connect(m_quickAddSpeedSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                const QString level =
                    m_quickAddSpeedSelector->currentData().toString();
                if (level.isEmpty())
                    return;
                QSettings().setValue(kClaudeEffortSetting, level);
                // The "/" popup shows the same setting; keep it truthful if it
                // happens to be open.
                if (m_slashActionsPopup && m_slashActionsPopup->isVisible())
                    populateSlashActionsList();
            });
    refreshQuickAddSpeedSelector();
    // Ask the installed CLI what it actually accepts; the picker repopulates
    // when the answer lands.
    refreshClaudeEffortLevels();
    m_quickAddCreatePr = new QCheckBox("Create PR");
    m_quickAddCreatePr->setToolTip(
        "When quick-add assigns an agent, create a pull request from its patch.");
    // Not shown in the controls row (kept out of the prompt-box chrome); it stays
    // wired up and defaults to checked so quick-add agents still open a PR.
    m_quickAddCreatePr->setVisible(false);
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

    // Mic: dictate the prompt with the locally-installed whisper.cpp. Shown
    // greyed-out until whisper.cpp is downloaded from Settings, then enabled
    // (updateVoiceInputButton()).
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
    // The "YOLO" (adhoc #12) and "Task" (adhoc #18) toggles that used to sit
    // beside the Auto checkbox are gone from the composer (adhoc #120): the
    // prompt bar keeps only the controls that describe the prompt itself. Every
    // prompted run now takes the defaults those toggles carried — no unattended
    // auto-merge, and an organization task opened for the run — see
    // startAgentForIssue()/startAdHocAgentForRepo() in MainWindowAgents.cpp.
    m_quickAddCreatePr->setChecked(true);
    m_quickAddCreatePr->setEnabled(true);
    m_quickAddAgentProvider->setEnabled(true);
    // The provider dropdown now carries a "Manual (create issue)" choice (adhoc
    // #29) in place of the old Agent / Create-issue checkboxes. Picking it files
    // an issue rather than running an agent, so the model/mode pickers — which
    // only apply to the two CLI-backed agent providers (Claude Code gets its
    // Claude model list, Codex its OpenAI list) — hide.
    auto syncQuickAddAgentControls = [this]() {
        const QString provider = m_quickAddAgentProvider->currentData().toString();
        const bool claudeCode = provider == QLatin1String("claude-code");
        const bool codex = agentIsCodexProvider(provider);
        m_quickAddClaudeModel->setVisible(claudeCode || codex);
        m_quickAddModeSelector->setVisible(claudeCode || codex);
        if (m_quickAddSpeedSelector) {
            m_quickAddSpeedSelector->setVisible(claudeCode || codex);
            // Codex's ladder is per model, so the items themselves change with
            // the provider — not just whether the picker is shown.
            refreshQuickAddSpeedSelector();
        }
    };
    connect(m_quickAddAgentProvider, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, syncQuickAddAgentControls, refreshQuickAddModelPicker](int) {
                QSettings().setValue(
                    kQuickAddAgentProviderSetting,
                    m_quickAddAgentProvider->currentData().toString());
                refreshQuickAddModelPicker();
                syncQuickAddAgentControls();
            });

    // Slash-actions button (adhoc #116): a small bordered "/" box, like the
    // Claude Code extension's, that opens the filterable actions popup —
    // Context/Model quick actions plus the CLI's own slash commands, pulled
    // live from `claude` the first time the popup opens.
    m_quickAddSlashButton = new QPushButton(QStringLiteral("/"));
    m_quickAddSlashButton->setObjectName("quickAddSlashButton");
    m_quickAddSlashButton->setCursor(Qt::PointingHandCursor);
    m_quickAddSlashButton->setFixedSize(22, 22);
    m_quickAddSlashButton->setToolTip(
        "Agent commands and quick actions");
    connect(m_quickAddSlashButton, &QPushButton::clicked, this,
            &MainWindow::openQuickAddSlashActions);

    // Small send button inside the prompt frame: a paper-airplane icon with a
    // "new" label (adhoc #28). This is the quick-add "start a new agent / file a
    // new issue" send path, kept visually distinct from the "add" button that
    // follows up on the agent already open above. A touch bigger than the old
    // icon-only square so the label reads clearly.
    m_quickAddSendButton = new QPushButton(QStringLiteral("new"));
    m_quickAddSendButton->setObjectName("quickAddSendIcon");
    m_quickAddSendButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddSendButton, "paper-airplane", 17);
    // Fixed width, but stretch vertically (adhoc #115): the two send buttons now
    // form a full-height column down the right edge of the prompt frame, so the
    // prompt box is exactly as tall as the stacked add/new buttons.
    m_quickAddSendButton->setFixedWidth(58);
    m_quickAddSendButton->setMinimumHeight(28);
    m_quickAddSendButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(m_quickAddSendButton, &QPushButton::clicked, this,
            &MainWindow::quickAddIssue);
    // No corner glyph on the button any more (adhoc #120): the little green "⏎"
    // badge (adhoc #89) that rode the top-right corner of "new" while Enter
    // targeted it is gone. The button's own green outline, applied by
    // updateQuickAddEnterTarget(), still marks which send Enter activates.

    // Second paper airplane, rotated to point straight up, stacked above the
    // regular send icon (adhoc #99): sends the typed prompt as a follow-up
    // message to the agent session currently open above, instead of the
    // quick-add issue/new-agent flow.
    m_quickAddSendToAgentButton = new QPushButton(QStringLiteral("add"));
    m_quickAddSendToAgentButton->setObjectName("quickAddSendIcon");
    m_quickAddSendToAgentButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddSendToAgentButton, "paper-airplane", 17, -45.0);
    m_quickAddSendToAgentButton->setFixedWidth(58);
    m_quickAddSendToAgentButton->setMinimumHeight(28);
    m_quickAddSendToAgentButton->setSizePolicy(QSizePolicy::Fixed,
                                               QSizePolicy::Expanding);
    connect(m_quickAddSendToAgentButton, &QPushButton::clicked, this, [this] {
        if (!m_issueQuickAdd)
            return;
        const QString typed = m_issueQuickAdd->toPlainText().trimmed();
        if (m_selectedAgentSessionId < 0) {
            logSystem(QStringLiteral(
                "No agent open above to send that to \xE2\x80\x94 open one first."));
            return;
        }
        if (typed.isEmpty() && m_quickAddImages.isEmpty()) {
            // Nothing typed and nothing attached: just resume the open session,
            // the same thing the old per-session Continue button did (adhoc
            // #178) — but honoring the composer's provider/model/mode
            // dropdowns first, exactly like the follow-up path below, so "add"
            // continues with the model currently selected instead of whatever
            // the session last ran with (adhoc #372).
            applyComposerSelectionToAgentSession(m_selectedAgentSessionId);
            continueSelectedAgentSession();
            return;
        }
        // Fold any attached images into the follow-up the same way the new-agent
        // path does (issue #79): one "Attached image: <path>" line per file, so
        // the agent open above actually receives the pictures the user attached
        // rather than the bare text (adhoc #28).
        QString prompt = typed;
        for (const QString &img : m_quickAddImages) {
            if (!prompt.isEmpty() && !prompt.endsWith(QLatin1Char('\n')))
                prompt += QLatin1Char('\n');
            prompt += QStringLiteral("Attached image: %1").arg(img);
        }
        if (!typed.isEmpty())
            recordQuickAddHistory(typed);
        m_issueQuickAdd->clear();
        clearQuickAddImages();
        sendPromptToSelectedAgent(prompt);
    });

    // Third button, stacked above "add" and "new" (adhoc #42): "task" doesn't
    // send the typed prompt at all — it starts an agent wired to the remote MCP
    // server configured on the website, so the agent picks its own work off the
    // organization's shared task list and reports back through the same tools.
    // Anything typed in the box rides along as extra guidance for that run.
    // Labelled "task" rather than "genie" (adhoc #120), after what it actually
    // does; the widget/QSS name stays the genie one the rest of the run plumbing
    // (AgentSession::genie, startGenieAgent) is keyed to.
    m_quickAddGenieButton = new QPushButton(QStringLiteral("task"));
    m_quickAddGenieButton->setObjectName("quickAddGenieButton");
    m_quickAddGenieButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_quickAddGenieButton, "list-unordered", 15);
    m_quickAddGenieButton->setFixedWidth(58);
    m_quickAddGenieButton->setMinimumHeight(24);
    m_quickAddGenieButton->setSizePolicy(QSizePolicy::Fixed,
                                         QSizePolicy::Expanding);
    m_quickAddGenieButton->setToolTip(
        QString::fromUtf8("Task \xE2\x80\x94 start a running agent session that "
                          "picks its own work off the organization's shared task "
                          "list. No setup: the first press mints this node's own "
                          "task credential from the account you are signed in "
                          "as."));
    connect(m_quickAddGenieButton, &QPushButton::clicked, this,
            &MainWindow::startGenieAgent);

    // Vertically Expanding (not Fixed) so the text area absorbs any spare height
    // in the prompt frame. With the fixed-height bottom bar below it, that keeps
    // the toolbar pinned flush to the foot of the frame instead of floating up
    // with a gap when the frame is taller than the two rows' combined hint
    // (adhoc: the interface items must stay fixed to the bottom, not drift with
    // the text).
    m_issueQuickAdd->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Three buttons stacked in a full-height column down the prompt's right edge
    // (adhoc #115): each stretches to take its share of the frame height, so the
    // text area to their left ends flush against them and the whole prompt box is
    // just as tall as the task/add/new stack. "task" sits on top (adhoc #42).
    auto *sendColumn = new QVBoxLayout;
    sendColumn->setContentsMargins(0, 0, 0, 0);
    sendColumn->setSpacing(2);
    sendColumn->addWidget(m_quickAddGenieButton, 1);
    sendColumn->addWidget(m_quickAddSendToAgentButton, 1);
    sendColumn->addWidget(m_quickAddSendButton, 1);
    // Enter targets "new" until an agent session is opened above.
    updateQuickAddEnterTarget();

    // Agent hand-off controls (adhoc #99): the provider/model/mode dropdowns,
    // grouped as one unit in the middle of the bottom bar. The provider dropdown
    // now also carries "Manual (create issue)" (adhoc #29). No border/frame
    // around them any more (adhoc #111 removed the pill outline) — they just sit
    // inline in the bar.
    auto *agentBox = new QWidget;
    agentBox->setObjectName("quickAddAgentBox");
    auto *agentBoxRow = new QHBoxLayout(agentBox);
    agentBoxRow->setContentsMargins(6, 1, 4, 1);
    agentBoxRow->setSpacing(2);
    agentBoxRow->addWidget(m_quickAddAgentProvider);
    agentBoxRow->addWidget(m_quickAddClaudeModel);
    agentBoxRow->addWidget(m_quickAddModeSelector);
    agentBoxRow->addWidget(m_quickAddSpeedSelector);
    // Apply initial visibility only after the controls have their real parent.
    // Showing a parentless combo and then reparenting it can leave it hidden,
    // which made the Claude model picker depend on event-loop timing at startup.
    syncQuickAddAgentControls();

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
    bottomBar->setContentsMargins(8, 4, 6, 0);
    bottomBar->setSpacing(5);
    bottomBar->addWidget(m_quickAddImageButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddMicButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_voiceLevelMeter, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddAttachStrip, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_quickAddVoiceAutoSubmit, 0, Qt::AlignBottom);
    bottomBar->addStretch(1);
    // The "/" actions box sits immediately left of the agent box (adhoc #116),
    // matching where the Claude Code extension keeps its actions menu.
    bottomBar->addWidget(m_quickAddSlashButton, 0, Qt::AlignBottom);
    bottomBar->addWidget(agentBox, 0, Qt::AlignBottom);
    bottomBar->addStretch(1);
    bottomBar->addWidget(m_quickAddCharCount, 0, Qt::AlignBottom);
    // The tiny Codex + Claude usage gauges sit immediately left of the send
    // icons (adhoc #47), moved down from the top bar so the current 5h/weekly
    // utilisation is visible right where prompts are launched.
    bottomBar->addWidget(m_navCodexUsage, 0, Qt::AlignBottom);
    bottomBar->addWidget(m_navTokenUsage, 0, Qt::AlignBottom);
    // The send column (add/new) no longer lives in this toolbar (adhoc #115) — it
    // moved out to a full-height column down the right edge of the prompt frame.

    auto *bottomBarHost = new QWidget;
    bottomBarHost->setObjectName("quickAddBottomBarHost");
    bottomBarHost->setLayout(bottomBar);
    bottomBarHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // The controls sit in a scroll area only so the row's own minimum width does
    // not force the whole window wider (the scroll area keeps minimumWidth 0).
    // The horizontal scrollbar is switched off entirely (adhoc): it used to
    // appear whenever the row was a touch too wide and, by stealing height from
    // the fixed-height viewport, shoved every control up out of alignment. With
    // widgetResizable the host now tracks the viewport width, so the stretches
    // keep the send column pinned right and the controls stay put; on a very
    // narrow window the row clips instead of scrolling.
    auto *bottomBarScroll = new QScrollArea;
    bottomBarScroll->setObjectName("quickAddBottomBarScroll");
    bottomBarScroll->setWidget(bottomBarHost);
    bottomBarScroll->setWidgetResizable(true);
    bottomBarScroll->setFrameShape(QFrame::NoFrame);
    bottomBarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomBarScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomBarScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    bottomBarScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bottomBarScroll->setMinimumWidth(0);
    // Fit the strip to its tallest control — the two stacked send icons make the
    // row 62px, not 56px — so the viewport is never shorter than the host. A
    // too-short viewport made widgetResizable keep the host at its taller minimum
    // and scroll it vertically, pushing the toolbar controls up out of view
    // (adhoc #19: "ui objects go below the line"). Deriving the height from the
    // content keeps it correct if the controls ever change, and with the viewport
    // now tall enough the toolbar is fixed — only the text area above scrolls.
    bottomBarScroll->setFixedHeight(bottomBarHost->sizeHint().height());

    // Prompt wrapper: the border lives on this frame; the text edit sits on
    // top with the bottom bar nested below it inside the same box, so the
    // controls read as an overlay along the foot of the prompt input rather
    // than a separate strip above it.
    auto *promptWrapper = new QFrame;
    promptWrapper->setObjectName("promptWrapper");
    // Horizontal split (adhoc #115): the text area + its bottom toolbar stack in
    // a left column, and the genie/add/new buttons form a full-height column down
    // the right edge. The text entry therefore ends flush against the buttons and
    // the whole box is exactly as tall as the stacked buttons.
    auto *promptLayout = new QHBoxLayout(promptWrapper);
    promptLayout->setContentsMargins(0, 0, 0, 0);
    promptLayout->setSpacing(0);
    auto *promptLeftCol = new QVBoxLayout;
    promptLeftCol->setContentsMargins(0, 0, 0, 0);
    promptLeftCol->setSpacing(0);
    // The editor stretches to fill the freed vertical space (the send column no
    // longer sits below it), and the bottom bar carries its own fixed height, so
    // the border sits right above the text and the controls weld to the foot.
    promptLeftCol->addWidget(m_issueQuickAdd, 1);
    promptLeftCol->addWidget(bottomBarScroll, 0);
    promptLayout->addLayout(promptLeftCol, 1);
    promptLayout->addLayout(sendColumn, 0);

    // The prompt frame is the right half of the footer on its own (adhoc #60):
    // no surrounding card chrome and no "Agents:" status strip above it (that
    // fleet state already lives on the window-chrome dot matrix beside the Agents
    // nav button), so the bordered box reads exactly like the log and Background
    // panels and fills the dock top to bottom and edge to edge.
    promptWrapper->setMinimumWidth(0);
    promptWrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // A scrollable strip below the quick-add bar: the always-on live log. It
    // fills as much height as the dock row allows (matching the prompt card
    // beside it) and streams every network/update line, oldest at top, newest
    // at bottom — the scrollbar lets you scroll back through history to search
    // it instead of only ever seeing the latest line (adhoc #211).
    m_footerUpdateLog = new QTextEdit;
    m_footerUpdateLog->setObjectName("footerUpdateLog");
    m_footerUpdateLog->setReadOnly(true);
    m_footerUpdateLog->setFrameShape(QFrame::NoFrame);
    // Tight paragraph metrics so the rich-text strip still reads as a dense log
    // tail rather than a spaced-out document.
    m_footerUpdateLog->document()->setDocumentMargin(0);
    // Don't wrap (adhoc #133): a long line clips at the right edge instead of
    // reflowing onto extra rows, so every entry stays one row tall and the strip
    // reads like a dense log tail. The full text is still reachable — hovering a
    // line shows it in a tooltip and clicking opens the full Log view at it.
    m_footerUpdateLog->setLineWrapMode(QTextEdit::NoWrap);
    m_footerUpdateLog->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_footerUpdateLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_footerUpdateLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_footerUpdateLog->setMinimumWidth(0);
    m_footerUpdateLog->setToolTip(
        "Live log \xE2\x80\x94 click a line to open the full Log at it; scroll up "
        "to search back through recent history.");
    // Per-line hover tooltips (the full, untruncated line) and click-to-open are
    // driven from MainWindow::eventFilter on the viewport; the hand cursor hints
    // that the lines are clickable. Mouse tracking is left off deliberately so a
    // plain hover doesn't fire QPlainTextEdit's own mouse-move handler, which
    // would otherwise flip the cursor back to an I-beam over the text.
    m_footerUpdateLog->viewport()->setCursor(Qt::PointingHandCursor);
    m_footerUpdateLog->viewport()->installEventFilter(this);
    // The live buffer is bounded the same way the seed below is, so it can't
    // grow without limit over a long-running session. QTextEdit has no
    // setMaximumBlockCount, so setFooterUpdateLine() drops the oldest block
    // itself once the strip is full.
    styleFooterUpdateLog();
    // Seed the always-on strip with recent history (or a ready placeholder) so
    // it's already scrollable on first paint; logSystem() then streams every new
    // event onto it. Keep the full dated lines so timestamps show.
    if (!m_networkLog.isEmpty()) {
        const int from = qMax(0, m_networkLog.size() - kFooterLogSeedLines);
        // Render seed history with the same colored badges (and favicons) as live
        // lines instead of raw plain text (adhoc #19), but as a single setHtml()
        // pass — 300 individual appends would re-lay out the document each time,
        // on the startup path.
        QString seedHtml;
        QStringList seedLines; // the raw lines, one per rendered block
        for (int i = from; i < m_networkLog.size(); ++i) {
            const QString clean = m_networkLog.at(i).trimmed();
            if (clean.isEmpty())
                continue;
            seedHtml += QStringLiteral("<div>%1</div>").arg(footerLogLineHtml(clean));
            seedLines << clean;
        }
        m_footerUpdateLog->setHtml(seedHtml);
        // Stamp each block with its raw line so hover tooltips and click-to-open
        // work on seeded history exactly as they do on live lines.
        int seedIndex = 0;
        for (QTextBlock b = m_footerUpdateLog->document()->firstBlock();
             b.isValid() && seedIndex < seedLines.size(); b = b.next(), ++seedIndex)
            b.setUserData(new FooterLogLineData(seedLines.at(seedIndex)));
        m_footerUpdateLog->verticalScrollBar()->setValue(
            m_footerUpdateLog->verticalScrollBar()->maximum());
    } else {
        m_footerUpdateLog->setPlainText(QStringLiteral("ForkMesh ready"));
    }

    m_footerUpdateLog->installEventFilter(this);

    // Tiny pause-scroll toggle floating over the strip's bottom-right corner
    // (adhoc #92). It rides on top of the log instead of taking a layout row so
    // the fixed-height footer doesn't lose a line of history to it. The log
    // otherwise always follows the newest line; this parks that follow.
    m_footerLogPauseButton = new QPushButton(m_footerUpdateLog);
    m_footerLogPauseButton->setObjectName("footerLogPauseButton");
    m_footerLogPauseButton->setCheckable(true);
    m_footerLogPauseButton->setFocusPolicy(Qt::NoFocus);
    m_footerLogPauseButton->setCursor(Qt::PointingHandCursor);
    m_footerLogPauseButton->setFixedSize(18, 14);
    m_footerLogPauseButton->setIconSize(QSize(8, 8));
    // The strip's canvas is forced white in both themes (styleFooterUpdateLog),
    // so the toggle carries its own light-on-white look rather than a theme rule.
    m_footerLogPauseButton->setStyleSheet(QStringLiteral(
        "QPushButton#footerLogPauseButton{background:#f6f8fa;border:1px solid "
        "#d0d7de;border-radius:4px;color:#57606a;font-size:8px;padding:0;}"
        "QPushButton#footerLogPauseButton:hover{background:#eaeef2;color:#1f2328;}"
        "QPushButton#footerLogPauseButton:checked{background:#ddf4e4;"
        "border-color:#1a7f37;color:#1a7f37;}"));
    connect(m_footerLogPauseButton, &QPushButton::toggled, this,
            [this](bool paused) {
                m_footerLogScrollPaused = paused;
                updateFooterLogPauseButton();
                // Un-pausing catches up immediately: the point of resuming is to
                // be back on the newest line, not wherever the view was parked.
                if (!paused && m_footerUpdateLog)
                    if (QScrollBar *bar = m_footerUpdateLog->verticalScrollBar())
                        bar->setValue(bar->maximum());
            });
    updateFooterLogPauseButton();
    positionFooterLogPauseButton();
    // The strip's own resize event doesn't fire when the scrollbar appears or
    // goes away (that only changes the viewport), so re-park the toggle whenever
    // the scroll range flips between "fits" and "scrolls".
    connect(m_footerUpdateLog->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this] { positionFooterLogPauseButton(); });

    // Background work is visible without taking over the app: this narrow strip
    // sits exactly between the live log and the agent prompt and lists one
    // spinner plus one-word tag per kind of job in flight. Five tags fit; past
    // that the list scrolls (adhoc #421). The panel is permanent (adhoc #419):
    // it holds its slot in the footer and reads "idle" when nothing is running,
    // so it never appears/disappears under the pointer and the row it would use
    // is never borrowed by the log or the prompt.
    m_backgroundQueue = new QFrame;
    m_backgroundQueue->setObjectName("backgroundTaskQueue");
    m_backgroundQueue->setFrameShape(QFrame::StyledPanel);
    m_backgroundQueue->setFixedWidth(132);
    auto *backgroundLayout = new QVBoxLayout(m_backgroundQueue);
    backgroundLayout->setContentsMargins(9, 6, 6, 6);
    backgroundLayout->setSpacing(4);
    m_backgroundQueueTitle = new QLabel(QStringLiteral("Background"));
    m_backgroundQueueTitle->setObjectName("backgroundTaskQueueTitle");
    QFont backgroundTitleFont = m_backgroundQueueTitle->font();
    backgroundTitleFont.setBold(true);
    backgroundTitleFont.setPointSizeF(
        qMax(8.0, backgroundTitleFont.pointSizeF() - 1.0));
    m_backgroundQueueTitle->setFont(backgroundTitleFont);
    backgroundLayout->addWidget(m_backgroundQueueTitle);
    m_backgroundQueueRowsHost = new QWidget;
    m_backgroundQueueRowsLayout =
        new QVBoxLayout(m_backgroundQueueRowsHost);
    m_backgroundQueueRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_backgroundQueueRowsLayout->setSpacing(kBackgroundTaskRowSpacing);
    // Placeholder for the (common) case of nothing in flight: an always-visible
    // panel with an empty body would read as broken, and the dimmed word keeps
    // the list's height stable as rows come and go.
    m_backgroundQueueIdleLabel = new QLabel(QStringLiteral("idle"));
    m_backgroundQueueIdleLabel->setObjectName("backgroundTaskIdle");
    m_backgroundQueueIdleLabel->setToolTip(
        QStringLiteral("No background work in flight"));
    m_backgroundQueueRowsLayout->addWidget(m_backgroundQueueIdleLabel);
    m_backgroundQueueRowsLayout->addStretch(1);
    m_backgroundQueueScroll = new QScrollArea;
    m_backgroundQueueScroll->setObjectName("backgroundTaskQueueScroll");
    m_backgroundQueueScroll->setFrameShape(QFrame::NoFrame);
    m_backgroundQueueScroll->setWidgetResizable(true);
    m_backgroundQueueScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_backgroundQueueScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_backgroundQueueScroll->setWidget(m_backgroundQueueRowsHost);
    // Height for exactly kBackgroundTaskVisibleRows rows: the sixth kind of work
    // pushes the list into its scrollbar instead of stretching the footer.
    QFont backgroundRowFont = m_backgroundQueue->font();
    backgroundRowFont.setPointSizeF(
        qMax(7.5, backgroundRowFont.pointSizeF() - 1.0));
    m_backgroundTaskRowHeight = QFontMetrics(backgroundRowFont).height() + 2;
    m_backgroundQueueIdleLabel->setFont(backgroundRowFont);
    m_backgroundQueueIdleLabel->setFixedHeight(m_backgroundTaskRowHeight);
    m_backgroundQueueIdleLabel->setStyleSheet(
        QStringLiteral("color:#6e7681;"));
    m_backgroundQueueScroll->setMaximumHeight(
        kBackgroundTaskVisibleRows * m_backgroundTaskRowHeight +
        (kBackgroundTaskVisibleRows - 1) * kBackgroundTaskRowSpacing);
    backgroundLayout->addWidget(m_backgroundQueueScroll, 1);

    // Every announcement in the process lands here. The hop through
    // invokeMethod() is what lets tickets be opened off the GUI thread (mirror
    // scans, contribution snapshots) while all queue state stays on one thread;
    // posted events are dropped if the window dies first.
    forkmesh::BackgroundActivity::setListener(
        [this](quint64 id, const QString &kind, const QString &detail,
               forkmesh::ActionTelemetry::Execution execution, bool started) {
            const bool backgrounded =
                execution != forkmesh::ActionTelemetry::Execution::UiBlocking;
            QMetaObject::invokeMethod(
                this,
                [this, id, kind, detail, backgrounded, started] {
                    noteBackgroundActivity(id, kind, detail, backgrounded,
                                           started);
                },
                Qt::QueuedConnection);
        });

    // The live log and Background queue form one left-hand region, butted
    // together and ending where the prompt half begins. Each compact panel has
    // the same rounded green border language as the prompt.
    auto *logPanel = new QFrame;
    logPanel->setObjectName(QStringLiteral("footerLogPanel"));
    auto *logPanelLayout = new QVBoxLayout(logPanel);
    logPanelLayout->setContentsMargins(1, 1, 1, 1);
    logPanelLayout->setSpacing(2);
    // Errors and successes land at the very top of the mini-log, pushed against
    // its first line rather than floating up in the window chrome: the toast and
    // the log lines it summarises are read together. It is hidden by default and
    // only borrows height from the log while a message is up — the footer's own
    // height is fixed, so nothing else in the window moves (adhoc #14).
    // buildBreadcrumb() runs before this dock is built, so the pill already exists.
    logPanelLayout->addWidget(m_topMessageContainer, 0, Qt::AlignTop);
    logPanelLayout->addWidget(m_footerUpdateLog, 1);

    auto *leftRegion = new QWidget;
    leftRegion->setObjectName(QStringLiteral("footerLeftRegion"));
    auto *leftRegionLayout = new QHBoxLayout(leftRegion);
    leftRegionLayout->setContentsMargins(0, 0, 0, 0);
    // The log and the Background panel are separated by exactly the gap the row
    // uses everywhere else — the same 8px as the dock's own margins and the gap
    // to the prompt (adhoc #92). Butting them together (adhoc #84) left their two
    // rounded borders touching as one 2px line that pinched apart at the corners.
    leftRegionLayout->setSpacing(8);
    leftRegionLayout->addWidget(logPanel, 1);
    leftRegionLayout->addWidget(m_backgroundQueue, 0);

    // Horizontal split: bordered log + Background, then prompt. The hairline
    // rule that used to sit between the two halves is gone (adhoc #84): every
    // panel in the row already carries its own border, so the extra line was one
    // divider too many.
    auto *dockRow = new QHBoxLayout(dock);
    dockRow->setContentsMargins(8, 8, 8, 8);
    dockRow->setSpacing(8);
    dockRow->addWidget(leftRegion, 1);
    dockRow->addWidget(promptWrapper, 1);

    // Pin the footer to just the compact prompt's height (adhoc #107): the dock
    // margins plus the six-line prompt and its controls. With the card padding
    // and the "Agents:" strip gone (adhoc #60) the prompt frame is the tallest
    // thing in the row, so the log panel beside it is exactly as tall as the
    // prompt and nothing reflows.
    dock->setFixedHeight(promptWrapper->sizeHint().height() + 16);

    // Enter sends (Shift+Enter inserts a newline) — handled in the event filter
    // since QPlainTextEdit has no returnPressed signal.
    updateVoiceInputButton();
    return dock;
}

// One-word tag for the strip: callers may hand over a phrase, the row shows the
// first word ("git", "net", "fork" …) and keeps the rest for the tooltip.
QString MainWindow::backgroundTaskWord(const QString &kind)
{
    QString word;
    for (const QChar ch : kind.simplified()) {
        if (ch.isSpace())
            break;
        if (ch.isLetterOrNumber())
            word.append(ch.toLower());
    }
    if (word.isEmpty())
        word = QStringLiteral("work");
    return word.left(10);
}

quint64 MainWindow::beginBackgroundTask(const QString &kind,
                                        const QString &detail)
{
    // Route even in-window callers through the bus so there is exactly one path
    // into the strip, whoever opened the ticket.
    return forkmesh::BackgroundActivity::begin(kind, detail);
}

void MainWindow::finishBackgroundTask(quint64 id, bool success,
                                      const QString &detail)
{
    forkmesh::BackgroundActivity::end(
        id, success ? QStringLiteral("succeeded") : QStringLiteral("failed"));
    if (!detail.trimmed().isEmpty())
        logSystem(QStringLiteral("Background: %1").arg(detail.trimmed()));
    if (!success && !detail.trimmed().isEmpty())
        flashMessage(detail.trimmed(), true);
}

// A finished run of one kind of work goes into a pending tally rather than
// straight into the log (adhoc #419): the hot paths retire hundreds of tickets a
// minute and one line each would bury every other event (and rewrite the log
// file that often). flushBackgroundOutcomes() turns each tally into a single
// entry — ✓ for async/worker execution, red ✕ only for work explicitly marked
// as running synchronously on the GUI thread.
void MainWindow::recordBackgroundOutcome(const QString &word, qint64 elapsedMs,
                                         const QString &detail, qint64 now,
                                         bool backgrounded)
{
    QHash<QString, BackgroundOutcomeTally> &bucket =
        backgrounded ? m_backgroundTaskDone : m_backgroundTaskUiBlocking;
    BackgroundOutcomeTally &tally = bucket[word];
    if (tally.runs == 0)
        tally.firstAt = now;
    ++tally.runs;
    tally.longestMs = qMax(tally.longestMs, elapsedMs);
    const QString note = detail.trimmed();
    if (!note.isEmpty())
        tally.detail = note;
}

// Emit the tallies that have been open long enough to be worth summarising (or
// all of them, when the strip is about to go quiet).
void MainWindow::flushBackgroundOutcomes(bool force)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (QHash<QString, BackgroundOutcomeTally> *bucket :
         {&m_backgroundTaskDone, &m_backgroundTaskUiBlocking}) {
        const bool backgrounded = bucket == &m_backgroundTaskDone;
        for (auto it = bucket->begin(); it != bucket->end();) {
            if (!force && now - it->firstAt < kBackgroundTaskFastFlushMs) {
                ++it;
                continue;
            }
            logSystem(forkmesh::backgroundOutcomeLine(
                it.key(), it->runs, it->longestMs, it->detail,
                backgrounded));
            it = bucket->erase(it);
        }
    }
}

// Ticket bookkeeping. Rows are *not* touched here: a job that finishes inside
// kBackgroundTaskShowAfterMs must never create a widget, so the sweep below owns
// what is on screen and this only maintains the counts it reads.
void MainWindow::noteBackgroundActivity(quint64 id, const QString &kind,
                                        const QString &detail,
                                        bool backgrounded, bool started)
{
    if (!m_backgroundQueue || !m_backgroundQueueRowsLayout)
        return;
    if (started) {
        const QString word = backgroundTaskWord(kind);
        m_backgroundTaskWords.insert(id, word);
        if (!backgrounded)
            m_backgroundTaskHadUiBlocking.insert(word, true);
        const int count = m_backgroundTaskCounts.value(word) + 1;
        m_backgroundTaskCounts.insert(word, count);
        if (count == 1)
            m_backgroundTaskSince.insert(word, QDateTime::currentMSecsSinceEpoch());
        const QString note = detail.trimmed();
        if (!note.isEmpty())
            m_backgroundTaskDetails.insert(word, note);
    } else {
        const QString word = m_backgroundTaskWords.take(id);
        if (word.isEmpty())
            return;
        const int count = m_backgroundTaskCounts.value(word) - 1;
        if (count > 0) {
            m_backgroundTaskCounts.insert(word, count);
        } else {
            // Last ticket of this kind: hand the run to the log's ✓ / ✕ tally.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            recordBackgroundOutcome(word,
                                    now - m_backgroundTaskSince.value(word, now),
                                    m_backgroundTaskDetails.value(word), now,
                                    !m_backgroundTaskHadUiBlocking.value(word));
            m_backgroundTaskCounts.remove(word);
            m_backgroundTaskSince.remove(word);
            m_backgroundTaskDetails.remove(word);
            m_backgroundTaskHadUiBlocking.remove(word);
        }
    }
    if (!m_backgroundTaskSpinTimer) {
        m_backgroundTaskSpinTimer = new QTimer(this);
        m_backgroundTaskSpinTimer->setInterval(90);
        connect(m_backgroundTaskSpinTimer, &QTimer::timeout, this,
                &MainWindow::tickBackgroundQueue);
    }
    if (!m_backgroundTaskSpinTimer->isActive()) {
        m_backgroundTaskIdleTicks = 0;
        m_backgroundTaskSpinTimer->start();
    }
}

// Advance the spinner glyphs and reconcile the visible rows with the open
// tickets. Cheap: at most a handful of kinds are ever in flight at once.
void MainWindow::tickBackgroundQueue()
{
    static const QStringList frames{
        QString::fromUtf8("\xE2\xA0\x8B"), QString::fromUtf8("\xE2\xA0\x99"),
        QString::fromUtf8("\xE2\xA0\xB9"), QString::fromUtf8("\xE2\xA0\xB8"),
        QString::fromUtf8("\xE2\xA0\xBC"), QString::fromUtf8("\xE2\xA0\xB4"),
        QString::fromUtf8("\xE2\xA0\xA6"), QString::fromUtf8("\xE2\xA0\xA7"),
        QString::fromUtf8("\xE2\xA0\x87"), QString::fromUtf8("\xE2\xA0\x8F"),
    };
    if (!m_backgroundQueue || !m_backgroundQueueRowsLayout)
        return;
    m_backgroundTaskSpinFrame = (m_backgroundTaskSpinFrame + 1) % frames.size();
    const QString glyph = frames.at(m_backgroundTaskSpinFrame);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Retire rows whose last ticket closed.
    const QStringList shown = m_backgroundTaskRows.keys();
    for (const QString &word : shown) {
        if (m_backgroundTaskCounts.contains(word))
            continue;
        if (QWidget *row = m_backgroundTaskRows.take(word)) {
            // Drop it from the layout now: deleteLater() alone would leave the
            // dead row occupying a slot until the next event-loop pass, and a
            // fresh ticket for the same word would draw a second one beside it.
            m_backgroundQueueRowsLayout->removeWidget(row);
            row->hide();
            row->deleteLater();
        }
        m_backgroundTaskSpinners.remove(word);
        m_backgroundTaskLabels.remove(word);
    }

    // Add or refresh a row per kind that has outlived the show delay.
    for (auto it = m_backgroundTaskCounts.constBegin();
         it != m_backgroundTaskCounts.constEnd(); ++it) {
        const QString &word = it.key();
        if (now - m_backgroundTaskSince.value(word, now) <
            kBackgroundTaskShowAfterMs)
            continue;
        QLabel *spinner = m_backgroundTaskSpinners.value(word);
        QLabel *label = m_backgroundTaskLabels.value(word);
        if (!spinner || !label) {
            auto *row = new QWidget(m_backgroundQueueRowsHost);
            row->setObjectName("backgroundTaskRow");
            row->setFixedHeight(m_backgroundTaskRowHeight);
            auto *layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(6);
            spinner = new QLabel(glyph);
            spinner->setObjectName("backgroundTaskSpinner");
            spinner->setStyleSheet(
                QStringLiteral("color:%1;font-weight:700;")
                    .arg(QString::fromLatin1(Theme::kRunning)));
            spinner->setFixedWidth(14);
            label = new QLabel(word);
            label->setObjectName("backgroundTaskNote");
            QFont noteFont = label->font();
            noteFont.setPointSizeF(qMax(7.5, noteFont.pointSizeF() - 1.0));
            label->setFont(noteFont);
            spinner->setFont(noteFont);
            layout->addWidget(spinner, 0, Qt::AlignVCenter);
            layout->addWidget(label, 1);
            m_backgroundQueueRowsLayout->insertWidget(
                qMax(0, m_backgroundQueueRowsLayout->count() - 1), row);
            m_backgroundTaskRows.insert(word, row);
            m_backgroundTaskSpinners.insert(word, spinner);
            m_backgroundTaskLabels.insert(word, label);
        }
        spinner->setText(glyph);
        const int count = it.value();
        label->setText(count > 1 ? QStringLiteral("%1 %2%3")
                                       .arg(word)
                                       .arg(QChar(0x00D7))
                                       .arg(count)
                                 : word);
        const QString note = m_backgroundTaskDetails.value(word);
        label->setToolTip(note.isEmpty() ? word : note);
    }

    // The panel itself never hides (adhoc #419); the placeholder stands in for
    // the rows while nothing is in flight.
    const int visible = m_backgroundTaskRows.size();
    m_backgroundQueue->show();
    if (m_backgroundQueueIdleLabel)
        m_backgroundQueueIdleLabel->setVisible(visible == 0);
    if (m_backgroundQueueTitle) {
        m_backgroundQueueTitle->setText(
            visible > 0 ? QStringLiteral("Background %1 %2")
                              .arg(QChar(0x00B7))
                              .arg(visible)
                        : QStringLiteral("Background"));
    }

    // Stand the timer down once nothing is running and nothing is drawn, with a
    // grace period so a stream of short jobs doesn't flap it. The pending ✓ / ✕
    // tallies are flushed unconditionally on the way down, since nothing will be
    // ticking to flush them later.
    if (m_backgroundTaskCounts.isEmpty() && visible == 0) {
        const bool standingDown =
            ++m_backgroundTaskIdleTicks >= kBackgroundTaskIdleTicksBeforeStop;
        flushBackgroundOutcomes(standingDown);
        if (standingDown && m_backgroundTaskSpinTimer) {
            m_backgroundTaskSpinTimer->stop();
            m_backgroundTaskIdleTicks = 0;
        }
    } else {
        m_backgroundTaskIdleTicks = 0;
        flushBackgroundOutcomes(false);
    }
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
    // Claude exposes its command catalog through its control protocol. Codex
    // commands are handled by app-server and do not require launching a second
    // probe merely to open this menu.
    if (m_quickAddAgentProvider &&
        m_quickAddAgentProvider->currentData().toString() ==
            QLatin1String("claude-code"))
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
    const bool codexProvider =
        m_quickAddAgentProvider &&
        agentIsCodexProvider(m_quickAddAgentProvider->currentData().toString());
    // What the current provider actually accepts (adhoc #38): shared with the
    // composer's speed picker, which writes the same setting these dots do.
    const QStringList effortLevels = agentEffortLevels();
    QStringList effortLabels;
    for (const QString &level : effortLevels)
        effortLabels << agentEffortLabel(level);
    const QString currentEffort =
        QSettings().value(kClaudeEffortSetting, QStringLiteral("high")).toString();
    int effortIdx = effortLevels.indexOf(currentEffort);
    if (effortIdx < 0) {
        effortIdx = qMax(0, effortLevels.indexOf(QStringLiteral("high")));
        if (codexProvider && effortIdx < effortLevels.size())
            QSettings().setValue(kClaudeEffortSetting,
                                 effortLevels.at(effortIdx));
    }
    const bool modelSectionMatches =
        matches(QStringLiteral("Switch model")) || matches(QStringLiteral("Effort")) ||
        (!codexProvider && matches(QStringLiteral("Thinking"))) ||
        (!codexProvider &&
         matches(QStringLiteral("Switch models when a message is flagged"))) ||
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
                QStringLiteral("Effort (%1)").arg(effortLabels.value(effortIdx)));
            title->setObjectName("slashActionRowLabel");
            rowLayout->addWidget(title, 1);
            auto *dotsLayout = new QHBoxLayout;
            dotsLayout->setSpacing(4);
            for (int i = 0; i < effortLevels.size(); ++i) {
                auto *dot = new QToolButton;
                dot->setObjectName("slashEffortDot");
                dot->setCheckable(true);
                dot->setChecked(i == effortIdx);
                dot->setFixedSize(10, 10);
                dot->setCursor(Qt::PointingHandCursor);
                dot->setProperty("slashKind", QStringLiteral("effortLevel"));
                dot->setProperty("slashValue", effortLevels.at(i));
                dot->installEventFilter(this);
                dotsLayout->addWidget(dot);
                m_slashActionRows.append(dot); // arrow-key reachable, like the rows
            }
            rowLayout->addLayout(dotsLayout);
            m_slashActionsListLayout->addWidget(row);
        }
        if (!codexProvider && matches(QStringLiteral("Thinking")))
            addToggleRow(QStringLiteral("Thinking"), QStringLiteral("toggleThinking"),
                        QSettings().value(kClaudeThinkingSetting, true).toBool());
        if (!codexProvider &&
            matches(QStringLiteral("Switch models when a message is flagged")))
            addToggleRow(QStringLiteral("Switch models when a message is flagged"),
                        QStringLiteral("toggleFallback"),
                        QSettings().value(kClaudeFallbackModelSetting, false).toBool());
        if (matches(QStringLiteral("Account & usage")))
            addRow(QStringLiteral("Account & usage\xE2\x80\xA6"), QString(),
                   QStringLiteral("accountUsage"), QString());
    }

    // --- Commands section: the CLI's own slash commands, pulled live ---
    if (!codexProvider && !m_claudeSlashCommands.isEmpty()) {
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
            const QString provider =
                m_quickAddAgentProvider->currentData().toString();
            if (provider != QLatin1String("claude-code") &&
                !agentIsCodexProvider(provider)) {
                const int idx = m_quickAddAgentProvider->findData(
                    QStringLiteral("claude-code"));
                if (idx >= 0)
                    m_quickAddAgentProvider->setCurrentIndex(idx);
            }
        }
        if (m_quickAddClaudeModel) {
            m_quickAddClaudeModel->setFocus();
            m_quickAddClaudeModel->showPopup();
        }
    } else if (kind == QLatin1String("effortLevel")) {
        QSettings().setValue(kClaudeEffortSetting, value);
        // The composer's speed picker shows the same setting (adhoc #38).
        refreshQuickAddSpeedSelector();
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

// The reasoning-effort ladder the composer's provider actually accepts (adhoc
// #38). Codex publishes supportedReasoningEfforts per model in the app-server
// catalog, so a model that only does low/medium never offers "max"; Claude Code
// is probed for what its installed CLI takes (refreshClaudeEffortLevels), since
// the ladder has grown over releases. Neither known yet => the default ladder.
QStringList MainWindow::agentEffortLevels() const
{
    const QString provider =
        m_quickAddAgentProvider ? m_quickAddAgentProvider->currentData().toString()
                                : QString();
    if (agentIsCodexProvider(provider) && m_quickAddClaudeModel) {
        const QString selectedModel = selectedModelComboValue(m_quickAddClaudeModel);
        const QJsonArray models =
            QJsonDocument::fromJson(
                QSettings().value(kCodexModelsCacheSetting).toByteArray())
                .array();
        for (const QJsonValue &value : models) {
            const QJsonObject model = value.toObject();
            QString id = model.value(QStringLiteral("model")).toString();
            if (id.isEmpty())
                id = model.value(QStringLiteral("id")).toString();
            if (id != selectedModel)
                continue;
            QStringList levels;
            for (const QJsonValue &effortValue :
                 model.value(QStringLiteral("supportedReasoningEfforts")).toArray()) {
                const QString level = effortValue.toObject()
                                          .value(QStringLiteral("reasoningEffort"))
                                          .toString();
                if (!level.isEmpty())
                    levels << level;
            }
            if (!levels.isEmpty())
                return levels;
            break;
        }
        return defaultAgentEffortLevels();
    }
    const QStringList probed =
        QSettings().value(kClaudeEffortLevelsCacheSetting).toStringList();
    return probed.isEmpty() ? defaultAgentEffortLevels() : probed;
}

// Rebuild the composer's speed picker from agentEffortLevels() and select the
// live kClaudeEffortSetting. A stored level the current provider doesn't offer
// (switching to a Codex model with a shorter ladder) falls back to "high", or to
// the top of the ladder, and is written back so the launch and this picker never
// disagree about what the run will use.
void MainWindow::refreshQuickAddSpeedSelector()
{
    if (!m_quickAddSpeedSelector)
        return;
    const QStringList levels = agentEffortLevels();
    if (levels.isEmpty())
        return;
    QString current = QSettings()
                          .value(kClaudeEffortSetting, QStringLiteral("high"))
                          .toString()
                          .trimmed()
                          .toLower();
    if (!levels.contains(current)) {
        current = levels.contains(QStringLiteral("high")) ? QStringLiteral("high")
                                                          : levels.last();
        QSettings().setValue(kClaudeEffortSetting, current);
    }
    const QSignalBlocker block(m_quickAddSpeedSelector);
    m_quickAddSpeedSelector->clear();
    for (const QString &level : levels)
        m_quickAddSpeedSelector->addItem(agentEffortLabel(level), level);
    const int idx = m_quickAddSpeedSelector->findData(current);
    m_quickAddSpeedSelector->setCurrentIndex(idx >= 0 ? idx : 0);
}

// Ask the installed `claude` CLI which --effort values it accepts (adhoc #38)
// instead of hard-coding a ladder that drifts with the CLI. `claude --help`
// prints the flag with its choices, e.g.
//   --effort <level>   Reasoning effort (choices: "low", "medium", "high")
// so the levels are read off that line and cached
// (kClaudeEffortLevelsCacheSetting). Once per app run; anything unparseable
// leaves the cached/default ladder in place.
void MainWindow::refreshClaudeEffortLevels()
{
    if (m_claudeEffortProbe)
        return;
    auto *proc = new QProcess(this);
    m_claudeEffortProbe = proc;
    m_claudeEffortProbeBuf.clear();
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_claudeEffortProbeBuf += proc->readAllStandardOutput();
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int, QProcess::ExitStatus) {
                if (m_claudeEffortProbe == proc)
                    m_claudeEffortProbe = nullptr;
                const QString help = QString::fromUtf8(m_claudeEffortProbeBuf);
                m_claudeEffortProbeBuf.clear();
                proc->deleteLater();
                static const QRegularExpression effortLine(
                    QStringLiteral("--effort[^\\n]*"));
                const QRegularExpressionMatch line = effortLine.match(help);
                if (!line.hasMatch())
                    return;
                // Quoted choices on that line, in the order the CLI lists them.
                static const QRegularExpression choice(
                    QStringLiteral("\"([A-Za-z][A-Za-z0-9_-]*)\""));
                QStringList levels;
                QRegularExpressionMatchIterator it =
                    choice.globalMatch(line.captured(0));
                while (it.hasNext()) {
                    const QString level = it.next().captured(1).toLower();
                    if (!levels.contains(level))
                        levels << level;
                }
                if (levels.isEmpty()) {
                    // Plainer help text lists them unquoted: "(choices: low,
                    // medium, high)".
                    static const QRegularExpression bare(
                        QStringLiteral("choices:\\s*([^)]+)"));
                    const QRegularExpressionMatch list = bare.match(line.captured(0));
                    if (!list.hasMatch())
                        return;
                    static const QRegularExpression separator(
                        QStringLiteral("[,\\s]+"));
                    static const QRegularExpression wordOnly(
                        QStringLiteral("^[a-z][a-z0-9_-]*$"));
                    for (const QString &part :
                         list.captured(1).split(separator, Qt::SkipEmptyParts)) {
                        const QString level = part.trimmed().toLower();
                        if (wordOnly.match(level).hasMatch() && !levels.contains(level))
                            levels << level;
                    }
                }
                if (levels.isEmpty())
                    return;
                QSettings().setValue(kClaudeEffortLevelsCacheSetting, levels);
                refreshQuickAddSpeedSelector();
                if (m_slashActionsPopup && m_slashActionsPopup->isVisible())
                    populateSlashActionsList();
            });
    // A login shell so a `claude` in ~/.local/bin resolves exactly as it does for
    // the real launches.
    proc->start(QStringLiteral("bash"),
                {QStringLiteral("-lc"), QStringLiteral("claude --help 2>/dev/null")});
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

// ---------------------------------------------------------------- World voice

void MainWindow::initializeWorldSpeechBridge()
{
    if (m_worldSpeechBridge)
        return;
    m_worldSpeechBridge = new WorldSpeechBridge(this);
    m_worldSpeechDraftEdit = new QPlainTextEdit(this);
    m_worldSpeechDraftEdit->setVisible(false);
    m_worldSpeechDraftEdit->setObjectName(
        QStringLiteral("worldSpeechTranscriptDraft"));
    m_worldSpeechHiddenMicButton = new QPushButton(this);
    m_worldSpeechHiddenMicButton->setVisible(false);

    connect(m_worldSpeechBridge, &WorldSpeechBridge::auditEvent, this,
            [this](const QString &event) {
                // Events are generalized by the bridge: no capability,
                // transcript, microphone, or browsing data reaches this log.
                logSystem(QStringLiteral("World voice: ") + event);
            });
    connect(m_worldSpeechBridge, &WorldSpeechBridge::captureRequested, this,
            [this](const QString &captureId, const QString &destination) {
                if (!m_worldSpeechBridge || captureId.isEmpty())
                    return;
                if ((m_voiceRecordProc &&
                     m_voiceRecordProc->state() != QProcess::NotRunning) ||
                    (m_voiceTranscribeProc &&
                     m_voiceTranscribeProc->state() != QProcess::NotRunning) ||
                    m_voiceRecording) {
                    m_worldSpeechBridge->publishError(
                        captureId,
                        QStringLiteral("Desktop microphone is already in use."));
                    return;
                }
                if (!voiceInputReady()) {
                    m_worldSpeechBridge->publishError(
                        captureId,
                        QStringLiteral("Set up a local speech-to-text engine in "
                                       "Qt Settings > Voice."));
                    return;
                }
                const AudioRecorderCommand recorder =
                    audioRecorderFor(QStringLiteral("probe.wav"));
                if (recorder.program.isEmpty()) {
                    m_worldSpeechBridge->publishError(
                        captureId,
                        QStringLiteral("No supported local microphone recorder "
                                       "is installed."));
                    return;
                }
                m_worldSpeechCancelPending = false;
                m_worldSpeechCaptureId = captureId;
                m_worldSpeechDraftEdit->clear();
                m_worldSpeechDraftEdit->setPlaceholderText(
                    QStringLiteral("World %1 transcript").arg(destination));
                startVoiceCaptureFor(m_worldSpeechDraftEdit,
                                     m_worldSpeechHiddenMicButton);
                if (!m_voiceRecording) {
                    m_worldSpeechBridge->publishError(
                        captureId,
                        QStringLiteral("The local microphone could not start."));
                    m_worldSpeechCaptureId.clear();
                } else if (m_worldSpeechStatusLabel) {
                    m_worldSpeechStatusLabel->setText(
                        QStringLiteral("World is recording locally for the %1 "
                                       "composer. Use Stop or Cancel in the "
                                       "browser.")
                            .arg(destination));
                }
            });
    connect(m_worldSpeechBridge, &WorldSpeechBridge::stopRequested, this,
            [this](const QString &captureId) {
                if (captureId != m_worldSpeechCaptureId)
                    return;
                stopVoiceCapture();
                if (m_worldSpeechStatusLabel)
                    m_worldSpeechStatusLabel->setText(
                        QStringLiteral("World capture stopped; transcribing "
                                       "locally."));
            });
    connect(m_worldSpeechBridge, &WorldSpeechBridge::cancelRequested, this,
            [this](const QString &captureId) {
                if (captureId == m_worldSpeechCaptureId)
                    cancelWorldVoiceCapture();
            });
}

void MainWindow::createWorldSpeechPairing()
{
    if (!m_worldSpeechBridge)
        initializeWorldSpeechBridge();
    if (!m_worldSpeechBridge || !m_worldSpeechOriginEdit)
        return;
    const QString origin = m_worldSpeechOriginEdit->text().trimmed();
    const QJsonObject capability =
        m_worldSpeechBridge->issuePairing(origin, 120);
    if (capability.isEmpty()) {
        if (m_worldSpeechStatusLabel)
            m_worldSpeechStatusLabel->setText(
                QStringLiteral("Pairing refused. Enter one exact http(s) origin "
                               "without a path, wildcard, query, or fragment."));
        return;
    }
    QSettings().setValue(kWorldSpeechOriginSetting, origin);
    const QString secret =
        capability.value(QStringLiteral("secret")).toString();
    if (m_worldSpeechPairCodeEdit) {
        m_worldSpeechPairCodeEdit->setText(secret);
        m_worldSpeechPairCodeEdit->selectAll();
        m_worldSpeechPairCodeEdit->setFocus();
    }
    if (m_worldSpeechPairButton)
        m_worldSpeechPairButton->setEnabled(false);
    if (m_worldSpeechRevokeButton)
        m_worldSpeechRevokeButton->setEnabled(true);
    if (m_worldSpeechStatusLabel)
        m_worldSpeechStatusLabel->setText(
            QStringLiteral("Listening on 127.0.0.1:%1. This one-time code "
                           "expires at %2 and is valid only for %3.")
                .arg(capability.value(QStringLiteral("port")).toInt())
                .arg(capability.value(QStringLiteral("expiresAt")).toString(),
                     capability.value(QStringLiteral("origin")).toString()));

}

void MainWindow::revokeWorldSpeechPairing()
{
    if (m_worldSpeechBridge)
        m_worldSpeechBridge->revokeAll();
    if (m_worldSpeechPairCodeEdit)
        m_worldSpeechPairCodeEdit->clear();
    if (m_worldSpeechPairButton)
        m_worldSpeechPairButton->setEnabled(true);
    if (m_worldSpeechRevokeButton)
        m_worldSpeechRevokeButton->setEnabled(false);
    if (m_worldSpeechStatusLabel)
        m_worldSpeechStatusLabel->setText(
            QStringLiteral("Bridge capabilities revoked. No browser can "
                           "control the microphone."));
}

void MainWindow::cancelWorldVoiceCapture()
{
    m_worldSpeechCancelPending = true;
    if (m_voiceLiveTimer)
        m_voiceLiveTimer->stop();
    stopVoiceLevelMeter();
    if (m_voiceRecordProc &&
        m_voiceRecordProc->state() != QProcess::NotRunning)
        m_voiceRecordProc->kill();
    if (m_voiceTranscribeProc &&
        m_voiceTranscribeProc->state() != QProcess::NotRunning)
        m_voiceTranscribeProc->kill();
    m_voiceRecording = false;
    hideVoiceTranscribeSpinner();
    QFile::remove(m_voiceWavPath);
    QFile::remove(m_voiceWavPath + QStringLiteral(".out.txt"));
    m_voiceInsertPos = -1;
    m_voiceInsertLen = 0;
    m_voiceLastPreview.clear();
    if (m_worldSpeechDraftEdit)
        m_worldSpeechDraftEdit->clear();
    m_worldSpeechCaptureId.clear();
    updateVoiceInputButton();
    if (m_worldSpeechStatusLabel)
        m_worldSpeechStatusLabel->setText(
            QStringLiteral("World microphone capture cancelled locally."));
}

// Show the mic only once a voice engine is installed; reset its idle look. Called
// when the bar is built and again after a successful install from Settings. Also
// syncs the comment-composer mics (m_voiceButtons), which share the same engine.
void MainWindow::updateVoiceInputButton()
{
    const bool ready = voiceInputReady();
    // The Auto-send toggle only makes sense alongside a working mic, so it stays
    // hidden until speech-to-text is set up.
    if (m_quickAddVoiceAutoSubmit)
        m_quickAddVoiceAutoSubmit->setVisible(ready);
    if (m_quickAddMicButton) {
        // Keep the mic visible (and clickable) even when speech-to-text isn't set
        // up (adhoc #29): rather than disabling it, clicking it while unready jumps
        // to the Settings > Voice tab (see startVoiceCaptureFor / openVoiceSettings,
        // adhoc #132) so the mic is a path to setup, not a dead end.
        m_quickAddMicButton->setVisible(true);
        m_quickAddMicButton->setEnabled(true);
        // Leave the mic currently recording on its red broadcast glyph.
        if (!(m_voiceRecording && m_voiceActiveButton == m_quickAddMicButton)) {
            setOcticon(m_quickAddMicButton, "mic", 16);
            m_quickAddMicButton->setStyleSheet(QString());
            m_quickAddMicButton->setToolTip(
                ready ? QString::fromUtf8(
                            "Speak your prompt \xE2\x80\x94 hold to record, release "
                            "to transcribe.\nVoice model: %1")
                            .arg(voiceModelLabel())
                      : QString::fromUtf8(
                            "Speech-to-text isn't set up yet \xE2\x80\x94 click to open "
                            "Settings and set up voice input."));
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
        openVoiceSettings();
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
                if (m_worldSpeechCancelPending &&
                    m_voiceTargetEdit == m_worldSpeechDraftEdit) {
                    QFile::remove(m_voiceWavPath);
                    QFile::remove(m_voiceWavPath +
                                  QStringLiteral(".out.txt"));
                    return;
                }
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
                    if (m_worldSpeechBridge &&
                        m_voiceTargetEdit == m_worldSpeechDraftEdit &&
                        !m_worldSpeechCaptureId.isEmpty()) {
                        if (err.isEmpty())
                            m_worldSpeechBridge->publishFinal(
                                m_worldSpeechCaptureId, QString());
                        else
                            m_worldSpeechBridge->publishError(
                                m_worldSpeechCaptureId,
                                QStringLiteral("Local microphone capture "
                                               "failed."));
                        m_worldSpeechCaptureId.clear();
                    }
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
                if (m_worldSpeechBridge &&
                    m_voiceTargetEdit == m_worldSpeechDraftEdit &&
                    !m_worldSpeechCaptureId.isEmpty()) {
                    m_worldSpeechBridge->publishError(
                        m_worldSpeechCaptureId,
                        QStringLiteral("The local microphone recorder could "
                                       "not start."));
                    m_worldSpeechCaptureId.clear();
                }
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
            if (m_worldSpeechBridge &&
                target == m_worldSpeechDraftEdit &&
                !m_worldSpeechCaptureId.isEmpty()) {
                m_worldSpeechBridge->publishError(
                    m_worldSpeechCaptureId,
                    QStringLiteral("The local microphone is busy."));
                m_worldSpeechCaptureId.clear();
            }
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

// Land on Settings > Voice — called when a mic is clicked before speech-to-text
// is set up, so the click goes somewhere useful instead of a silent no-op
// (adhoc #132).
void MainWindow::openVoiceSettings()
{
    showSection(1); // Settings
    if (m_settingsTabs && m_voiceSettingsTabIndex >= 0)
        m_settingsTabs->setCurrentIndex(m_voiceSettingsTabIndex);
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
        if (m_worldSpeechBridge &&
            m_voiceTargetEdit == m_worldSpeechDraftEdit &&
            !m_worldSpeechCaptureId.isEmpty()) {
            m_worldSpeechBridge->publishFinal(m_worldSpeechCaptureId,
                                               QString());
            m_worldSpeechCaptureId.clear();
        }
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
                if (m_worldSpeechCancelPending &&
                    m_voiceTargetEdit == m_worldSpeechDraftEdit) {
                    QFile::remove(base + QStringLiteral(".txt"));
                    QFile::remove(wav);
                    hideVoiceTranscribeSpinner();
                    return;
                }

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
                if (text.isEmpty() && exitCode != 0 &&
                    m_worldSpeechBridge &&
                    m_voiceTargetEdit == m_worldSpeechDraftEdit &&
                    !m_worldSpeechCaptureId.isEmpty()) {
                    m_worldSpeechBridge->publishError(
                        m_worldSpeechCaptureId,
                        QStringLiteral("Local speech-to-text failed."));
                    m_worldSpeechCaptureId.clear();
                }
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
        if (finalPass && m_worldSpeechBridge &&
            m_voiceTargetEdit == m_worldSpeechDraftEdit &&
            !m_worldSpeechCaptureId.isEmpty()) {
            m_worldSpeechBridge->publishFinal(m_worldSpeechCaptureId, text);
            m_worldSpeechCaptureId.clear();
        }
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

    if (m_worldSpeechBridge &&
        m_voiceTargetEdit == m_worldSpeechDraftEdit &&
        !m_worldSpeechCaptureId.isEmpty()) {
        if (finalPass) {
            m_worldSpeechBridge->publishFinal(m_worldSpeechCaptureId, text);
            m_worldSpeechCaptureId.clear();
            if (m_worldSpeechStatusLabel)
                m_worldSpeechStatusLabel->setText(
                    QStringLiteral("World transcript completed locally. "
                                   "Review it in the selected browser "
                                   "composer."));
        } else {
            m_worldSpeechBridge->publishPartial(m_worldSpeechCaptureId, text);
        }
    }

    if (finalPass) {
        m_voiceInsertPos = -1;
        m_voiceInsertLen = 0;
        m_voiceTargetEdit->setTextCursor(cur);
        if (m_voiceTargetEdit != m_worldSpeechDraftEdit)
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
    // No repo open (Log/Settings/etc.): nothing repo-specific to show.
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

// The tip of the branch named by the footer's branch button: date, subject and
// author, so the strip says where that branch actually sits (adhoc #55). Read
// detached for the same reason the identity above is — this runs from
// openRepoDetail, where a synchronous git call blocks the GUI thread.
//
// The strip only has room for one elided line, so the same read also pulls the
// fields nobody can see there — full hash, decorations, author email, committer,
// message body, diffstat — and hovering the label pops the lot up as a rich
// tooltip (adhoc #65).
void MainWindow::updateFooterCommitInfo()
{
    if (!m_footerCommitInfo)
        return;
    const QString dir = repoGitDir();
    if (dir.isEmpty()) {
        m_footerCommitInfo->clear();
        return;
    }
    const QString ref = currentRef();
    runGitDetached(
        dir,
        // Separators are git's own %x1f/%x1e placeholders rather than literal
        // escapes so the body (which is last, and may contain anything) stays
        // unambiguous. --root so the initial commit still reports a diffstat.
        {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--root"),
         QStringLiteral("--shortstat"),
         QStringLiteral("--date=format:%Y-%m-%d %H:%M"),
         QStringLiteral("--pretty=%H%x1f%h%x1f%ad%x1f%s%x1f%an%x1f%ct%x1f%ae"
                        "%x1f%cn%x1f%cd%x1f%D%x1f%b%x1e"),
         ref, QStringLiteral("--")},
        [this, dir, ref](bool ok, const QByteArray &out) {
            if (!m_footerCommitInfo)
                return;
            // Repo or branch switched (or closed) while the read was in flight.
            if (repoGitDir() != dir || currentRef() != ref)
                return;
            const QString raw = QString::fromUtf8(out);
            const QStringList f =
                raw.section(QLatin1Char('\x1e'), 0, 0).split(QLatin1Char('\x1f'));
            // No commits yet (fresh repo), or the ref doesn't resolve.
            if (!ok || f.size() < 5) {
                m_footerCommitInfo->clear();
                m_footerCommitInfo->setToolTip(
                    QStringLiteral("Commit the browsed branch points at"));
                return;
            }
            const QString date = f.at(2), subject = f.at(3), author = f.at(4);
            const QString rel =
                formatShortRelativeTime(f.value(5).toLongLong());
            // Long subjects are elided rather than allowed to push the app-path
            // label off the strip; the tooltip keeps the full text.
            const QString shortSubject = m_footerCommitInfo->fontMetrics().elidedText(
                subject, Qt::ElideRight, 360);
            m_footerCommitInfo->setText(
                QString::fromUtf8("\xC2\xB7 %1 \xC2\xB7 %2 \xC2\xB7 %3 \xC2\xB7 %4")
                    .arg(f.at(1), date, shortSubject, author));

            // Everything the one-line strip had to drop, laid out for hover.
            const QString email = f.value(6).trimmed();
            const QString committer = f.value(7).trimmed();
            const QString commitDate = f.value(8).trimmed();
            const QString refs = f.value(9).trimmed();
            // Rejoin past field 10: a message body may legitimately contain the
            // separator, and losing the tail would be worse than keeping it.
            QString body = f.mid(10).join(QLatin1Char('\x1f')).trimmed();
            const QString stat = raw.section(QLatin1Char('\x1e'), 1)
                                     .trimmed()
                                     .section(QLatin1Char('\n'), 0, 0);

            const QString muted = QStringLiteral("#8b949e");
            QStringList lines;
            lines << QStringLiteral("<b>%1</b>").arg(subject.toHtmlEscaped());
            if (!body.isEmpty()) {
                // A long body is trimmed here, not scrolled: a tooltip taller
                // than the window is worse than a truncated one.
                if (body.size() > 800)
                    body = body.left(800) + QString::fromUtf8("\xE2\x80\xA6");
                lines << QStringLiteral("<span style='color:%1'>%2</span>")
                             .arg(muted, body.toHtmlEscaped().replace(
                                             QLatin1Char('\n'),
                                             QStringLiteral("<br>")));
            }
            QStringList meta;
            meta << QStringLiteral("Commit: %1").arg(f.at(0).toHtmlEscaped());
            if (!refs.isEmpty())
                meta << QStringLiteral("Refs: %1").arg(refs.toHtmlEscaped());
            meta << QStringLiteral("Author: %1")
                        .arg((email.isEmpty()
                                  ? author
                                  : QStringLiteral("%1 <%2>").arg(author, email))
                                 .toHtmlEscaped());
            if (!committer.isEmpty() && committer != author)
                meta << QStringLiteral("Committer: %1").arg(committer.toHtmlEscaped());
            meta << QStringLiteral("Authored: %1%2")
                        .arg(date.toHtmlEscaped(),
                             rel.isEmpty()
                                 ? QString()
                                 : QString::fromUtf8(" (%1 ago)").arg(rel));
            if (!commitDate.isEmpty() && commitDate != date)
                meta << QStringLiteral("Committed: %1").arg(commitDate.toHtmlEscaped());
            if (!stat.isEmpty())
                meta << QStringLiteral("Changes: %1").arg(stat.toHtmlEscaped());
            lines << QStringLiteral("<span style='color:%1'>%2</span>")
                         .arg(muted, meta.join(QStringLiteral("<br>")));

            // A width attribute, not CSS: Qt's rich text ignores the latter, and
            // without it the hash and body lines lay out as one endless row.
            m_footerCommitInfo->setToolTip(
                QStringLiteral("<table cellspacing='0' cellpadding='0'><tr>"
                               "<td width='460'>%1</td></tr></table>")
                    .arg(lines.join(QStringLiteral("<br><br>"))));
        });
}

// Start the UI-stall watchdog + the live CPU/memory readout. Called once the
// window is up so the heartbeat reflects a real, interactive event loop.
QString MainWindow::stallLogPath()
{
#ifdef FORKMESH_WINDOW_TESTS
    // Test binaries stall on purpose (blocking asserts, offscreen waits) —
    // never let their reports pollute the user's real diagnostics log.
    return QString();
#else
    return QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics/stalls.log");
#endif
}

void MainWindow::startDiagnostics()
{
    if (!m_stallWatchdog) {
        m_stallWatchdog = new StallWatchdog(this);
        connect(m_stallWatchdog, &StallWatchdog::stalled, this, &MainWindow::onUiStall);
        const QString logPath = stallLogPath();
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
        // Name the durable log in the main app log once per run, so where the
        // full backtraces live is discoverable from the Log view alone and not
        // only from the diagnostics dialog (adhoc #73).
        if (!logPath.isEmpty())
            logSystem(QStringLiteral("UI-stall watchdog armed; reports append to %1")
                          .arg(QDir::toNativeSeparators(logPath)));
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
    double hostMemoryPct = -1.0;
    if (auto *mem = static_cast<ResourceSparkline *>(m_memChart)) {
        const qint64 total = SystemStats::totalMemoryBytes();
        const qint64 avail = SystemStats::availableMemoryBytes();
        double pct = -1.0;
        if (total > 0 && avail >= 0 && avail <= total)
            pct = 100.0 * double(total - avail) / double(total);
        hostMemoryPct = pct;
        mem->addSample(pct >= 0 ? pct : 0.0, 100.0,
                       pct >= 0 ? QStringLiteral("%1%").arg(pct, 0, 'f', 0) : dash);
        mem->setToolTip(
            total > 0
                ? QStringLiteral("Host memory in use: %1 of %2")
                      .arg(SystemStats::formatBytes(total - avail),
                           SystemStats::formatBytes(total))
                : QStringLiteral("Host memory in use"));
    }
#ifndef FORKMESH_WINDOW_TESTS
    // Open once on the upward crossing. It rearms only after memory has fallen
    // comfortably below the threshold, so dismissing the panel at 86% does not
    // make it reopen every second.
    if (hostMemoryPct >= 85.0 && m_highMemoryAlertArmed) {
        m_highMemoryAlertArmed = false;
        QTimer::singleShot(0, this, &MainWindow::showHighMemoryProcessPanel);
    } else if (hostMemoryPct >= 0.0 && hostMemoryPct < 83.0) {
        m_highMemoryAlertArmed = true;
    }
#endif
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

    // The diagnostics indicator rides beside the CPU/MEM/DISK sparklines now
    // (adhoc #145). Crisp octicons replace the old 🖥/⚠ emoji: a muted monitor
    // while the UI has stayed smooth, and an amber alert plus the running count
    // once a stall has been recorded so it reads as a real warning.
    if (m_stallCount > 0) {
        m_footerDiagnostics->setIcon(
            themedOcticon(QStringLiteral("alert"), QColor("#d29922"), 14));
        m_footerDiagnostics->setIconSize(QSize(14, 14));
        m_footerDiagnostics->setText(
            QStringLiteral(" %1 stall%2")
                .arg(m_stallCount)
                .arg(m_stallCount == 1 ? QString() : QStringLiteral("s")));
    } else {
        setOcticon(m_footerDiagnostics, QStringLiteral("device-desktop"), 14);
        m_footerDiagnostics->setText(QString());
    }
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
    QString head = QStringLiteral("UI stalled ~%1 ms (event loop blocked)").arg(peakMs);
    if (!blockingCall.isEmpty())
        head += QStringLiteral(" while %1").arg(blockingCall);
    // Shows up in the app's Log view under its own STALL badge (filterable from
    // the chip row); logSystem stamps the time itself, so the dialog's copy is
    // the one that carries it. The main-log copy also names the durable report
    // file so the full backtrace is findable from the Log view (adhoc #73).
    QString logLine = head;
    if (!m_stallLogPath.isEmpty())
        logLine += QStringLiteral(" - full backtrace in %1")
                       .arg(QDir::toNativeSeparators(m_stallLogPath));
    logSystem(logLine);
    QString entry = QStringLiteral("[%1] %2").arg(when, head);
    if (!backtrace.isEmpty())
        entry += QLatin1Char('\n') + backtrace;
    m_stallLog.append(entry);
    while (m_stallLog.size() > 100)
        m_stallLog.removeFirst();
    if (m_footerDiagnostics)
        m_footerDiagnostics->setToolTip(
            QStringLiteral("Last UI stall: ~%1 ms at %2%3 (%4 logged). Click to draft a "
                           "fix-it prompt in the composer; right-click for details.")
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
#ifdef FORKMESH_WINDOW_TESTS
    // Window tests deliberately hold the GUI thread while checking worker and
    // responsive-layout states. Keep the watchdog coverage/logging active, but
    // never turn those synthetic stalls into real CLI agent processes.
    return;
#endif
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
            QStringLiteral("UI-stall diagnostics: any freezes long enough to trip the "
                           "Wait/Kill prompt land here. Click to draft a fix-it prompt "
                           "in the composer; right-click for the recorded stall "
                           "details."));
    updateFooterDiagnostics();
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
            "changed).\n\n%2\nRecorded stalls:\n\n%3")
            .arg(m_stallLog.size())
            .arg(stallLogLocationsBlock())
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

// Keep the drafted prompt inside the quick-add composer's 16000-char cap. The
// composer trims overflow off the *end*, which would cut a backtrace mid-frame,
// so build the text to fit instead and say so where reports were dropped.
static constexpr int kStallPromptMaxChars = 15500;

// The "please fix these stalls" prompt the footer badge drafts: the ask, where
// both logs live, and the recorded reports newest-first (the freshest freeze is
// the one most likely still reproducible).
// Both on-disk logs an agent needs to work a stall report, plus the standing ask
// to sweep them for work that should have been backgrounded but was not: the
// sampled backtrace only names the call that happened to be on the stack, while
// the app log records every slow main-thread operation of the session (adhoc #90).
QString MainWindow::stallLogLocationsBlock() const
{
    QString block;
    const QString stallLog =
        m_stallLogPath.isEmpty() ? stallLogPath() : m_stallLogPath;
    if (!stallLog.isEmpty())
        block += QStringLiteral("Stall log: %1\n")
                     .arg(QDir::toNativeSeparators(stallLog));
    block += QStringLiteral("App log: %1\n")
                 .arg(QDir::toNativeSeparators(networkLogPath()));
    block += QStringLiteral(
        "\nAlso read both logs for any other work that never got backgrounded — "
        "slow git/network/disk operations still running on the GUI thread — and "
        "move those off the main thread too, not just the sampled frames below.\n");
    return block;
}

QString MainWindow::stallFixPrompt() const
{
    QString head =
        QStringLiteral(
            "Please fix these UI stalls. ForkMesh's GUI thread was blocked %1 "
            "time(s) this session, which freezes the window. For each report "
            "below, find the blocking call in the backtrace and fix it so the UI "
            "stays responsive (move the slow work off the main thread, or skip it "
            "when nothing changed).\n\n")
            .arg(m_stallCount);
    head += stallLogLocationsBlock();
    head += QStringLiteral("\nRecorded stalls (newest first):\n\n");

    QString body;
    const QString separator = QStringLiteral("\n\n---\n\n");
    for (int i = m_stallLog.size() - 1; i >= 0; --i) {
        const QString entry = m_stallLog.at(i);
        if (!body.isEmpty() &&
            head.size() + body.size() + separator.size() + entry.size() >
                kStallPromptMaxChars) {
            body += QStringLiteral(
                "\n\n(older reports omitted - the full history is in the stall log above)");
            break;
        }
        if (!body.isEmpty())
            body += separator;
        body += entry;
    }
    if (body.isEmpty())
        body = QStringLiteral("(nothing recorded yet)");
    // A single oversized backtrace can still overrun the budget; clamp so the
    // composer never has to trim (and never silently drops the trailing text).
    return (head + body).left(kStallPromptMaxChars);
}

#ifdef FORKMESH_WINDOW_TESTS
QString MainWindow::testQuickAddText() const
{
    return m_issueQuickAdd ? m_issueQuickAdd->toPlainText() : QString();
}

bool MainWindow::testDraftStallPromptInComposer()
{
    if (!m_issueQuickAdd)
        return false;
    sendStallReportToComposer();
    return true;
}
#endif

// Footer stall badge click (adhoc #73): rather than only showing the read-only
// dialog, draft the fix-it prompt straight into the quick-add composer so the
// recorded freezes are one Enter away from an agent run. The detail dialog is
// still one right-click away (and is the fallback when nothing was recorded).
void MainWindow::sendStallReportToComposer()
{
    if (!m_issueQuickAdd || m_stallLog.isEmpty()) {
        showDiagnosticsDialog();
        return;
    }
    // Anything half-typed goes into the recall history first, so overwriting the
    // box with the draft never loses a prompt — Up brings it straight back.
    recordQuickAddHistory(m_issueQuickAdd->toPlainText());
    m_issueQuickAdd->setPlainText(stallFixPrompt());
    m_issueQuickAdd->moveCursor(QTextCursor::End);
    m_issueQuickAdd->setFocus();
    logSystem(QStringLiteral("Drafted a fix-it prompt for the %1 recorded UI stall(s); "
                             "details in %2")
                  .arg(m_stallLog.size())
                  .arg(m_stallLogPath.isEmpty()
                           ? QStringLiteral("this session's diagnostics")
                           : QDir::toNativeSeparators(m_stallLogPath)));
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
    auto *draftBtn = new QPushButton(QStringLiteral("Draft in composer"));
    draftBtn->setToolTip(
        QStringLiteral("Fill the footer composer with a \"fix these stalls\" prompt "
                       "(with the log locations) so it can be reviewed before sending"));
    draftBtn->setEnabled(haveStalls);
    auto *sendBtn = new QPushButton(QStringLiteral("Send to a new agent"));
    sendBtn->setToolTip(
        QStringLiteral("Hand all recorded stalls to a coding agent to investigate and fix"));
    sendBtn->setEnabled(haveStalls);
    auto *close = new QPushButton(QStringLiteral("Close"));

    connect(clearBtn, &QPushButton::clicked, &dlg,
            [this, summary, view, clearBtn, sendBtn, draftBtn] {
                clearStallLog();
                summary->setText(QStringLiteral(
                    "No UI stalls detected this session. The app watches the "
                    "GUI thread and records any freeze longer than 1.5s here."));
                view->setPlainText(QStringLiteral("(nothing recorded yet)"));
                clearBtn->setEnabled(false);
                sendBtn->setEnabled(false);
                draftBtn->setEnabled(false);
            });
    connect(draftBtn, &QPushButton::clicked, &dlg, [this, &dlg] {
        sendStallReportToComposer();
        dlg.accept();
    });
    connect(sendBtn, &QPushButton::clicked, &dlg, [this, &dlg] {
        if (sendStallLogToAgent())
            dlg.accept();
    });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto *row = new QHBoxLayout;
    row->addWidget(clearBtn);
    row->addStretch(1);
    row->addWidget(draftBtn);
    row->addWidget(sendBtn);
    row->addWidget(close);
    v->addLayout(row);
    dlg.exec();
}

// Column layout of the "High memory usage" table; adhoc #98 added the trend
// square and the command line and adhoc #96 the agent attribution, so the
// indexes are worth naming.
static constexpr int kHighMemoryTrendColumn = 1;
static constexpr int kHighMemoryAgentColumn = 6;
static constexpr int kHighMemoryCommandColumn = 7;
static constexpr int kHighMemoryActionColumn = 8;
static constexpr int kHighMemoryColumnCount = 9;
// How many rows carry a trend square. One per row for all 30 would be mostly
// noise; the top ten are the ones worth watching grow.
static constexpr int kHighMemoryTrendRows = 10;

void MainWindow::showHighMemoryProcessPanel()
{
    if (m_highMemoryDialog) {
        m_highMemoryDialog->show();
        m_highMemoryDialog->raise();
        m_highMemoryDialog->activateWindow();
        refreshHighMemoryProcessTable();
        return;
    }

    auto *dialog = new QDialog(this);
    m_highMemoryDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setModal(false);
    dialog->setWindowTitle(QStringLiteral("High memory usage"));
    // Wide enough for the command-line column to be worth reading (adhoc #98)
    // beside the agent column (adhoc #96), clamped to the screen so it still
    // fits on smaller displays.
    QSize preferred(1420, 620);
    if (QScreen *screen = QGuiApplication::primaryScreen())
        preferred =
            preferred.boundedTo(screen->availableGeometry().size() * 0.92);
    dialog->resize(preferred);
    auto *layout = new QVBoxLayout(dialog);

    auto *heading = new QLabel(QStringLiteral(
        "<b>Host memory is above 85%</b><br>"
        "Processes are sorted by resident memory, and the top ten carry a "
        "trend square showing how their memory has moved since this panel "
        "opened. Anything an agent run started — the agent itself and every "
        "build, test or tool below it — names that run in the Agent column. "
        "“Kill” requests a normal termination and “Kill all” does the "
        "same for every listed process sharing that name; ForkMesh and PID 1 "
        "are protected."));
    heading->setTextFormat(Qt::RichText);
    heading->setWordWrap(true);
    layout->addWidget(heading);

    m_highMemoryProcessStatus =
        new QLabel(QStringLiteral("Scanning for the largest memory users…"));
    m_highMemoryProcessStatus->setObjectName(QStringLiteral("statusLine"));
    layout->addWidget(m_highMemoryProcessStatus);

    m_highMemoryProcessTable = new QTableWidget(0, kHighMemoryColumnCount);
    m_highMemoryProcessTable->setHorizontalHeaderLabels(
        {QStringLiteral("Process"), QStringLiteral("Trend"),
         QStringLiteral("PID"), QStringLiteral("Owner"),
         QStringLiteral("Memory"), QStringLiteral("Host %"),
         QStringLiteral("Agent"), QStringLiteral("Command line"),
         QStringLiteral("Action")});
    m_highMemoryProcessTable->verticalHeader()->setVisible(false);
    m_highMemoryProcessTable->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    m_highMemoryProcessTable->setEditTriggers(
        QAbstractItemView::NoEditTriggers);
    m_highMemoryProcessTable->setSortingEnabled(false);
    m_highMemoryProcessTable->setAlternatingRowColors(true);
    m_highMemoryProcessTable->setWordWrap(false);
    m_highMemoryProcessTable->setTextElideMode(Qt::ElideRight);
    // The command line takes every spare pixel now that it is the widest cell;
    // the rest stay at fixed, content-sized widths.
    for (int column = 0; column < kHighMemoryColumnCount; ++column)
        m_highMemoryProcessTable->horizontalHeader()->setSectionResizeMode(
            column, column == kHighMemoryCommandColumn ? QHeaderView::Stretch
                                                       : QHeaderView::Fixed);
    m_highMemoryProcessTable->setColumnWidth(0, 180);
    m_highMemoryProcessTable->setColumnWidth(kHighMemoryTrendColumn, 46);
    m_highMemoryProcessTable->setColumnWidth(2, 72);
    m_highMemoryProcessTable->setColumnWidth(3, 110);
    m_highMemoryProcessTable->setColumnWidth(4, 105);
    m_highMemoryProcessTable->setColumnWidth(5, 72);
    m_highMemoryProcessTable->setColumnWidth(kHighMemoryAgentColumn, 190);
    m_highMemoryProcessTable->setColumnWidth(kHighMemoryActionColumn,
                                            160); // Kill + Kill all
    // Tall enough for a trend square to sit inside a row.
    m_highMemoryProcessTable->verticalHeader()->setDefaultSectionSize(38);
    layout->addWidget(m_highMemoryProcessTable, 1);

    auto *refresh = new QPushButton(QStringLiteral("Refresh"));
    setOcticon(refresh, QStringLiteral("sync"), 14);
    connect(refresh, &QPushButton::clicked, this,
            &MainWindow::refreshHighMemoryProcessTable);
    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(refresh);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(dialog, &QObject::destroyed, this, [this] {
        m_highMemoryDialog = nullptr;
        m_highMemoryProcessTable = nullptr;
        m_highMemoryProcessStatus = nullptr;
        m_highMemoryProcessQuery = nullptr;
        m_highMemoryRssHistory.clear();
    });
    auto *autoRefresh = new QTimer(dialog);
    autoRefresh->setInterval(5000);
    connect(autoRefresh, &QTimer::timeout, this,
            &MainWindow::refreshHighMemoryProcessTable);
    autoRefresh->start();
    dialog->show();
    dialog->raise();
    // Let the new window paint before starting even the lightweight process
    // query. QProcess remains asynchronous, so a pressured host never blocks
    // the GUI while the culprit list is collected.
    QTimer::singleShot(0, this, &MainWindow::refreshHighMemoryProcessTable);
}

void MainWindow::refreshHighMemoryProcessTable()
{
    if (!m_highMemoryDialog || !m_highMemoryProcessTable ||
        m_highMemoryProcessQuery)
        return;
    m_highMemoryProcessStatus->setText(
        m_highMemoryProcessTable->rowCount() == 0
            ? QStringLiteral("Scanning for the largest memory users…")
            : QStringLiteral("Refreshing memory culprits in the background…"));
    auto *query = new QProcess(m_highMemoryDialog);
    m_highMemoryProcessQuery = query;
    connect(query, &QProcess::finished, this,
            [this, query](int exitCode, QProcess::ExitStatus status) {
                const QByteArray output = query->readAllStandardOutput();
                const QByteArray error = query->readAllStandardError();
                query->deleteLater();
                m_highMemoryProcessQuery = nullptr;
                if (!m_highMemoryDialog || !m_highMemoryProcessTable)
                    return;
                if (status != QProcess::NormalExit || exitCode != 0) {
                    const QString detail =
                        QString::fromLocal8Bit(error).trimmed();
                    m_highMemoryProcessStatus->setText(
                        QStringLiteral("Could not list processes: %1")
                            .arg(detail.isEmpty()
                                     ? QStringLiteral("the background query timed out")
                                     : detail));
                    return;
                }

                struct ProcessRow {
                    qint64 pid = 0;
                    QString owner;
                    qint64 rssKb = 0;
                    double percent = 0.0;
                    QString name;
                    QString commandLine;
                };
                QVector<ProcessRow> rows;
                rows.reserve(30);
                // Parent of every process on the host, so a listed row can be
                // walked back to the agent run that spawned it (adhoc #96).
                // `ps` already reports it, which keeps the attribution free of
                // a second /proc pass.
                QHash<qint64, qint64> parentOf;
                const QList<QByteArray> lines = output.split('\n');
                int validProcesses = 0;
                for (const QByteArray &raw : lines) {
                    const QList<QByteArray> fields =
                        raw.simplified().split(' ');
                    if (fields.size() < 6)
                        continue;
                    bool pidOk = false;
                    bool rssOk = false;
                    const qint64 pid = fields.at(0).toLongLong(&pidOk);
                    const qint64 ppid = fields.at(1).toLongLong();
                    const QString owner = QString::fromLocal8Bit(fields.at(2));
                    const qint64 rssKb = fields.at(3).toLongLong(&rssOk);
                    const QString percent =
                        QString::fromLocal8Bit(fields.at(4));
                    // `args` (not `comm`) so the arguments have something to
                    // show (adhoc #98); the Process column keeps reading like
                    // the old command name by taking argv[0]'s basename.
                    const QString commandLine =
                        QString::fromLocal8Bit(
                            QByteArrayList(fields.mid(5)).join(' '));
                    if (!pidOk || !rssOk || pid <= 0 || commandLine.isEmpty())
                        continue;
                    if (ppid > 0)
                        parentOf.insert(pid, ppid);
                    QString name = commandLine.section(QLatin1Char(' '), 0, 0);
                    const int slash = name.lastIndexOf(QLatin1Char('/'));
                    if (slash >= 0)
                        name = name.mid(slash + 1);
                    if (name.isEmpty())
                        name = commandLine;
                    ++validProcesses;
                    // `ps` is already RSS-sorted. Only materialize the top
                    // culprits: hundreds of cell widgets and repeated
                    // ResizeToContents passes were what made the old alert
                    // appear frozen under memory pressure.
                    if (rows.size() < 30)
                        rows.append({pid, owner, rssKb, percent.toDouble(),
                                     name, commandLine});
                }

                // Per-PID resident history, so the top rows can each carry a
                // trend square. Only PIDs still on the list are kept, which
                // bounds the map to the table's own size.
                QHash<qint64, QVector<double>> history;
                history.reserve(rows.size());
                double historyMaxKb = 1.0;
                for (const ProcessRow &process : rows) {
                    QVector<double> samples =
                        m_highMemoryRssHistory.value(process.pid);
                    samples.append(double(process.rssKb));
                    while (samples.size() > ProcessMemorySparkline::kMaxPoints)
                        samples.removeFirst();
                    for (double sample : samples)
                        historyMaxKb = qMax(historyMaxKb, sample);
                    history.insert(process.pid, samples);
                }
                m_highMemoryRssHistory = history;

                // Which listed PIDs share each command name, so a row's "Kill
                // all" can act on the whole family (the `killall` shape) without
                // shelling out.
                QHash<QString, QList<qint64>> pidsByName;
                for (const ProcessRow &process : rows)
                    pidsByName[process.name].append(process.pid);

                // Agent attribution (adhoc #96): the process driving each live
                // session, keyed by PID. A row is that session's if it is the
                // process itself or sits anywhere below it, so the pytest run
                // and the cc1plus swarm an agent kicked off say whose they are.
                struct AgentOwner {
                    qint64 rootPid = 0;
                    QString label;
                    QString detail;
                };
                QHash<qint64, AgentOwner> agentRoots;
                for (const AgentSession &session : std::as_const(m_agentSessions)) {
                    const qint64 rootPid = agentSessionProcessId(session.id);
                    if (rootPid <= 0)
                        continue;
                    QString title = session.issueTitle.trimmed();
                    if (title.isEmpty())
                        title = session.prompt.section(QLatin1Char('\n'), 0, 0)
                                    .trimmed();
                    if (title.isEmpty())
                        title = QStringLiteral("Agent run #%1").arg(session.id);
                    AgentOwner owner;
                    owner.rootPid = rootPid;
                    owner.label =
                        session.issueNumber > 0
                            ? QStringLiteral("#%1 %2").arg(session.issueNumber).arg(title)
                            : title;
                    QStringList detail{owner.label};
                    if (!session.owner.isEmpty() && !session.name.isEmpty())
                        detail << QStringLiteral("Repository: %1/%2")
                                      .arg(session.owner, session.name);
                    if (!session.branchName.isEmpty())
                        detail << QStringLiteral("Branch: %1").arg(session.branchName);
                    QString provider = agentProviderName(session.provider);
                    if (!session.model.isEmpty())
                        provider += QStringLiteral(" · %1").arg(session.model);
                    detail << QStringLiteral("Agent: %1").arg(provider)
                           << QStringLiteral("Status: %1").arg(session.status);
                    owner.detail = detail.join(QLatin1Char('\n'));
                    agentRoots.insert(rootPid, owner);
                }
                // Walk a listed PID up to init looking for one of those roots.
                // The hop cap is belt and braces: a `ps` snapshot taken while
                // processes exit can hand back an inconsistent parent chain.
                auto agentOwnerFor =
                    [&agentRoots, &parentOf](qint64 pid) -> const AgentOwner * {
                    for (int hops = 0; pid > 1 && hops < 64; ++hops) {
                        const auto found = agentRoots.constFind(pid);
                        if (found != agentRoots.constEnd())
                            return &found.value();
                        const qint64 parent = parentOf.value(pid, 0);
                        if (parent <= 0 || parent == pid)
                            break;
                        pid = parent;
                    }
                    return nullptr;
                };

                m_highMemoryProcessTable->setUpdatesEnabled(false);
                m_highMemoryProcessTable->clearContents();
                m_highMemoryProcessTable->setRowCount(rows.size());
                qint64 shownRssKb = 0;
                int agentRows = 0;
                for (int row = 0; row < rows.size(); ++row) {
                    const ProcessRow &process = rows.at(row);
                    shownRssKb += process.rssKb;
                    auto put = [this, row](int column, const QString &text,
                                           const QVariant &sortValue = {}) {
                        auto *item = new QTableWidgetItem(text);
                        if (sortValue.isValid())
                            item->setData(Qt::UserRole, sortValue);
                        m_highMemoryProcessTable->setItem(row, column, item);
                    };
                    put(0, process.name);
                    put(2, QString::number(process.pid), process.pid);
                    put(3, process.owner);
                    put(4, SystemStats::formatBytes(process.rssKb * 1024),
                        process.rssKb);
                    put(5, QStringLiteral("%1%").arg(process.percent, 0, 'f', 1),
                        process.percent);
                    // Whose run this is, if any (adhoc #96). The agent's own
                    // process names the session outright; anything it started
                    // is marked with "↳" so the tree reads at a glance.
                    const AgentOwner *agent = agentOwnerFor(process.pid);
                    if (agent) {
                        ++agentRows;
                        const bool isAgentItself = process.pid == agent->rootPid;
                        put(kHighMemoryAgentColumn,
                            isAgentItself
                                ? agent->label
                                : QStringLiteral("↳ %1").arg(agent->label));
                        if (QTableWidgetItem *cell =
                                m_highMemoryProcessTable->item(
                                    row, kHighMemoryAgentColumn))
                            cell->setToolTip(
                                isAgentItself
                                    ? QStringLiteral("%1\n\nThis is the agent's "
                                                     "own process.")
                                          .arg(agent->detail)
                                    : QStringLiteral("%1\n\nStarted by that "
                                                     "agent run (PID %2).")
                                          .arg(agent->detail)
                                          .arg(agent->rootPid));
                    }
                    // The full command line outgrows any sane column, so the
                    // cell elides and the tooltip carries the whole thing.
                    put(kHighMemoryCommandColumn, process.commandLine);
                    if (QTableWidgetItem *command =
                            m_highMemoryProcessTable->item(
                                row, kHighMemoryCommandColumn))
                        command->setToolTip(process.commandLine);
                    if (row < 5) {
                        for (int column = 0; column <= kHighMemoryCommandColumn;
                             ++column) {
                            QTableWidgetItem *item =
                                m_highMemoryProcessTable->item(row, column);
                            if (!item) // the trend column holds a widget, not an item
                                continue;
                            QFont font = item->font();
                            font.setBold(true);
                            item->setFont(font);
                            item->setForeground(QColor(QStringLiteral("#cf222e")));
                        }
                    }

                    // Trend square for the top ten: the footer sparkline shape,
                    // on a scale shared by every row so the squares compare
                    // against each other.
                    if (row < kHighMemoryTrendRows) {
                        const QVector<double> samples =
                            history.value(process.pid);
                        auto *chart = new ProcessMemorySparkline;
                        chart->setHistory(samples, historyMaxKb);
                        auto *cell = new QWidget;
                        // The square ignores mouse events, so its tooltip has
                        // to live on the cell around it.
                        cell->setToolTip(
                            QStringLiteral("Resident memory for %1 (PID %2) "
                                           "over the last %3 refreshes")
                                .arg(process.name)
                                .arg(process.pid)
                                .arg(samples.size()));
                        auto *cellRow = new QHBoxLayout(cell);
                        cellRow->setContentsMargins(0, 0, 0, 0);
                        cellRow->addWidget(chart, 0, Qt::AlignCenter);
                        m_highMemoryProcessTable->setCellWidget(
                            row, kHighMemoryTrendColumn, cell);
                    }

                    auto *kill = new QPushButton(QStringLiteral("Kill"));
                    kill->setProperty("buttonSize", "sm");
                    const bool protectedProcess =
                        process.pid == 1 ||
                        process.pid == QCoreApplication::applicationPid();
                    kill->setEnabled(!protectedProcess);
                    kill->setToolTip(
                        protectedProcess
                            ? QStringLiteral("This process is protected")
                            : QStringLiteral("Request that %1 terminate")
                                  .arg(process.name));
                    connect(kill, &QPushButton::clicked, this,
                            [this, pid = process.pid, name = process.name] {
                                killHighMemoryProcess(pid, name);
                            });

                    // "Kill all" beside it, for the common case where the memory
                    // is spread across many same-named workers (adhoc #46). It
                    // only offers itself when the list holds more than one
                    // killable PID under that name.
                    const QList<qint64> family = pidsByName.value(process.name);
                    QList<qint64> killable;
                    for (qint64 candidate : family) {
                        if (candidate <= 1 ||
                            candidate == QCoreApplication::applicationPid())
                            continue;
                        killable.append(candidate);
                    }
                    auto *killAll = new QPushButton(QStringLiteral("Kill all"));
                    killAll->setProperty("buttonSize", "sm");
                    killAll->setEnabled(killable.size() > 1);
                    killAll->setToolTip(
                        killable.size() > 1
                            ? QStringLiteral("Request that all %1 listed “%2” "
                                             "processes terminate")
                                  .arg(killable.size())
                                  .arg(process.name)
                            : QStringLiteral("Only one killable “%1” process is "
                                             "listed")
                                  .arg(process.name));
                    connect(killAll, &QPushButton::clicked, this,
                            [this, name = process.name, killable] {
                                killAllHighMemoryProcesses(name, killable);
                            });

                    auto *actions = new QWidget;
                    auto *actionRow = new QHBoxLayout(actions);
                    actionRow->setContentsMargins(0, 0, 0, 0);
                    actionRow->setSpacing(4);
                    actionRow->addWidget(kill);
                    actionRow->addWidget(killAll);
                    m_highMemoryProcessTable->setCellWidget(
                        row, kHighMemoryActionColumn, actions);
                }
                m_highMemoryProcessTable->setUpdatesEnabled(true);
                m_highMemoryProcessTable->viewport()->update();
                m_highMemoryProcessStatus->setText(
                    QStringLiteral(
                        "Top %1 of %2 processes · %3 resident%4 · refreshed %5")
                        .arg(rows.size())
                        .arg(validProcesses)
                        .arg(SystemStats::formatBytes(shownRssKb * 1024))
                        .arg(agentRows > 0
                                 ? QStringLiteral(" · %1 from agent runs")
                                       .arg(agentRows)
                                 : QString())
                        .arg(QTime::currentTime().toString(
                            QStringLiteral("h:mm:ss AP"))));
            });
    connect(query, &QProcess::errorOccurred, this,
            [this, query](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                const QString detail = query->errorString();
                query->deleteLater();
                if (m_highMemoryProcessQuery == query)
                    m_highMemoryProcessQuery = nullptr;
                if (m_highMemoryProcessStatus)
                    m_highMemoryProcessStatus->setText(
                        QStringLiteral("Could not list processes: %1")
                            .arg(detail));
            });
    query->start(QStringLiteral("ps"),
                 {QStringLiteral("-eo"),
                  QStringLiteral("pid=,ppid=,user=,rss=,%mem=,args="),
                  QStringLiteral("--sort=-rss")});
    // A broken or heavily starved `ps` must not leave the panel looking busy
    // forever. Killing this helper is safe and does not affect listed processes.
    QTimer::singleShot(3000, query, [query] {
        if (query->state() != QProcess::NotRunning)
            query->kill();
    });
}

void MainWindow::killHighMemoryProcess(qint64 pid, const QString &name)
{
    if (pid <= 1 || pid == QCoreApplication::applicationPid())
        return;
    const auto answer = QMessageBox::warning(
        m_highMemoryDialog ? static_cast<QWidget *>(m_highMemoryDialog.data())
                           : this,
        QStringLiteral("Kill process"),
        QStringLiteral("Request that “%1” (PID %2) terminate?\n\n"
                       "Unsaved work in that process may be lost.")
            .arg(name)
            .arg(pid),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

#if defined(Q_OS_UNIX)
    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0) {
        QMessageBox::warning(
            m_highMemoryDialog
                ? static_cast<QWidget *>(m_highMemoryDialog.data())
                : this,
            QStringLiteral("Could not kill process"),
            QStringLiteral("%1 (PID %2): %3")
                .arg(name)
                .arg(pid)
                .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
    if (m_highMemoryProcessStatus)
        m_highMemoryProcessStatus->setText(
            QStringLiteral("Termination requested for %1 (PID %2).")
                .arg(name)
                .arg(pid));
    QTimer::singleShot(750, this,
                       &MainWindow::refreshHighMemoryProcessTable);
#else
    QMessageBox::information(
        m_highMemoryDialog ? static_cast<QWidget *>(m_highMemoryDialog.data())
                           : this,
        QStringLiteral("Kill process"),
        QStringLiteral("Per-process termination is not supported on this "
                       "platform yet."));
#endif
}

void MainWindow::killAllHighMemoryProcesses(const QString &name,
                                            const QList<qint64> &pids)
{
    QList<qint64> targets;
    for (qint64 pid : pids) {
        if (pid <= 1 || pid == QCoreApplication::applicationPid())
            continue;
        targets.append(pid);
    }
    if (targets.isEmpty())
        return;
    auto *parent = m_highMemoryDialog
                       ? static_cast<QWidget *>(m_highMemoryDialog.data())
                       : this;
    // No confirmation here (adhoc #58): "Kill all" is an explicit, already
    // deliberate click, and the tooltip spells out how many processes it hits.

#if defined(Q_OS_UNIX)
    int sent = 0;
    QStringList failures;
    for (qint64 pid : targets) {
        if (::kill(static_cast<pid_t>(pid), SIGTERM) == 0) {
            ++sent;
            continue;
        }
        // A process that exited between the listing and the click is not a
        // failure worth reporting as one.
        if (errno == ESRCH)
            continue;
        failures.append(QStringLiteral("PID %1: %2")
                            .arg(pid)
                            .arg(QString::fromLocal8Bit(std::strerror(errno))));
    }
    if (!failures.isEmpty())
        QMessageBox::warning(parent,
                             QStringLiteral("Could not kill every process"),
                             QStringLiteral("%1\n%2")
                                 .arg(name, failures.join(QLatin1Char('\n'))));
    if (m_highMemoryProcessStatus)
        m_highMemoryProcessStatus->setText(
            QStringLiteral("Termination requested for %1 “%2” process%3.")
                .arg(sent)
                .arg(name)
                .arg(sent == 1 ? QString() : QStringLiteral("es")));
    QTimer::singleShot(750, this, &MainWindow::refreshHighMemoryProcessTable);
#else
    QMessageBox::information(
        parent, QStringLiteral("Kill all processes"),
        QStringLiteral("Per-process termination is not supported on this "
                       "platform yet."));
#endif
}

void MainWindow::showTreasuryDonateDialog()
{
    // Read only the Worker's public pool state. The Worker never creates,
    // receives, decrypts, or signs with the pool key; that key is imported into
    // the first-instance owner's encrypted local Qt vault.
    const QUrl endpoint =
        rewardPoolWorkerEndpoint(QStringLiteral("/api/rewards/pool"));
    if (endpoint.isEmpty() || !m_networkAccess) {
        QMessageBox::information(
            this, QStringLiteral("Community reward pool"),
            QStringLiteral("A secure HTTPS reward-pool endpoint is not "
                           "available for the active ForkMesh server."));
        return;
    }

    QNetworkRequest request(endpoint);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-store");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_networkAccess->get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    constexpr qsizetype kMaximumPoolResponse = 1024 * 1024;
    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError networkError = reply->error();
    QByteArray body = reply->read(kMaximumPoolResponse + 1);
    reply->deleteLater();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject resp =
        parseError.error == QJsonParseError::NoError && document.isObject()
            ? document.object()
            : QJsonObject();
    const QString address = resp.value("address").toString().trimmed();
    const bool validAddress =
        forkmesh::rewards::decodeBase58(address, 32).size() == 32;
    const bool externalSigner =
        resp.value("custody").toString() ==
            QLatin1String("external-local-signer") &&
        resp.value("privateKeyStoredByWorker").isBool() &&
        !resp.value("privateKeyStoredByWorker").toBool(true);
    if (networkError != QNetworkReply::NoError || httpStatus != 200 ||
        body.size() > kMaximumPoolResponse || !validAddress ||
        !externalSigner) {
        QMessageBox::information(
            this, QStringLiteral("Community reward pool"),
            QStringLiteral(
                "ForkMesh could not verify a non-custodial community-pool "
                "address from this server. No transfer has been requested."));
        return;
    }

    // Only accept a Solana URI that visibly targets the verified public pool.
    // A bare URI is safer than following an unverified server-supplied target.
    QString uri = resp.value("uri").toString().trimmed();
    if (!uri.startsWith(QStringLiteral("solana:%1").arg(address)))
        uri = QStringLiteral("solana:%1").arg(address);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Community reward pool"));
    auto *l = new QVBoxLayout(&dialog);
    l->setContentsMargins(20, 20, 20, 20);
    l->setSpacing(12);

    auto *intro = new QLabel(
        QStringLiteral(
            "<b>Non-custodial:</b> this is a voluntary transfer from your "
            "self-custodial wallet to the transparent public community reward "
            "pool. ForkMesh never receives your private key. The pool signer "
            "key remains in the first-instance owner's encrypted local desktop "
            "vault; the Worker stores public plans only. Rewards are community "
            "incentives, not investments, and no return is guaranteed."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
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

    const QString network =
        resp.value("network").toString(QStringLiteral("unknown"));
    const QString balance =
        resp.value("balanceSol").toString(QStringLiteral("0"));
    auto *poolState = new QLabel(
        QStringLiteral(
            "Network: %1 · Public on-chain pool balance: %2 SOL\n"
            "Pending rewards are ledger allocations only; funds remain in the "
            "source wallet until the local owner reviews, signs, broadcasts, "
            "and finalizes the exact transfer. Completed rewards have a "
            "finalized on-chain signature.")
            .arg(network.toHtmlEscaped(), balance.toHtmlEscaped()));
    poolState->setObjectName(QStringLiteral("modeHint"));
    poolState->setWordWrap(true);
    l->addWidget(poolState);

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

// The ping feed above the network log: how many lines it shows, and how tall
// it is (adhoc #77). Deliberately small — it is a glance, not a second page.
static constexpr int kLogEventStripLimit = 8;
static constexpr int kLogEventStripRows = 5;

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
    m_logScrollLockButton = new QPushButton(QStringLiteral("Pause scroll"));
    m_logScrollLockButton->setObjectName("ghostButton");
    m_logScrollLockButton->setCheckable(true);
    m_logScrollLockButton->setCursor(Qt::PointingHandCursor);
    m_logScrollLockButton->setToolTip(
        QStringLiteral("Keep the current log position when new entries arrive"));
    setOcticon(m_logScrollLockButton, "stop", 14);
    auto *cloudflareButton = new QPushButton("Cloudflare logs");
    cloudflareButton->setObjectName(
        QStringLiteral("cloudflareWorkerLogsButton"));
    cloudflareButton->setCursor(Qt::PointingHandCursor);
    cloudflareButton->setToolTip(
        QStringLiteral("View the deployed Cloudflare Worker's live logs"));
    setOcticon(cloudflareButton, "cloud", 14);
    connect(cloudflareButton, &QPushButton::clicked, this,
            &MainWindow::showCloudflareWorkerLogs);

    m_settingsLog = new QTextBrowser;
    m_settingsLog->setReadOnly(true);
    m_settingsLog->setObjectName("networkLog");
    m_settingsLog->setOpenExternalLinks(true);
    // Clicks on the leading "add to prompt" plus of an entry are handled in
    // MainWindow::eventFilter before the browser's own anchor activation sees
    // them (adhoc #114); http(s) links in the message body still open normally.
    m_settingsLog->viewport()->installEventFilter(this);
    // No setMaximumBlockCount here: that trims blocks from the *top* of the
    // document, which would silently discard the older segments this view now
    // loads on demand when the user scrolls up (adhoc #15). m_networkLog
    // itself (capped at kNetworkLogLimit) is the real bound on total history.
    connect(m_settingsLog->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MainWindow::onNetworkLogScrolled);
    connect(m_logScrollLockButton, &QPushButton::toggled, this,
            [this](bool locked) {
                m_logScrollLocked = locked;
                m_logScrollLockButton->setText(
                    locked ? QStringLiteral("Resume scroll")
                           : QStringLiteral("Pause scroll"));
                m_logScrollLockButton->setToolTip(
                    locked
                        ? QStringLiteral(
                              "Resume following new log entries at the bottom")
                        : QStringLiteral(
                              "Keep the current log position when new entries arrive"));
                if (!locked && m_settingsLog && m_settingsLog->verticalScrollBar())
                    m_settingsLog->verticalScrollBar()->setValue(
                        m_settingsLog->verticalScrollBar()->maximum());
            });

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
    // chips now, but leave rendering the history itself (the newest
    // kNetworkLogSegmentSize lines; older segments load lazily on scroll) to
    // the first visit of the Log section — it's pure constructor cost for a
    // view most launches never open. Live logSystem() lines still append to
    // the (empty) view immediately; the first visit's rebuild re-renders the
    // latest segment in order, history included.
    m_logFilterCounts.clear();
    for (const QString &line : std::as_const(m_networkLog))
        ++m_logFilterCounts[logBadgeFor(line)];
    rebuildLogFilterButtons();
    m_networkLogViewStale = !m_networkLog.isEmpty();

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_networkLog.clear();
        m_lastLogRenderDate.clear();
        m_logFilter.clear();
        m_logFilterCounts.clear();
        m_logRenderFrom = 0; // nothing left to page back into once cleared
        m_logFilterEmptyNotice = false;
        if (m_settingsLog)
            m_settingsLog->clear();
        saveNetworkLog();          // truncate the on-disk log too
        rebuildLogFilterButtons(); // drop the category chips, re-check "All"
    });

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(label);
    headerRow->addStretch();
    headerRow->addWidget(m_logScrollLockButton);
    headerRow->addWidget(cloudflareButton);
    headerRow->addWidget(clearButton);

    // Every ping this window raises also lands in a compact feed directly
    // above the log, so "what just happened?" is answered without leaving the
    // page or waiting for the toast to reappear (adhoc #77). Double-clicking a
    // line opens the full Pings page.
    auto *eventsLabel = new QLabel(QStringLiteral("RECENT PINGS"));
    eventsLabel->setObjectName("sectionLabel");
    m_logEventList = new QListWidget;
    m_logEventList->setObjectName("logEventList");
    m_logEventList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_logEventList->setUniformItemSizes(true);
    m_logEventList->setFixedHeight(kLogEventStripRows *
                                       m_logEventList->fontMetrics().height() +
                                   12);
    m_logEventList->setToolTip(
        QStringLiteral("The newest pings. Double-click to open the Pings "
                       "page."));
    connect(m_logEventList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { showNotifications(); });

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    layout->addLayout(headerRow);
    layout->addWidget(eventsLabel);
    layout->addWidget(m_logEventList);
    layout->addWidget(filterScroll);
    layout->addWidget(m_settingsLog, 1);
    refreshLogEventList();
    return page;
}

// Repaint the compact ping feed above the network log from the same list the
// Pings page shows, newest first (adhoc #77).
void MainWindow::refreshLogEventList()
{
    if (!m_logEventList)
        return;
    m_logEventList->clear();
    if (m_notifications.isEmpty()) {
        auto *empty = new QListWidgetItem(
            QStringLiteral("No pings yet in this session."));
        empty->setForeground(QColor("#6e7681"));
        empty->setFlags(Qt::NoItemFlags);
        m_logEventList->addItem(empty);
        return;
    }
    const int shown = qMin(int(m_notifications.size()), kLogEventStripLimit);
    for (int index = 0; index < shown; ++index) {
        const AppNotification &notice = m_notifications.at(index);
        QString text =
            QDateTime::fromMSecsSinceEpoch(notice.timestampMs)
                .toString(QStringLiteral("HH:mm:ss")) +
            QStringLiteral("  ") + notice.title.simplified();
        const QString detail = notice.body.simplified();
        if (!detail.isEmpty())
            text += QString::fromUtf8(" \xE2\x80\x94 ") + detail;
        auto *item = new QListWidgetItem(text);
        item->setToolTip(text);
        if (notice.warning)
            item->setForeground(QColor("#f85149"));
        m_logEventList->addItem(item);
    }
}

void MainWindow::showCloudflareWorkerLogs()
{
    QString token =
        m_cloudflareTokenEdit
            ? m_cloudflareTokenEdit->text().trimmed()
            : QString();
    // Fall back to the credential this node already stores for the deploy
    // workflow (Settings > Secrets & Coves) so the viewer does not ask for a
    // second token that authenticates against the same account.
    const QMap<QString, QString> storedVariables = ActionStore::variables();
    bool storedToken = false;
    if (token.isEmpty()) {
        token = forkmesh::control::cloudflareApiTokenFromVariables(
            storedVariables);
        storedToken = !token.isEmpty();
    }
    if (token.isEmpty()) {
        bool accepted = false;
        token = QInputDialog::getText(
                    this, QStringLiteral("Cloudflare Worker logs"),
                    QStringLiteral(
                        "Scoped Cloudflare API token (used for this live "
                        "viewer only):"),
                    QLineEdit::Password, QString(), &accepted)
                    .trimmed();
        if (!accepted || token.isEmpty()) {
            token.fill(QChar(u'\0'));
            token.clear();
            return;
        }
    }

    const QString workerDirectory =
        forkmesh::control::findCloudflareWorkerDirectory(
            QStringLiteral(FORKMESH_SOURCE_DIR),
            QCoreApplication::applicationDirPath());
    const QString npx =
        QStandardPaths::findExecutable(QStringLiteral("npx"));
    QString account =
        m_cloudflareAccountEdit
            ? m_cloudflareAccountEdit->text().trimmed()
            : QString();
    if (account.isEmpty()) {
        account =
            QSettings()
                .value(QStringLiteral("control/cloudflareAccount"))
                .toString()
                .trimmed();
    }
    if (account.isEmpty()) {
        account = forkmesh::control::cloudflareAccountIdFromVariables(
            storedVariables);
    }
    const auto command =
        forkmesh::control::buildCloudflareTailCommand(
            token, account, npx);
    if (workerDirectory.isEmpty() || command.program.isEmpty()) {
        flashMessage(
            workerDirectory.isEmpty()
                ? QStringLiteral(
                      "The installed Cloudflare Worker bundle is incomplete.")
                : QStringLiteral(
                      "Cloudflare live logs require Node.js/npx and a valid "
                      "account ID."),
            true);
        token.fill(QChar(u'\0'));
        token.clear();
        return;
    }
    if (command.arguments.join(QChar(u'\0')).contains(token)) {
        flashMessage(
            QStringLiteral(
                "Refusing an unsafe Worker log command containing a "
                "credential."),
            true);
        token.fill(QChar(u'\0'));
        token.clear();
        return;
    }
    if (m_cloudflareTokenEdit &&
        !m_cloudflareTokenEdit->text().isEmpty()) {
        m_cloudflareTokenEdit->clear();
        m_cloudflareTokenEdit->setPlaceholderText(
            QStringLiteral("token is in the live log viewer only"));
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("cloudflareWorkerLogsDialog"));
    dialog.setWindowTitle(QStringLiteral("Cloudflare Worker live logs"));
    dialog.resize(900, 560);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto *notice = new QLabel(
        storedToken
            ? QStringLiteral(
                  "Read-only live tail for the configured ForkMesh Worker, "
                  "authenticated with the stored CLOUDFLARE_API_TOKEN secret. "
                  "The token stays in this process's memory only and is erased "
                  "when this viewer closes.")
            : QStringLiteral(
                  "Read-only live tail for the configured ForkMesh Worker. The "
                  "API token stays in this process's memory only and is erased "
                  "when this viewer closes."));
    notice->setObjectName(QStringLiteral("modeHint"));
    notice->setWordWrap(true);
    layout->addWidget(notice);

    auto *status = new QLabel(QStringLiteral("Connecting…"));
    status->setObjectName(QStringLiteral("cloudflareWorkerLogsStatus"));
    layout->addWidget(status);

    auto *output = new QPlainTextEdit;
    output->setObjectName(QStringLiteral("cloudflareWorkerLiveLogs"));
    output->setReadOnly(true);
    output->setLineWrapMode(QPlainTextEdit::NoWrap);
    output->document()->setMaximumBlockCount(2500);
    layout->addWidget(output, 1);

    auto *closeButton = new QPushButton(QStringLiteral("Close"));
    closeButton->setObjectName(QStringLiteral("primaryButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, &dialog,
            &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    QProcess process(&dialog);
    process.setWorkingDirectory(workerDirectory);
    process.setProcessEnvironment(command.environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    const auto appendOutput = [&process, output, &token] {
        const QString chunk =
            QString::fromUtf8(process.readAllStandardOutput());
        if (chunk.isEmpty())
            return;
        output->moveCursor(QTextCursor::End);
        output->insertPlainText(
            forkmesh::control::redactProcessOutput(chunk, {token}));
        output->moveCursor(QTextCursor::End);
        output->ensureCursorVisible();
    };
    connect(&process, &QProcess::readyReadStandardOutput, &dialog,
            appendOutput);
    connect(&process, &QProcess::started, &dialog, [status] {
        status->setText(
            QStringLiteral("Connected · waiting for Worker events"));
    });
    connect(
        &process, &QProcess::errorOccurred, &dialog,
        [status](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                status->setText(
                    QStringLiteral(
                        "Could not start npx. Install Node.js to use live "
                        "Worker logs."));
            }
        });
    connect(
        &process, &QProcess::finished, &dialog,
        [status, appendOutput](int exitCode,
                               QProcess::ExitStatus exitStatus) {
            appendOutput();
            status->setText(
                exitStatus == QProcess::NormalExit && exitCode == 0
                    ? QStringLiteral("Log stream ended")
                    : QStringLiteral("Log stream stopped (exit %1)")
                          .arg(exitCode));
        });
    process.start(command.program, command.arguments);
    dialog.exec();

    if (process.state() != QProcess::NotRunning) {
        process.terminate();
        if (!process.waitForFinished(1500)) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
    process.setProcessEnvironment(QProcessEnvironment());
    token.fill(QChar(u'\0'));
    token.clear();
    if (m_cloudflareTokenEdit &&
        m_cloudflareTokenEdit->text().isEmpty()) {
        m_cloudflareTokenEdit->setPlaceholderText(
            QStringLiteral("session-only Cloudflare API token"));
    }
}

QWidget *MainWindow::buildBreadcrumb()
{
    auto *bar = new QWidget;
    bar->setObjectName("breadcrumbBar");

    // --- Relay switcher: just the active relay's favicon (adhoc #91) — the
    // domain and relay count moved into the dropdown it opens (search / switch
    // / add). ----------------------------------------------------------------
    m_relayMenuButton = new QPushButton;
    m_relayMenuButton->setObjectName("relayMenuButton");
    m_relayMenuButton->setCursor(Qt::PointingHandCursor);
    // A 25px favicon (a tenth smaller than the old 28) in a button exactly as
    // wide as an activity-rail item, so the logo paints on the same vertical
    // axis as every rail octicon underneath it. The chrome row drops its left
    // margin to match; see chromeRow in this same function.
    m_relayMenuButton->setIconSize(QSize(25, 25));
    m_relayMenuButton->setFixedWidth(railItemWidth());
    m_relayMenuButton->setToolTip("Switch, search, or add relays");
    connect(m_relayMenuButton, &QPushButton::clicked, this,
            &MainWindow::showRelayMenu);

    // Red dot pinned over the favicon while a freshly launched instance waits
    // to be linked (adhoc #97), with the Approve button that opens the join
    // dialog right beside it. Both stay hidden until the signed heartbeat
    // reply reports a pending join request for this admin.
    m_relayJoinDot = new QLabel(m_relayMenuButton);
    m_relayJoinDot->setObjectName(QStringLiteral("relayJoinDot"));
    m_relayJoinDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_relayJoinDot->setFixedSize(10, 10);
    m_relayJoinDot->hide();
    m_relayJoinApproveButton = new QPushButton(QStringLiteral("Approve"));
    m_relayJoinApproveButton->setObjectName(
        QStringLiteral("relayJoinApproveButton"));
    m_relayJoinApproveButton->setCursor(Qt::PointingHandCursor);
    m_relayJoinApproveButton->hide();
    connect(m_relayJoinApproveButton, &QPushButton::clicked, this,
            &MainWindow::showRelayJoinApprovalDialog);

    // Connection speed as a colour, pinned above the instance logo (adhoc
    // #124): the radar dish that used to carry this on the right of the chrome
    // line is gone, so the link's health now rides the instance it belongs to.
    // The probe itself is still driven by m_relayLatencyTimer.
    m_relaySpeedDot = new RelaySpeedDot(m_relayMenuButton);

    // Node switcher, to the right of the relay switcher: "node ▾ count".
    m_nodeMenuButton = new QPushButton;
    m_nodeMenuButton->setObjectName("nodeMenuButton");
    m_nodeMenuButton->setCursor(Qt::PointingHandCursor);
    m_nodeMenuButton->setToolTip("Pick a node to view its repositories");
    connect(m_nodeMenuButton, &QPushButton::clicked, this, &MainWindow::showNodeMenu);

    // The public wallet balance heads the top-chrome line right after the relay
    // switcher. The "user/node" caption that used to precede it was dropped
    // (adhoc #42) — the avatar already says who is signed in.
    m_navSolanaBalance = new QLabel(QStringLiteral("0.000000000 SOL"));
    m_navSolanaBalance->setObjectName("navSolanaBalance");
    m_navSolanaBalance->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_navSolanaBalance->setTextFormat(Qt::RichText);
    m_navSolanaBalance->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    m_navSolanaBalance->setOpenExternalLinks(false);
    m_navSolanaBalance->setCursor(Qt::PointingHandCursor);
    m_navSolanaBalance->setToolTip(
        "Your Solana wallet balance \xE2\x80\x94 click to switch "
        "currency (SOL / USD / INR).\n"
        "Non-custodial payout address: this client only shares its public "
        "address; its private key stays in the wallet you control.");
    // Clicking the balance itself cycles its display currency, so the control
    // sits right on the value instead of needing a separate swap icon.
    m_navSolanaBalance->installEventFilter(this);
    connect(m_navSolanaBalance, &QLabel::linkActivated, this,
            [this](const QString &) {
                QUrl url = catalogApiUrl();
                url.setPath(QStringLiteral("/dashboard/settings"));
                url.setQuery(QString());
                url.setFragment(QString());
                QDesktopServices::openUrl(url);
            });

    // The reward-availability toggle (online/offline switch, status line, uptime)
    // used to live here beside the balance; it's now built in
    // buildNodeProfilePanel(), right under Mirror reward settings, as a clear
    // on/off switch for the whole node rather than a small top-bar pill.

    // Tiny Claude Code usage chart that rides beside the wallet balance/avatar (issue
    // #266): a 5-hour and a weekly horizontal gauge. Seed it from the last cached
    // utilisation so it renders immediately; from there it only updates when the
    // user hovers the chart to check it (adhoc #20) — no background poll and no
    // other trigger keeps it current between those.
    // Three bars: 5-hour, weekly, and the account's Fable weekly window
    // (adhoc #96).
    auto *tokenUsage = new TokenUsageMiniChart(
        QStringLiteral("Claude Code usage"), /*remainingMode=*/false,
        /*windows=*/3);
    m_navTokenUsage = tokenUsage;
    // This hover is also the only place that re-fetches the live claude-code
    // model list (GET /v1/models, adhoc #41) — everywhere else that touches a
    // model combo just applies whatever's already cached. The refresh flashes a
    // green/red box around the chart when it lands (adhoc #96).
    tokenUsage->onHover = [this] {
        refreshClaudeCodeUsage(/*fromHover=*/true);
        refreshClaudeModelCombo();
    };
    {
        QSettings settings;
        auto restore = [&](bool weekly, const QString &key) {
            if (settings.contains(key))
                tokenUsage->setUsage(weekly, settings.value(key).toInt());
        };
        restore(false, kClaudeUsage5hPctSetting);
        restore(true, kClaudeUsageWeekPctSetting);
        if (settings.contains(kClaudeUsageFablePctSetting))
            tokenUsage->setUsage(TokenUsageMiniChart::Fable,
                                 settings.value(kClaudeUsageFablePctSetting).toInt());
        // Reset countdown (issue #50): the cached instant is wall-clock, so derive
        // the remaining time relative to now; a window that already elapsed shows
        // no countdown until the next poll refreshes it.
        auto restoreReset = [&](TokenUsageMiniChart::Window window,
                                const QString &key) {
            if (!settings.contains(key))
                return;
            const qint64 remaining = settings.value(key).toLongLong() -
                                     QDateTime::currentMSecsSinceEpoch();
            if (remaining > 0)
                tokenUsage->setReset(window, humanizeRemaining(remaining));
        };
        restoreReset(TokenUsageMiniChart::FiveHour, kClaudeUsage5hResetSetting);
        restoreReset(TokenUsageMiniChart::Weekly, kClaudeUsageWeekResetSetting);
        restoreReset(TokenUsageMiniChart::Fable, kClaudeUsageFableResetSetting);
    }
    // Codex rides beside Claude Code. App-server updates replace the local
    // countdown estimate with the account's live utilization and reset time.
    // Two bars only — Codex has no per-model weekly window.
    auto *codexUsage = new TokenUsageMiniChart(
        QStringLiteral("Codex usage remaining"), /*remainingMode=*/true,
        /*windows=*/2);
    m_navCodexUsage = codexUsage;
    // The Codex figures are computed locally, so the hover always "succeeds";
    // the green box still confirms the reading is fresh (adhoc #96).
    codexUsage->onHover = [this] {
        refreshCodexUsageRemaining();
        flashUsageChart(m_navCodexUsage, true);
    };
    refreshCodexUsageRemaining();

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
    m_repoViewButton = new ActivityRailButton(QStringLiteral("code"),
                                              QStringLiteral("Repo"));
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

    // Repos: network-wide catalog of repositories known by the active relay.
    m_reposNavButton = new ActivityRailButton(QStringLiteral("repo"),
                                              QStringLiteral("Repos"));
    m_reposNavButton->setObjectName("topNavButton");
    m_reposNavButton->setCheckable(true);
    m_reposNavButton->setCursor(Qt::PointingHandCursor);
    m_reposNavButton->setToolTip(QStringLiteral("Browse all repositories on the network"));
    setOcticon(m_reposNavButton, "repo", 16);
    m_navGroup->addButton(m_reposNavButton, kNetworkReposSectionIndex);
    connect(m_reposNavButton, &QPushButton::clicked, this, [this] {
        showSection(kNetworkReposSectionIndex);
    });

    m_tasksNavButton = new ActivityRailButton(QStringLiteral("list-unordered"),
                                              QStringLiteral("Tasks"));
    m_tasksNavButton->setObjectName("organizationTasksNavButton");
    m_tasksNavButton->setCheckable(true);
    m_tasksNavButton->setCursor(Qt::PointingHandCursor);
    m_tasksNavButton->setToolTip(QStringLiteral("Organization tasks"));
    setOcticon(m_tasksNavButton, "list-unordered", 16);
    m_navGroup->addButton(
        m_tasksNavButton, kOrganizationTasksSectionIndex);
    connect(m_tasksNavButton, &QPushButton::clicked, this, [this] {
        showSection(kOrganizationTasksSectionIndex);
    });

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

    // Pings bell: a regular rail destination (adhoc #117 made it the same
    // icon-over-caption item as the rest). The pending count rides the bell's
    // corner as a red "needs you" badge and the glyph tints amber while
    // anything waits — both painted by ActivityRailButton, driven from
    // updateNotificationButton().
    auto *notificationRailButton =
        new ActivityRailButton(QStringLiteral("bell"), QStringLiteral("Pings"));
    notificationRailButton->setBadgeUrgent(true);
    m_notificationButton = notificationRailButton;
    m_notificationButton->setObjectName("topNavButton");
    m_notificationButton->setToolTip("Pings");
    m_navGroup->addButton(m_notificationButton, 3); // section 3: Notifications
    connect(m_notificationButton, &QPushButton::clicked, this,
            &MainWindow::showNotifications);

    // Compact success/failure toast. Built here with the rest of the chrome, but
    // it is docked into the footer's mini-log panel (see buildNetworkLogDock),
    // pinned to the top of that panel: messages belong with the log they explain,
    // not in the crowded window-chrome line (adhoc #14).
    m_topMessage = new QLabel;
    m_topMessage->setObjectName("topMessageText");
    m_topMessage->setTextFormat(Qt::RichText);
    // Left-align the text itself: the toast as a whole still sits centered in the
    // bar (via the stretches around it below), but when the window is too narrow
    // to fit the full one-liner, Qt clips the label rather than eliding it, and a
    // centered label clips from both ends — hiding the start of the message where
    // the useful detail is. Left alignment keeps that start visible.
    m_topMessage->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The pill's overall width is capped on m_topMessageContainer below (which
    // also holds the Expand/Copy/✕ buttons); the label itself just fills it. The
    // text is elided to one line in flashMessage regardless.
    // Selectable like before, plus clickable links (e.g. the "jump to agent" toast).
    m_topMessage->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                          Qt::LinksAccessibleByMouse);
    // ...but never at the cost of the caret: setTextInteractionFlags() bumps a
    // QLabel to ClickFocus, and the toast now sits right beside the agent prompt,
    // so selecting an error would silently steal the keyboard from whatever the
    // user was typing. Mouse selection and link clicks still work without focus.
    m_topMessage->setFocusPolicy(Qt::NoFocus);
    // A one-line QLabel reports its whole text width as its minimum, which would
    // let a long error force the mini-log panel — and with it the window — wider.
    // An explicit minimum overrides that hint, so the pill shrinks with the panel
    // and clips (from the right, per the alignment above) instead.
    m_topMessage->setMinimumWidth(1);
    connect(m_topMessage, &QLabel::linkActivated, this, [this](const QString &href) {
        if (href.startsWith(QLatin1String("fm:agent:"))) {
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
    m_topMessageCopy->setFocusPolicy(Qt::NoFocus); // a toast never grabs the keyboard
    setOcticon(m_topMessageCopy, "copy", 14);
    m_topMessageCopy->hide();
    connect(m_topMessageCopy, &QPushButton::clicked, this, [this] {
        if (!m_topMessageRaw.isEmpty())
            QGuiApplication::clipboard()->setText(m_topMessageRaw);
        advanceTopMessageQueue(); // move on to the next queued error, if any
    });
    // A plain "x" to dismiss an error toast without copying it — the octicon
    // SVG, like the Copy/Expand glyphs beside it, not a text glyph.
    m_topMessageClose = new QPushButton;
    m_topMessageClose->setObjectName("ghostButton");
    setOcticon(m_topMessageClose, "x", 14);
    m_topMessageClose->setCursor(Qt::PointingHandCursor);
    m_topMessageClose->setToolTip(QStringLiteral("Dismiss"));
    m_topMessageClose->setFocusPolicy(Qt::NoFocus);
    m_topMessageClose->hide();
    connect(m_topMessageClose, &QPushButton::clicked, this,
            [this] { dismissTopMessage(); }); // always fully close, even if another error is queued

    // Shown beside the toast when a message is too long to fit on one line.
    // Clicking it expands the full message in place (wrapped, growing the toast)
    // and toggles back to the elided one-liner — no modal pops up.
    m_topMessageExpand = new QPushButton;
    m_topMessageExpand->setObjectName("ghostButton");
    m_topMessageExpand->setCursor(Qt::PointingHandCursor);
    m_topMessageExpand->setToolTip(QStringLiteral("Show the full message"));
    m_topMessageExpand->setFocusPolicy(Qt::NoFocus);
    setOcticon(m_topMessageExpand, "chevron-down", 14);
    m_topMessageExpand->hide();
    connect(m_topMessageExpand, &QPushButton::clicked, this, [this] {
        m_topMessageExpanded = !m_topMessageExpanded;
        renderTopMessage();
        // Keep the live countdown suffix if a success toast is still ticking.
        if (m_topMessageTimer && m_topMessageTimer->isActive())
            renderTopMessageCountdown();
    });

    // Wrap the text and its Expand/Copy/✕ affordances in one bordered pill so
    // they render (and hit-test) as a single contained unit instead of the
    // buttons floating loose beside the box, which could leave them squeezed
    // to almost nothing — and effectively unclickable — once the rest of the
    // crowded top bar ran short on room (adhoc #16).
    m_topMessageContainer = new QFrame;
    m_topMessageContainer->setObjectName("topMessage");
    m_topMessageContainer->setFocusPolicy(Qt::NoFocus);
    // The pill fills the mini-log panel it now lives in, so a long error gets
    // every pixel the log has; the cap only stops it sprawling on a very wide
    // window. It can never widen the window itself — see the label's minimum above.
    m_topMessageContainer->setMaximumWidth(900);
    m_topMessageContainer->setSizePolicy(QSizePolicy::Preferred,
                                         QSizePolicy::Fixed);
    auto *topMessageRow = new QHBoxLayout(m_topMessageContainer);
    topMessageRow->setContentsMargins(12, 2, 6, 2);
    topMessageRow->setSpacing(4);
    topMessageRow->addWidget(m_topMessage, 1);
    topMessageRow->addWidget(m_topMessageExpand);
    topMessageRow->addWidget(m_topMessageCopy);
    topMessageRow->addWidget(m_topMessageClose);
    m_topMessageContainer->hide();

    // The expanded full text lives in this floating panel, parented to the window
    // (not to any layout) and raised above everything when shown. Revealing it
    // therefore overlays the UI on top instead of growing the inline toast, so it
    // never shifts the top bar or the layout below it. See renderTopMessage.
    m_topMessageOverlay = new QFrame(this);
    m_topMessageOverlay->setObjectName("topMessageOverlay");
    m_topMessageOverlay->setFocusPolicy(Qt::NoFocus);
    auto *overlayLayout = new QVBoxLayout(m_topMessageOverlay);
    overlayLayout->setContentsMargins(12, 10, 12, 10);
    m_topMessageOverlayText = new QLabel;
    m_topMessageOverlayText->setObjectName("topMessageOverlayText");
    m_topMessageOverlayText->setTextFormat(Qt::RichText);
    m_topMessageOverlayText->setWordWrap(true);
    m_topMessageOverlayText->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                     Qt::LinksAccessibleByMouse);
    m_topMessageOverlayText->setFocusPolicy(Qt::NoFocus);
    overlayLayout->addWidget(m_topMessageOverlayText);
    m_topMessageOverlay->hide();

    // User avatar, the rail's bottom-most Account item. Clicking it opens
    // Settings for the current user. Sized to sit flush with the rail's 20px
    // octicons (adhoc #117) rather than dwarfing them.
    m_userAvatarNavButton = new QPushButton;
    m_userAvatarNavButton->setObjectName("serverFooterButton");
    m_userAvatarNavButton->setCursor(Qt::PointingHandCursor);
    m_userAvatarNavButton->setFixedSize(26, 26);
    m_userAvatarNavButton->setIconSize(QSize(24, 24));
    m_userAvatarNavButton->setToolTip("Settings");
    connect(m_userAvatarNavButton, &QPushButton::clicked, this, [this] {
        showSection(1);
        // Land on Settings > Profile, so clicking the avatar still shows your
        // own profile the way it did before it became a tab (adhoc #274).
        if (m_settingsTabs && m_profileSettingsTabIndex >= 0)
            m_settingsTabs->setCurrentIndex(m_profileSettingsTabIndex);
    });
    updateUserSwitcher();
    updateAvatarButton();
    updateAdminCrownBadge();

    // Connection status dot, overlaid on the bottom-right of the (now sole)
    // avatar. It's purely decorative (clicks fall through to the avatar); the
    // live status text lives in the dot's tooltip, set by updateConnectionStatus.
    m_connectionDot = new QLabel(m_userAvatarNavButton);
    m_connectionDot->setObjectName("connectionDot");
    m_connectionDot->setFixedSize(10, 10);
    m_connectionDot->setAttribute(Qt::WA_TransparentForMouseEvents);
    // On the circular picture the corner is empty, so the dot sits on the rim
    // to read as part of it.
    m_connectionDot->move(26 - 10, 26 - 10);
    m_connectionDot->raise();

    // Admin crown badge, overlaid on the top-left of the same avatar (mirroring
    // the connection dot's bottom-right corner). A gold-tinted SVG like every
    // other glyph in the app (adhoc #117 retired the emoji). Hidden unless this
    // node is an admin; updateAdminCrownBadge() keeps it in sync with m_isAdmin.
    m_adminCrownBadge = new QLabel(m_userAvatarNavButton);
    m_adminCrownBadge->setObjectName("adminCrownBadge");
    m_adminCrownBadge->setFixedSize(12, 12);
    m_adminCrownBadge->setAlignment(Qt::AlignCenter);
    m_adminCrownBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_adminCrownBadge->setPixmap(tintedOcticonPixmap(
        QStringLiteral("crown"), QColor(QStringLiteral("#e3b341")), 12));
    m_adminCrownBadge->move(-2, -2);
    m_adminCrownBadge->raise();
    m_adminCrownBadge->hide();

    // Captions for the top-bar dropdowns.
    auto makeCaption = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setObjectName("navCaption");
        return l;
    };
    m_nodeLabel = makeCaption(QStringLiteral("Node"));
    m_repoLabel = makeCaption(QStringLiteral("Repo"));

    // Agents: a shortcut into the current repo's Agents tab (adhoc #194), not a
    // section of its own — it just jumps via openAgentsOverview() the same way
    // the footer "Agents:" label does. Checkable to show when the Agents tab is
    // active (adhoc #201). It used to be a "magical" gradient pill on the
    // window-chrome line; adhoc #70 made it a regular entry heading the app
    // navigation rail, badged with the number of *running* sessions, so it
    // reads like every other destination. Its fleet matrix stays on the chrome
    // line, where the horizontal room for it is.
    m_agentsNavButton = new ActivityRailButton(QStringLiteral("star"),
                                               QStringLiteral("Agents"));
    m_agentsNavButton->setObjectName("topNavButton");
    m_agentsNavButton->setCheckable(true);
    m_agentsNavButton->setCursor(Qt::PointingHandCursor);
    m_agentsNavButton->setToolTip(QStringLiteral("Agents"));
    setOcticon(m_agentsNavButton, "star", 16);
    connect(m_agentsNavButton, &QPushButton::clicked, this,
            &MainWindow::openAgentsOverview);

    // One tiny square per agent session: the whole fleet's status as a matrix,
    // with running sessions sweeping in time with their live output. Populated
    // (and kept current) by refreshAgentDotMatrix().
    m_agentDotMatrix = new AgentDotMatrix;
    m_agentDotMatrix->onDotClicked = [this](int sessionId) {
        if (sessionId > 0)
            switchToAgentsTab(sessionId);
        else
            openAgentsOverview();
    };

    // Immediately right of the fleet, behind a faint divider: one dot per node
    // on the network (adhoc #124), so the machines read on the same line as the
    // agents. Kept current by refreshNodeDotMatrix().
    m_chromeDotDivider = new QWidget;
    m_chromeDotDivider->setObjectName(QStringLiteral("chromeDotDivider"));
    // A bare QWidget ignores a stylesheet background unless it opts in.
    m_chromeDotDivider->setAttribute(Qt::WA_StyledBackground, true);
    m_chromeDotDivider->setFixedWidth(1);
    m_chromeDotDivider->setFixedHeight(15); // a hairline inside the 21px grids
    m_chromeDotDivider->hide();
    m_nodeDotMatrix = new NodeDotMatrix;
    m_nodeDotMatrix->onDotClicked = [this](const QString &node) {
        showNetworkTab(kNetworkNodesTab);
        if (node.isEmpty() || !m_nodesTable)
            return; // past the last dot: the Nodes list itself is the answer
        for (int row = 0; row < m_nodesTable->rowCount(); ++row) {
            const QTableWidgetItem *item = m_nodesTable->item(row, 0);
            if (!item || item->data(Qt::UserRole).toString() != node)
                continue;
            m_nodesTable->selectRow(row);
            showNodeDetailForRow(row);
            break;
        }
    };

    // Immediately right of the node dots: the most recent action runs and their
    // status (adhoc #70), so CI reads on the same line as the agents. Kept
    // current by refreshActionRunStrip().
    m_actionRunStrip = new ActionRunStrip;
    m_actionRunStrip->onCellClicked = [this](int runId) {
        // Past the last square there is nothing specific to open, so fall back
        // to the newest run — which is what the strip is about.
        if (runId <= 0 && !m_actionRuns.isEmpty())
            runId = m_actionRuns.first().id;
        if (runId > 0)
            openActionRunFromNotification(runId);
    };
    // Runs are loaded before the chrome exists, so paint the strip once here
    // rather than waiting for the next run-state change to reach it.
    refreshActionRunStrip();

    // Chat: its own top-level section (m_sectionStack index 2). The unread
    // count rides the icon's corner as a red "needs you" badge, painted by
    // ActivityRailButton itself (updateChatButton feeds it the tally).
    auto *chatRailButton = new ActivityRailButton(QStringLiteral("comment"),
                                                  QStringLiteral("Chat"));
    chatRailButton->setBadgeUrgent(true);
    m_chatButton = chatRailButton;
    m_chatButton->setObjectName("topNavButton");
    m_chatButton->setToolTip(QStringLiteral("Chat"));
    m_navGroup->addButton(m_chatButton, 2); // section 2: Chat
    connect(m_chatButton, &QPushButton::clicked, this, &MainWindow::showChatView);

    // Settings: its own top-level section (m_sectionStack index 1), a regular
    // rail destination styled like every other item (adhoc #117).
    m_settingsNavButton = new ActivityRailButton(QStringLiteral("gear"),
                                                 QStringLiteral("Settings"));
    m_settingsNavButton->setObjectName("topNavButton");
    m_settingsNavButton->setToolTip(QStringLiteral("Settings"));
    m_navGroup->addButton(m_settingsNavButton, 1); // section 1: Settings
    connect(m_settingsNavButton, &QPushButton::clicked, this,
            [this] { showSection(1); });

    // Full Log: a normal activity-rail destination. The mini log stays clean and
    // entirely devoted to output instead of carrying a floating navigation
    // button over its text.
    m_logNavButton = new ActivityRailButton(QStringLiteral("list-unordered"),
                                            QStringLiteral("Log"));
    m_logNavButton->setObjectName("topNavButton");
    m_logNavButton->setCheckable(true);
    m_logNavButton->setCursor(Qt::PointingHandCursor);
    m_logNavButton->setToolTip(QStringLiteral("Network and application log"));
    setOcticon(m_logNavButton, "list-unordered", 16);
    m_navGroup->addButton(m_logNavButton, 4);
    connect(m_logNavButton, &QPushButton::clicked, this,
            [this] { showSection(4); });

    // Control node: this desktop's operational surface for local mirrors,
    // permissions, keys, wallet public address, Cloudflare, and connected hosts.
    m_controlNodeNavButton = new ActivityRailButton(QStringLiteral("server"),
                                                    QStringLiteral("Control"));
    m_controlNodeNavButton->setObjectName("topNavButton");
    m_controlNodeNavButton->setCheckable(true);
    m_controlNodeNavButton->setCursor(Qt::PointingHandCursor);
    m_controlNodeNavButton->setToolTip(
        QStringLiteral("Control node - mirrors, health, keys, Cloudflare and hosts"));
    setOcticon(m_controlNodeNavButton, "server", 16);
    m_navGroup->addButton(m_controlNodeNavButton, kControlNodeSectionIndex);
    connect(m_controlNodeNavButton, &QPushButton::clicked, this,
            [this] { showSection(kControlNodeSectionIndex); });

    // Network: Relays, Nodes and Hosts as tabs (adhoc #54) alongside the
    // websocket / Durable Object diagnostics and the outbound firewall. The
    // three used to be rail buttons of their own (sections 7, 8 and 13); those
    // section indexes still resolve, they just land on the matching tab.
    m_networkNavButton = new ActivityRailButton(QStringLiteral("workflow"),
                                                QStringLiteral("Network"));
    m_networkNavButton->setObjectName("topNavButton");
    m_networkNavButton->setCheckable(true);
    m_networkNavButton->setCursor(Qt::PointingHandCursor);
    m_networkNavButton->setToolTip(
        QString::fromUtf8("Network \xE2\x80\x94 relays, nodes, hosts, websocket "
                          "and Durable Object diagnostics"));
    setOcticon(m_networkNavButton, "workflow", 16);
    m_navGroup->addButton(m_networkNavButton, kNetworkDiagnosticsSectionIndex);
    connect(m_networkNavButton, &QPushButton::clicked, this,
            [this] { showSection(kNetworkDiagnosticsSectionIndex); });

    // Small, icon-only rebuild+restart button, right-aligned under the avatar on
    // the section-nav row. Hidden unless opted in via Settings (off by default);
    // it's a dev-iteration shortcut for the same fast rebuild as the profile panel.
    m_navRebuildButton = new QPushButton;
    m_navRebuildButton->setObjectName("topNavButton");
    m_navRebuildButton->setCursor(Qt::PointingHandCursor);
    m_navRebuildButton->setToolTip(
        QString::fromUtf8("Rebuild & restart \xE2\x80\x94 fast local rebuild, "
                          "then relaunch"));
    m_navRebuildButton->setFixedSize(30, 30);
    setOcticon(m_navRebuildButton, "sync", 14);
    connect(m_navRebuildButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_navRebuildButton); quickRebuildRestart(); });

    // "Log in / Sign up" pill (adhoc #115). The old first-run screen that asked
    // for a username and a relay host is gone — the app opens straight into the
    // shell — so this is what a user who hasn't attached a forkmesh.com account
    // clicks. updateSignInButton() hides it the moment one is attached.
    m_navSignInButton = new QPushButton(QStringLiteral("Log in / Sign up"));
    m_navSignInButton->setObjectName("primaryButton");
    m_navSignInButton->setCursor(Qt::PointingHandCursor);
    m_navSignInButton->setToolTip(
        QStringLiteral("Attach this machine to your ForkMesh account, or create "
                       "one on forkmesh.com"));
    setOcticon(m_navSignInButton, "sign-in", 14);
    m_navSignInButton->hide();
    connect(m_navSignInButton, &QPushButton::clicked, this,
            &MainWindow::showSignInMenu);

    // Screenshot rail item: drag a region anywhere on screen and it lands in
    // the prompt as an attachment. A one-shot action, so it never stays checked.
    m_navScreenshotButton = new ActivityRailButton(QStringLiteral("screen-full"),
                                                   QStringLiteral("Capture"));
    m_navScreenshotButton->setCheckable(false);
    m_navScreenshotButton->setObjectName("topNavButton");
    m_navScreenshotButton->setToolTip(
        QString::fromUtf8("Screenshot a region \xE2\x80\x94 drag a square anywhere on "
                          "screen and it's attached to your prompt"));
    connect(m_navScreenshotButton, &QPushButton::clicked, this,
            &MainWindow::captureScreenRegion);

    // Resize rail item below the screenshot one: snap the window down to
    // a common minimal screen size (1280x720), so it's quick to preview how
    // ForkMesh looks on a smaller display before filing a UI bug. Also a
    // one-shot action, so it never stays checked.
    m_navResizeButton = new ActivityRailButton(QStringLiteral("device-desktop"),
                                               QStringLiteral("Resize"));
    m_navResizeButton->setCheckable(false);
    m_navResizeButton->setObjectName("topNavButton");
    m_navResizeButton->setToolTip(
        QString::fromUtf8("Resize to 1280\xC3\x97" "720 \xE2\x80\x94 a common "
                          "minimal screen size, handy for previewing smaller "
                          "displays"));
    connect(m_navResizeButton, &QPushButton::clicked, this, [this] {
        if (isMaximized())
            showNormal();
        resize(1280, 720);
    });

    // UI-stall indicator (adhoc #117/#145): an octicon that sits beside the
    // CPU/MEM/DISK sparklines on the window-chrome line and shows the count of
    // detected UI stalls. Click drafts a "fix these stalls" prompt in the
    // composer (adhoc #73); right-click still opens the read-only details.
    m_footerDiagnostics = new QPushButton;
    m_footerDiagnostics->setObjectName("footerDiagnostics");
    m_footerDiagnostics->setFlat(true);
    m_footerDiagnostics->setCursor(Qt::PointingHandCursor);
    m_footerDiagnostics->setToolTip(
        "UI-stall diagnostics: any freezes long enough to trip the Wait/Kill "
        "prompt land here. Click to draft a fix-it prompt in the composer; "
        "right-click for the recorded stall details.");
    m_footerDiagnostics->setStyleSheet(
        "QPushButton#footerDiagnostics{color:#d29922;border:none;background:transparent;"
        "font-size:10px;padding:0 3px;spacing:2px;}"
        "QPushButton#footerDiagnostics:hover{color:#e6edf3;}");
    m_footerDiagnostics->setFixedHeight(18);
    setOcticon(m_footerDiagnostics, QStringLiteral("device-desktop"), 14);
    connect(m_footerDiagnostics, &QPushButton::clicked, this,
            &MainWindow::sendStallReportToComposer);
    m_footerDiagnostics->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_footerDiagnostics, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &) { showDiagnosticsDialog(); });

    // Three little button-sized squares on the window-chrome line, each plotting
    // one resource — this app's CPU, the host's memory and its disk — as a moving
    // sparkline fed one sample a second by updateFooterDiagnostics. Clicking the
    // CPU or DISK square opens the same diagnostics dialog as the glyph; MEM
    // opens the high-memory process panel.
    auto *cpuChart = new ResourceSparkline(QStringLiteral("CPU"));
    auto *memChart = new ResourceSparkline(QStringLiteral("MEM"));
    auto *diskChart = new ResourceSparkline(QStringLiteral("DISK"));
    for (ResourceSparkline *chart : {cpuChart, diskChart})
        chart->onClicked = [this] { showDiagnosticsDialog(); };
    // The memory square goes straight to the culprit list instead: that panel is
    // what you want when the MEM curve spikes (adhoc #46).
    memChart->onClicked = [this] { showHighMemoryProcessPanel(); };
    m_cpuChart = cpuChart;
    m_memChart = memChart;
    m_diskChart = diskChart;

    auto *layout = new QVBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *appVersionLabel = new QLabel(QStringLiteral("v" FORKMESH_VERSION));
    appVersionLabel->setObjectName("chromeVersionLabel");
    appVersionLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    auto *chrome = new WindowChromeBar;
    auto *chromeRow = new QHBoxLayout(chrome);
    // No left margin: the relay favicon is a rail-item-wide button, so starting
    // the row at the window edge lines the logo up with the activity rail's
    // icons directly below it (this replaces the old 24px inset from adhoc #91).
    chromeRow->setContentsMargins(0, 0, 8, 0);
    chromeRow->setSpacing(8);
    // The instance/relay switcher heads the edge-to-edge chrome, with the public
    // SOL balance immediately to its right. The live fleet matrix sits in this
    // same left-hand group (adhoc #42), so it reads on the same left edge as the
    // balance instead of drifting with the search box, and the recent action
    // runs follow it (adhoc #70). The Agents button that used to head this group
    // is now a regular rail entry.
    chromeRow->addWidget(m_relayMenuButton);
    // Logged-out only: the sign-in pill sits immediately after the relay switcher
    // so it is the first thing on the bar that isn't chrome, and it lives in the
    // left-hand group because that group never scrolls out of a narrow window.
    chromeRow->addWidget(m_navSignInButton);
    chromeRow->addWidget(m_relayJoinApproveButton);
    auto *identityBalanceRow = new QHBoxLayout;
    identityBalanceRow->setContentsMargins(0, 0, 0, 0);
    identityBalanceRow->setSpacing(8);
    identityBalanceRow->addWidget(m_navSolanaBalance);
    identityBalanceRow->addWidget(m_agentDotMatrix);
    identityBalanceRow->addWidget(m_chromeDotDivider, 0, Qt::AlignVCenter);
    identityBalanceRow->addWidget(m_nodeDotMatrix);
    identityBalanceRow->addWidget(m_actionRunStrip);
    chromeRow->addLayout(identityBalanceRow);
    chromeRow->addStretch();

    auto *searchCluster = new QWidget;
    auto *searchClusterRow = new QHBoxLayout(searchCluster);
    searchClusterRow->setContentsMargins(0, 0, 0, 0);
    searchClusterRow->setSpacing(8);
    searchClusterRow->addWidget(createNavHistoryButtons());
    searchClusterRow->addWidget(createGlobalSearchBox());
    chromeRow->addWidget(searchCluster, 0, Qt::AlignCenter);
    chromeRow->addStretch();
    // The toast used to sit here; it now docks at the top of the footer's
    // mini-log panel (buildNetworkLogDock), beside the lines it explains.
    // The relay radar used to sit here too (adhoc #87); it is gone (adhoc
    // #124) — its colour moved to the dot above the instance logo and its
    // node blips to the node dots beside the agent fleet.
    // Live CPU/MEM/DISK sparklines, moved up onto the window-chrome line next
    // to the minimize/maximize/close buttons (adhoc #33).
    chromeRow->addWidget(cpuChart);
    chromeRow->addWidget(memChart);
    chromeRow->addWidget(diskChart);
    // Compact diagnostics stack: the stall indicator stays high on the chrome
    // line, its bare version number sits directly beneath it, and the opt-in
    // restart action is immediately to the right.
    auto *diagnosticsStack = new QWidget;
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsStack);
    diagnosticsLayout->setContentsMargins(0, 1, 0, 1);
    diagnosticsLayout->setSpacing(0);
    diagnosticsLayout->addWidget(m_footerDiagnostics, 0, Qt::AlignHCenter);
    diagnosticsLayout->addWidget(appVersionLabel, 0, Qt::AlignHCenter);
    chromeRow->addWidget(diagnosticsStack, 0, Qt::AlignVCenter);
    chromeRow->addWidget(m_navRebuildButton, 0, Qt::AlignVCenter);
    chromeRow->addSpacing(8);

    auto makeWindowButton = [this](QStyle::StandardPixmap icon, const QString &tip) {
        auto *button = new QPushButton;
        button->setObjectName(QStringLiteral("windowChromeButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(32, 30);
        button->setToolTip(tip);
        button->setIcon(style()->standardIcon(icon));
        button->setIconSize(QSize(14, 14));
        return button;
    };
    auto *minimizeButton = makeWindowButton(QStyle::SP_TitleBarMinButton,
                                            QStringLiteral("Minimize"));
    connect(minimizeButton, &QPushButton::clicked, this, &MainWindow::showMinimized);
    auto *maximizeButton = makeWindowButton(QStyle::SP_TitleBarMaxButton,
                                            QStringLiteral("Maximize / restore"));
    connect(maximizeButton, &QPushButton::clicked, this, [this] {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });
    auto *closeButton = makeWindowButton(QStyle::SP_TitleBarCloseButton,
                                         QStringLiteral("Close"));
    closeButton->setObjectName(QStringLiteral("windowChromeCloseButton"));
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::close);
    chromeRow->addWidget(minimizeButton);
    chromeRow->addWidget(maximizeButton);
    chromeRow->addWidget(closeButton);
    chrome->setMinimumWidth(0);
    chrome->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *chromeScroll = new QScrollArea;
    chromeScroll->setObjectName(QStringLiteral("topChromeScroll"));
    chromeScroll->setWidget(chrome);
    chromeScroll->setWidgetResizable(true);
    chromeScroll->setFrameShape(QFrame::NoFrame);
    chromeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    chromeScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chromeScroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    chromeScroll->setMinimumWidth(0);
    chromeScroll->setFixedHeight(54);
    chromeScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(chromeScroll);

    // The node switcher was retired from the global header. Keep its object
    // parented for the existing node-selection code paths, but do not render it;
    // Nodes are reached from the rail and repository selection lives in detail.
    m_nodeLabel->setParent(bar);
    m_nodeLabel->hide();
    m_nodeMenuButton->setParent(bar);
    m_nodeMenuButton->hide();
    m_repoLabel->setParent(bar);
    m_repoLabel->hide();
    m_breadcrumb->setParent(bar);
    m_breadcrumb->hide();

    // Home/Code is the initial section, so show its nav button selected up front.
    m_repoViewButton->setChecked(true);

    updateBreadcrumb();
    updateConnectionStatus();
    updateNotificationButton();
    updateChatButton();
    updateNavSolanaBalance();
    refreshRepoSyncIndicators();
    updateNavRebuildButton();
    updateNodeOnlineControls();
    updateSignInButton();
    return bar;
}

void MainWindow::updateNavRebuildButton()
{
    const bool visible =
        QSettings().value(kShowRebuildButtonSetting, false).toBool();
    if (m_navRebuildButton)
        m_navRebuildButton->setVisible(visible);
}

// The top-bar "Log in / Sign up" pill replaces the retired first-run screen
// (adhoc #115), so it must be honest about state rather than eager: it stays
// hidden until the deferred startup has actually resolved who this machine is.
// Silent auth runs a few seconds after launch and is what fills
// nodeOwnerDisplayName() on a signed-in machine — offering "Log in" before then
// would flash the pill on every start for a user who is already logged in. A
// headless mirror authenticates with its node key and has nobody at the
// keyboard, so it never gets the pill at all.
void MainWindow::updateSignInButton()
{
    if (!m_navSignInButton)
        return;
    const bool signedIn = !nodeOwnerDisplayName().trimmed().isEmpty();
    m_navSignInButton->setVisible(!m_headless && m_deferredStartupRun && !signedIn);
}

void MainWindow::showSignInMenu()
{
    QMenu menu(this);
    // In-app email/password login: this is the path that attaches this machine
    // to an existing forkmesh.com account (runLoginFlow registers the desktop
    // key with the relay and hands back a real website session).
    QAction *login = menu.addAction(QStringLiteral("Log in to your account\xE2\x80\xA6"));
    // Linking through the browser needs a registered, key-bound node — the relay
    // links by node name + this node's key — so only offer it once that holds.
    QAction *browser = nullptr;
    if (!accountOwner().isEmpty() && hasOwnerSigningCapability(accountOwner()))
        browser = menu.addAction(
            QStringLiteral("Link this node in your browser\xE2\x80\xA6"));
    QAction *signup = menu.addAction(QStringLiteral("Create an account\xE2\x80\xA6"));

    QAction *chosen = menu.exec(m_navSignInButton->mapToGlobal(
        QPoint(0, m_navSignInButton->height())));
    if (!chosen)
        return;
    if (chosen == login) {
        loginToUserAccount();
        updateUserSwitcher();
        return;
    }
    if (browser && chosen == browser) {
        openLinkNodeInBrowser();
        return;
    }
    if (chosen == signup) {
        QUrl url = catalogApiUrl(); // http(s) on the mainnode host
        url.setPath(QStringLiteral("/signup"));
        QDesktopServices::openUrl(url);
        logSystem("Account: opened the browser to create a ForkMesh account.");
    }
}

// Pin the floating "Log" button to the bottom-right corner of the live-log
// strip, clearing the vertical scrollbar when it's showing so the button never
// overlaps it. Called on creation and on every strip resize (see eventFilter).
void MainWindow::positionFloatingLogButton()
{
    if (!m_floatingLogButton || !m_footerUpdateLog)
        return;
    m_floatingLogButton->adjustSize();
    constexpr int kMargin = 8;
    const QSize sz = m_floatingLogButton->size();
    int scrollbarW = 0;
    if (QScrollBar *sb = m_footerUpdateLog->verticalScrollBar(); sb && sb->isVisible())
        scrollbarW = sb->width();
    m_floatingLogButton->move(
        m_footerUpdateLog->width() - sz.width() - kMargin - scrollbarW,
        m_footerUpdateLog->height() - sz.height() - kMargin);
    m_floatingLogButton->raise();
}

// Park the pause-scroll toggle in the live-log strip's bottom-right corner, just
// clear of the scrollbar so it never sits under the handle. Called on creation
// and on every strip resize (see eventFilter).
void MainWindow::positionFooterLogPauseButton()
{
    if (!m_footerLogPauseButton || !m_footerUpdateLog)
        return;
    constexpr int kMargin = 3;
    const QSize sz = m_footerLogPauseButton->size();
    int scrollbarW = 0;
    if (QScrollBar *sb = m_footerUpdateLog->verticalScrollBar(); sb && sb->isVisible())
        scrollbarW = sb->width();
    m_footerLogPauseButton->move(
        qMax(0, m_footerUpdateLog->width() - sz.width() - kMargin - scrollbarW),
        qMax(0, m_footerUpdateLog->height() - sz.height() - kMargin));
    m_footerLogPauseButton->raise();
}

// Two states, two icons: pause bars while the strip is following the newest
// line, a down-arrow while it's parked (click to catch back up). Both come from
// the style's own icon set rather than Unicode glyphs, which render as tofu in
// the strip's monospace font.
void MainWindow::updateFooterLogPauseButton()
{
    if (!m_footerLogPauseButton)
        return;
    const bool paused = m_footerLogScrollPaused;
    m_footerLogPauseButton->setIcon(style()->standardIcon(
        paused ? QStyle::SP_ArrowDown : QStyle::SP_MediaPause));
    m_footerLogPauseButton->setToolTip(
        paused ? QStringLiteral(
                     "Auto-scroll paused \xE2\x80\x94 click to follow new log "
                     "lines again")
               : QStringLiteral(
                     "Following new log lines \xE2\x80\x94 click to pause "
                     "auto-scroll"));
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
    // The status text rides on the dot's own tooltip; the dot itself just shows
    // the colour. (The count can change while the colour doesn't, so the tooltip
    // is always refreshed but the dot stylesheet is only rewritten on colour
    // change.)
    m_connectionDot->setToolTip(text);
    if (color == m_connectionStatusColor)
        return;
    m_connectionStatusColor = color;
    // Only the fill + radius are set inline; the background-matching ring is
    // themed via the #connectionDot rule in Theme.h so it works in light mode
    // too. Radius = half the dot's 10px fixed size.
    m_connectionDot->setStyleSheet(
        QStringLiteral("background:%1; border-radius:5px;").arg(color));
}

void MainWindow::updateAdminCrownBadge()
{
    if (!m_adminCrownBadge)
        return;
    m_adminCrownBadge->setVisible(m_isAdmin);
    m_adminCrownBadge->setToolTip(m_isAdmin ? QStringLiteral("Admin") : QString());
}

// Flip this node online/offline from the profile toggle. "Offline" keeps the user
// in the app but stops two reward-eligibility signals — the heartbeat and live
// repo serving — and folds the open session into the saved uptime total.
// "Online" resumes both and restarts the uptime clock. Eligibility never
// guarantees selection or payment. The choice is persisted so a node the user
// deliberately parked offline does not silently resume serving after restart.
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
                  "publishing reward-eligibility signals.");
    } else {
        // Restart the uptime clock only if we are actually attached to a relay.
        if (m_backend && m_connectedAtMs <= 0)
            m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
        if (m_backend && hasOwnerSigningCapability()) {
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
        logSystem("Node back online \xE2\x80\x94 serving repos and publishing "
                  "reward-eligibility signals; selection is not guaranteed.");
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
        online ? QStringLiteral(
                     "This machine is online and may be considered by community "
                     "reward policies; selection is not guaranteed. Click to "
                     "take it offline.")
               : QStringLiteral(
                     "This machine is offline and is not publishing reward-"
                     "eligibility signals. Click to bring it back online."));

    if (m_nodeOnlineStatusLabel) {
        m_nodeOnlineStatusLabel->setText(online ? QStringLiteral("Online")
                                                : QStringLiteral("Offline"));
        m_nodeOnlineStatusLabel->setStyleSheet(
            online ? QStringLiteral("color:#3fb950; font-size:13px; font-weight:800;")
                   : QStringLiteral("color:#d29922; font-size:13px; font-weight:800;"));
    }

    if (m_nodeRewardStatus) {
        m_nodeRewardStatus->setText(online
                                        ? QStringLiteral(
                                              "may be eligible \xC2\xB7 selection "
                                              "not guaranteed")
                                        : QStringLiteral(
                                              "offline \xC2\xB7 not publishing "
                                              "eligibility"));
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
    refreshRepoSyncIndicators();
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
    updateNetworkCounts(m_servers.size(), -1, -1);
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);

    m_relayMenuButton->setIcon(
        (m_activeServer >= 0 && m_activeServer < m_servers.size())
            ? QIcon(faviconFor(m_servers.at(m_activeServer)))
            : QIcon(letterFavicon(host.isEmpty() ? QStringLiteral("ForkMesh")
                                                 : host)));
    if (host.isEmpty())
        host = QStringLiteral("ForkMesh");
    // The button is favicon-only (adhoc #91): the domain and relay count moved
    // into the dropdown itself, so here they only ride the hover tooltip —
    // together with the link speed the dot above the logo is showing.
    refreshRelayMenuTooltip();
}

// The instance button's hover text: which relay is active, how many are
// configured, and what the speed dot above the logo currently means.
void MainWindow::refreshRelayMenuTooltip()
{
    if (!m_relayMenuButton)
        return;
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);
    if (host.isEmpty())
        host = QStringLiteral("ForkMesh");
    m_relayMenuButton->setToolTip(
        QStringLiteral("%1 — %2 %3 configured. Switch, search, or add relays.\n"
                       "Connection speed: %4")
            .arg(host)
            .arg(m_servers.size())
            .arg(m_servers.size() == 1 ? QStringLiteral("relay")
                                       : QStringLiteral("relays"),
                 relaySpeedText(host)));
}

// Apply one round-trip measurement (ms < 0 = the relay didn't answer) to the
// per-relay speed cache, and — when it is the relay we are actually connected
// to — to the dot above the instance logo and that button's tooltip.
void MainWindow::setRelayLinkSpeed(const QString &host, int ms)
{
    const QString key = host.trimmed().toLower();
    if (!key.isEmpty()) {
        m_relayHostLatency.insert(
            key, RelayLatencySample{ms, QDateTime::currentMSecsSinceEpoch()});
    }
    QString activeHost;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        activeHost = serverHost(m_servers.at(m_activeServer).url).toLower();
    if (!key.isEmpty() && !activeHost.isEmpty() && key != activeHost)
        return; // another relay's sample: the cache is all it feeds
    if (m_relaySpeedDot) {
        if (ms < 0)
            m_relaySpeedDot->setUnreachable();
        else
            m_relaySpeedDot->setLatency(ms);
    }
    refreshRelayMenuTooltip();
}

// "30 ms" / "no answer" / "measuring…" for one relay host, as shown in the
// relay dropdown and the instance tooltip (adhoc #124).
QString MainWindow::relaySpeedText(const QString &host) const
{
    const RelayLatencySample sample =
        m_relayHostLatency.value(host.trimmed().toLower());
    if (sample.stampMs <= 0)
        return QString::fromUtf8("measuring\xE2\x80\xA6");
    if (sample.ms < 0)
        return QStringLiteral("no answer");
    return QStringLiteral("%1 ms").arg(sample.ms);
}

// One line of the relay dropdown: "forkmesh.com  ·  30 ms".
QString MainWindow::relayMenuEntryText(const QString &host) const
{
    return QString::fromUtf8("%1  \xC2\xB7  %2").arg(host, relaySpeedText(host));
}

// The room socket's keepalive pong carries the relay round trip for free every
// ~25s; feed it straight to the speed dot so no HTTP probe is needed while the
// socket is up (probeRelayLatency below skips itself when this is fresh).
void MainWindow::onRelayLatencySampled(int ms)
{
    m_lastWsLatencySampleMs = QDateTime::currentMSecsSinceEpoch();
    m_relayProbeFailures = 0;
    QString host;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        host = serverHost(m_servers.at(m_activeServer).url);
    setRelayLinkSpeed(host, ms);
}

// Measure one relay's round-trip the same way probeRelayLatency measures the
// active one (GET /api/version), cache it, and hand the milliseconds (-1 when
// it didn't answer) to `done`. Feeds the per-instance speed in the relay
// dropdown; one probe per host at a time, so re-opening the menu while a probe
// is out doesn't stack a second one.
void MainWindow::probeRelayHostSpeed(const QString &serverUrl,
                                     std::function<void(int)> done)
{
    QUrl url(serverUrl);
    if (url.scheme() == "ws")
        url.setScheme(QStringLiteral("http"));
    else if (url.scheme() == "wss")
        url.setScheme(QStringLiteral("https"));
    url.setPath(QStringLiteral("/api/version"));
    url.setQuery(QString());
    url.setFragment(QString());

    const QString host = serverHost(serverUrl);
    if (!m_networkAccess || !url.isValid() || url.host().isEmpty()) {
        setRelayLinkSpeed(host, -1);
        if (done)
            done(-1);
        return;
    }
    if (m_relaySpeedProbes.contains(host))
        return;
    m_relaySpeedProbes.insert(host);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("accept", "application/json");
    request.setTransferTimeout(10000); // no answer within 10s counts as down

    auto *clock = new QElapsedTimer;
    clock->start();
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, clock, host, done = std::move(done)] {
                const qint64 elapsed = clock->elapsed();
                delete clock;
                reply->deleteLater();
                m_relaySpeedProbes.remove(host);
                const int ms = reply->error() == QNetworkReply::NoError
                                   ? static_cast<int>(elapsed)
                                   : -1;
                setRelayLinkSpeed(host, ms);
                if (done)
                    done(ms);
            });
}

// One dot per node on the network, three rows deep beside the agent fleet
// (adhoc #124). This is where the retired radar dish's blips went, and it keeps
// that widget's roster: refreshNodesTable's filtered list, so the mesh shows
// from launch rather than only while a repo's Mirror-nodes tab is open (adhoc
// #79). Unlike the dish, the dots are always the whole network — the open
// repo's per-node sync/integrity state only tints them (m_nodeDotRepoStates).
// The grid is bounded, so online nodes go in first and a bigger mesh loses its
// offline tail to the tooltip rather than its live nodes.
void MainWindow::refreshNodeDotMatrix()
{
    if (!m_nodeDotMatrix)
        return;
    QVector<NodeDotMatrix::Dot> dots;
    dots.reserve(m_nodeDotEntries.size());
    int online = 0;
    int caution = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (const NodeMenuEntry &e : std::as_const(m_nodeDotEntries)) {
            if (e.online != (pass == 0))
                continue;
            const NodeDotRepoState repo =
                m_nodeDotRepoStates.value(e.name.trimmed().toLower());
            NodeDotMatrix::Dot dot;
            dot.name = e.name;
            dot.self = e.self;
            if (repo.integrityFailing || (e.online && repo.behind)) {
                dot.color = QColor("#d29922"); // amber caution, as on the strip
                ++caution;
            } else if (e.online) {
                dot.color = QColor("#3fb950"); // green: serving
            } else {
                dot.color = QColor("#484f58"); // grey: offline
            }
            if (e.online)
                ++online;
            dots.append(dot);
        }
    }
    m_nodeDotMatrix->setDots(dots);
    m_nodeDotMatrix->setVisible(!dots.isEmpty());
    updateChromeDotDivider();
    if (dots.isEmpty())
        return;

    QString tip = QStringLiteral("%1 node%2 on the network \xE2\x80\x94 "
                                 "%3 online, %4 offline")
                      .arg(dots.size())
                      .arg(dots.size() == 1 ? QString() : QStringLiteral("s"))
                      .arg(online)
                      .arg(dots.size() - online);
    if (caution > 0)
        tip += QStringLiteral(", %1 out of sync").arg(caution);
    const int shown = m_nodeDotMatrix->shownCount();
    if (shown < dots.size())
        tip += QStringLiteral("\n(showing the first %1)").arg(shown);
    tip += QStringLiteral("\nClick a dot to open that node.");
    m_nodeDotMatrix->setToolTip(tip);
}

// The hairline between the agent squares and the node dots only earns its place
// when there are dots on both sides of it (adhoc #124).
void MainWindow::updateChromeDotDivider()
{
    if (!m_chromeDotDivider)
        return;
    // isHidden(), not isVisible(): the window itself may not be up yet when the
    // first roster lands, and the divider still needs to be laid out.
    m_chromeDotDivider->setVisible(m_agentDotMatrix &&
                                   !m_agentDotMatrix->isHidden() &&
                                   m_nodeDotMatrix &&
                                   !m_nodeDotMatrix->isHidden());
}

// Repo-scoped sync/integrity state for the node dots, published by the
// Mirror-nodes panel (empty when it has no repo to show).
void MainWindow::setNodeDotRepoStates(
    const QHash<QString, NodeDotRepoState> &states)
{
    if (states.isEmpty() && m_nodeDotRepoStates.isEmpty())
        return;
    m_nodeDotRepoStates = states;
    refreshNodeDotMatrix();
}

// Measure the round-trip latency to the active relay and feed it to the speed
// dot above the instance logo. We GET the relay's lightweight /api/version
// endpoint (small JSON, no Durable-Object fan-out) and time the request; a
// transport error or timeout turns the dot red. Only one probe runs at a time.
// While the room socket is connected its keepalive pong updates the dot
// every ~25s (onRelayLatencySampled), so this HTTP probe only fires when that
// signal has gone quiet — i.e. the socket is down or reconnecting.
void MainWindow::probeRelayLatency()
{
    if (!m_relaySpeedDot || !m_networkAccess || m_relayProbeInFlight)
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_lastWsLatencySampleMs < 90 * 1000)
        return;

    QUrl url = catalogApiUrl(); // same relay host, http(s) scheme
    const QString host = url.host();
    if (!url.isValid() || host.isEmpty()) {
        setRelayLinkSpeed(host, -1);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, clock, host] {
        const qint64 elapsed = clock->elapsed();
        delete clock;
        m_relayProbeInFlight = false;
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_relayProbeFailures = 0;
            setRelayLinkSpeed(host, static_cast<int>(elapsed));
            // A one-off slow sample (first request on a cold connection, a
            // momentary hiccup) paints the dot amber/red and then sits there
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
            // now-dead connection and the dot never clears even after we're
            // back online.
            if (m_networkAccess)
                m_networkAccess->clearConnectionCache();
            // A single miss is usually just a stale keep-alive socket or a
            // momentary blip (very common for the first probe right after
            // launch, before the connection is warm) — not a real outage. Don't
            // flip the dot to red on the strength of one failure; re-probe
            // shortly on the now-clean connection and only declare "offline"
            // once a second consecutive probe also fails. This stops the dot
            // getting stranded on red while we're genuinely online.
            // Exception: when the OS itself reports the machine has no network
            // at all, the outage is real — skip the grace period and show it
            // immediately (adhoc #41).
            const auto *netInfo = QNetworkInformation::instance();
            const bool osOffline =
                netInfo && netInfo->reachability() ==
                               QNetworkInformation::Reachability::Disconnected;
            if (++m_relayProbeFailures >= 2 || osOffline) {
                setRelayLinkSpeed(host, -1);
                // While offline, re-probe on a short leash instead of waiting
                // out the minute timer, so the dot flips back within seconds
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
                if (reachability ==
                    QNetworkInformation::Reachability::Disconnected) {
                    // Definitive: no network interface is up. No point probing;
                    // mark the outage as established so a later probe failure
                    // doesn't get the one-blip grace period.
                    m_relayProbeFailures = 2;
                    if (m_relaySpeedDot)
                        setRelayLinkSpeed(catalogApiUrl().host(), -1);
                    if (m_backend)
                        m_backend->setNetworkAvailable(false);
                } else {
                    // Link is (possibly) back: confirm with a real probe right
                    // away. The dot stays red until the probe succeeds, so a
                    // half-up link never shows a false green.
                    probeRelayLatency();
                    if (m_backend)
                        m_backend->setNetworkAvailable(true);
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
    // How long a measured speed stays good enough to show without re-probing.
    // Wide enough that the active relay's ~25s keepalive pong normally covers
    // it, so opening the menu doesn't re-probe the relay we're talking to.
    constexpr qint64 kRelaySpeedFreshMs = 30 * 1000;
    QMenu menu(this);

    // Header: the active relay's domain plus the relay count — the readout
    // that used to sit on the chrome-line button label itself (adhoc #91).
    QString activeHost;
    if (m_activeServer >= 0 && m_activeServer < m_servers.size())
        activeHost = serverHost(m_servers.at(m_activeServer).url);
    QAction *header = menu.addAction(
        activeHost.isEmpty()
            ? QStringLiteral("Relays (%1)").arg(formatCount(m_servers.size()))
            : QStringLiteral("%1 — Relays (%2)")
                  .arg(activeHost, formatCount(m_servers.size())));
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

    // One checkable action per relay (active one checked), each labelled with
    // that instance's connection speed (adhoc #124) — the readout the radar
    // dish used to carry for the active relay only. Cached samples show
    // instantly; anything stale is re-probed and the label updates in place
    // while the menu is open.
    QList<QAction *> relayActions;
    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &server = m_servers.at(i);
        const QString host = serverHost(server.url);
        QAction *act = menu.addAction(QIcon(faviconFor(server)),
                                      relayMenuEntryText(host));
        act->setCheckable(true);
        act->setChecked(i == m_activeServer);
        connect(act, &QAction::triggered, this, [this, i] { switchToServer(i); });
        relayActions.append(act);

        const RelayLatencySample sample =
            m_relayHostLatency.value(host.trimmed().toLower());
        if (QDateTime::currentMSecsSinceEpoch() - sample.stampMs <
            kRelaySpeedFreshMs)
            continue;
        // The action dies with the menu, which the probe can easily outlive.
        QPointer<QAction> guarded(act);
        probeRelayHostSpeed(server.url, [this, guarded, host](int) {
            if (guarded)
                guarded->setText(relayMenuEntryText(host));
        });
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

// How long a fetched balance stays good. Hovering the top-bar label again
// inside this window re-uses the cached figure instead of spending another
// public-endpoint getBalance call.
const qint64 kNavSolanaBalanceTtlMs = 60 * 1000;

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

QString externalWalletBalanceTooltip(const QString &detail)
{
    return detail +
           QStringLiteral(
               "\nNon-custodial: this is a public external-wallet balance. "
               "ForkMesh never receives or stores its private key.");
}
}  // namespace

void MainWindow::updateNodeSwitcher()
{
    updateUserSwitcher();
    // Keep the Nodes directory in step with the dropdown's node list. It owns the
    // rail badge too — m_nodeMenuEntries also holds chat user accounts and repo
    // owners, which are not nodes, so counting it here over-badged the rail.
    refreshNodesTable();
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

void MainWindow::updateUserSwitcher()
{
    // Every profile-hydration path lands here after updating the user/node
    // flags, so this is also where the chat backend learns which account kind
    // to stamp on outgoing frames (web surfaces only display user/guest frames
    // — same user-vs-node rule as welcomeChannelForIdentity()).
    if (m_backend) {
        const bool userLike =
            m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty();
        // No username yet (fresh install on its generated name): speak as
        // "guest" like the web's anonymous visitors, so the person's messages
        // render on web surfaces instead of being dropped as node frames.
        m_backend->setAccountKind(userLike ? QStringLiteral("user")
                                  : chatIdentityIsGuest()
                                      ? QStringLiteral("guest")
                                      : QStringLiteral("node"));
    }
    const QString user = topBarUserName();
    if (m_userAvatarNavButton) {
        m_userAvatarNavButton->setToolTip(
            chatIdentityIsGuest()
                ? QStringLiteral("Chatting as %1 — pick a username in "
                                 "Settings or log in to claim one")
                      .arg(guestChatName())
                : user.isEmpty()
                      ? QStringLiteral("Your user account")
                      : QStringLiteral("%1 user account").arg(user));
    }
    updateUserAvatarButton();
    updateChatIdentity();
    // Every profile-hydration path lands here, so this is also where the top-bar
    // "Log in / Sign up" pill learns that an account just arrived (or went away).
    updateSignInButton();
    // The top-right node-name label folds in the user account name
    // ("user/node"), so keep it in step with the user identity too.
    refreshWebUserSolanaAddress();
    updateNavSolanaBalance();
}

void MainWindow::cacheWebUserSolanaProfile(const QString &account,
                                           const QJsonObject &profile)
{
    const QString normalized = account.trimmed().toLower();
    if (normalized.isEmpty() ||
        profile.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("user"))
        return;
    QString address = profile.value(QStringLiteral("solana")).toString().trimmed();
    if (!address.isEmpty() &&
        !forkmesh::control::isValidSolanaPublicAddress(address))
        address.clear();
    m_webSolanaAccount = normalized;
    m_webSolanaAddress = address;
    m_webSolanaKnown = true;
    m_webSolanaFetchedMs = QDateTime::currentMSecsSinceEpoch();
    updateNavSolanaBalance();
}

void MainWindow::refreshWebUserSolanaAddress()
{
    if (!m_networkAccess)
        return;
    const QString account = topBarUserName().trimmed().toLower();
    const bool hasWebUser =
        !m_nodeOwnerUser.trimmed().isEmpty() || m_profileIsUserAccount ||
        (m_accountAuthenticated &&
         settingsAccountName().compare(account, Qt::CaseInsensitive) == 0);
    if (account.isEmpty() || !hasWebUser) {
        m_webSolanaAccount.clear();
        m_webSolanaAddress.clear();
        m_webSolanaKnown = false;
        m_webSolanaFetchedMs = 0;
        return;
    }

    if (!m_webSolanaTimer) {
        m_webSolanaTimer = new QTimer(this);
        m_webSolanaTimer->setInterval(60 * 1000);
        connect(m_webSolanaTimer, &QTimer::timeout, this,
                &MainWindow::refreshWebUserSolanaAddress);
        m_webSolanaTimer->start();
    }
    if (m_webSolanaAccount != account) {
        m_webSolanaAccount = account;
        m_webSolanaAddress.clear();
        m_webSolanaKnown = false;
        m_webSolanaFetchedMs = 0;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_webSolanaKnown && now - m_webSolanaFetchedMs < 55 * 1000)
        return;
    if (m_webSolanaFetchInFlight)
        return;

    m_webSolanaFetchInFlight = true;
    QNetworkRequest request(accountsApiUrl(account));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Cache-Control", "no-cache");
    if (!m_accountSessionToken.trimmed().isEmpty())
        request.setRawHeader(
            "Authorization",
            QByteArrayLiteral("Bearer ") + m_accountSessionToken.toUtf8());
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, account] {
                m_webSolanaFetchInFlight = false;
                const bool ok = reply->error() == QNetworkReply::NoError;
                const QJsonObject profile =
                    QJsonDocument::fromJson(reply->readAll()).object();
                reply->deleteLater();
                if (account != topBarUserName().trimmed().toLower()) {
                    refreshWebUserSolanaAddress();
                    return;
                }
                if (ok && profile.value(QStringLiteral("exists")).toBool() &&
                    profile.value(QStringLiteral("kind")).toString() ==
                        QStringLiteral("user"))
                    cacheWebUserSolanaProfile(account, profile);
            });
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
    // The top bar no longer prints "user/node" beside the balance (adhoc #42),
    // but admin status is still a crown badge on the avatar — piggyback on this
    // frequently-called refresh so the badge stays in sync wherever m_isAdmin
    // changes.
    updateAdminCrownBadge();
    if (!m_navSolanaBalance)
        return;

    const QString account = topBarUserName().trimmed().toLower();
    const QString addr =
        m_webSolanaKnown && m_webSolanaAccount == account
            ? m_webSolanaAddress
            : savedSolanaAddress();
    if (addr != m_navSolanaBalanceAddress) {
        // Address changed: the in-memory balance no longer applies. Seed from
        // the balance we persisted for this address last time so the label can
        // show a figure straight away — a hover is what refreshes it.
        m_navSolanaLamports = -1;
        m_navSolanaFetchedMs = 0;
        const QVariant saved = QSettings().value(lastSolanaBalanceSetting(addr));
        bool savedOk = false;
        const qint64 savedLamports = saved.toString().toLongLong(&savedOk);
        if (saved.isValid() && savedOk && savedLamports >= 0)
            m_navSolanaLamports = savedLamports;
    }
    m_navSolanaBalanceAddress = addr;
    if (addr.isEmpty()) {
        m_navSolanaLamports = -1;
        m_navSolanaBalance->setText(
            QStringLiteral("0.000000000 SOL &nbsp;&middot;&nbsp; "
                           "<a href=\"settings\">Add address online</a>"));
        m_navSolanaBalance->setToolTip(externalWalletBalanceTooltip(
            QStringLiteral(
                "Add a public self-custodial Solana address to show its balance.")));
        return;
    }
    if (!isLikelySolanaAddress(addr)) {
        m_navSolanaLamports = -1;
        m_navSolanaBalance->setText(QStringLiteral("SOL invalid"));
        m_navSolanaBalance->setToolTip(externalWalletBalanceTooltip(
            QStringLiteral("Saved public Solana address is invalid.")));
        return;
    }

    // No getBalance here: every caller of this function is a profile/identity
    // hydration path, and they fire often enough that querying from each one
    // hammered the public Solana endpoints. Render what we have; the RPC is
    // issued when the pointer enters the label (refreshNavSolanaBalance).
    renderNavSolanaBalance();
}

// Re-render the balance label from the cached lamports + fiat rate, without
// touching Solana. Shows a "hover to load" placeholder when nothing is cached
// yet, and fetches a single price when the fiat rate is stale.
void MainWindow::renderNavSolanaBalance()
{
    if (!m_navSolanaBalance)
        return;
    if (m_navSolanaBalanceAddress.isEmpty())
        return; // updateNavSolanaBalance() already painted the empty state
    if (m_navSolanaLamports < 0) {
        if (m_navSolanaFetchInFlight)
            return; // a hover-triggered query is already painting "SOL ..."
        m_navSolanaBalance->setText(QString::fromUtf8("SOL \xE2\x80\x94"));
        m_navSolanaBalance->setToolTip(externalWalletBalanceTooltip(
            QStringLiteral("Hover to check your public Solana balance.")));
        return;
    }
    const QString cur = solanaDisplayCurrency();
    const QString solBalance = formatSolanaBalance(m_navSolanaLamports);
    if (cur == QLatin1String("sol")) {
        m_navSolanaBalance->setText(solBalance);
        m_navSolanaBalance->setToolTip(
            externalWalletBalanceTooltip(
                QStringLiteral("Your public Solana balance: %1")
                    .arg(solBalance)));
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
            externalWalletBalanceTooltip(
                QStringLiteral("Your public wallet balance: %1 (%2)")
                    .arg(fiatBalance, solBalance)));
        return;
    }
    // No fresh rate cached. This function now runs on every profile-hydration
    // pass, so back off after a recent attempt (successful or not) instead of
    // re-asking the price API each time; show the SOL figure meanwhile.
    const bool attemptedRecently =
        it != m_navFiatRates.constEnd() && now - it->second < 60 * 1000;
    if (attemptedRecently || m_navFiatFetchInFlight) {
        m_navSolanaBalance->setText(solBalance);
        m_navSolanaBalance->setToolTip(
            externalWalletBalanceTooltip(
                QStringLiteral("SOL/%1 price unavailable. Public balance: %2")
                    .arg(cur.toUpper(), solBalance)));
        return;
    }
    m_navSolanaBalance->setText(QStringLiteral("%1 ...").arg(fiatCurrencySymbol(cur)));
    m_navSolanaBalance->setToolTip(
        externalWalletBalanceTooltip(
            QStringLiteral("Checking SOL/%1 price for %2")
                .arg(cur.toUpper(), solBalance)));
    queryNavSolanaUsdPrice(m_navSolanaBalanceAddress, m_navSolanaLamports);
}

// Hovering the top-bar balance is the only thing that spends a Solana RPC
// call: everything else renders the cached figure. Repeat hovers inside the
// TTL (and hovers while a query is already out) are no-ops.
void MainWindow::refreshNavSolanaBalance(bool force)
{
    if (!m_navSolanaBalance || !m_networkAccess)
        return;
    const QString addr = m_navSolanaBalanceAddress;
    if (addr.isEmpty() || !isLikelySolanaAddress(addr))
        return;
    if (m_navSolanaFetchInFlight)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_navSolanaFetchedMs > 0 &&
        now - m_navSolanaFetchedMs < kNavSolanaBalanceTtlMs)
        return;
    if (m_navSolanaLamports < 0) {
        m_navSolanaBalance->setText(QStringLiteral("SOL ..."));
        m_navSolanaBalance->setToolTip(externalWalletBalanceTooltip(
            QStringLiteral("Checking your public Solana balance.")));
    }
    m_navSolanaFetchInFlight = true;
    queryNavSolanaBalance(addr, 0);
}

void MainWindow::queryNavSolanaBalance(const QString &addr, int endpointIndex)
{
    const int count = int(sizeof(kSolanaRpcEndpoints) / sizeof(kSolanaRpcEndpoints[0]));
    if (endpointIndex >= count) {
        m_navSolanaFetchInFlight = false;
        // Back off for a TTL before the next hover retries, so a dead endpoint
        // can't be re-probed on every pointer pass over the label.
        m_navSolanaFetchedMs = QDateTime::currentMSecsSinceEpoch();
        if (m_navSolanaBalance && m_navSolanaBalanceAddress == addr &&
            m_navSolanaLamports < 0) {
            m_navSolanaBalance->setText(QStringLiteral("SOL unavailable"));
            m_navSolanaBalance->setToolTip(externalWalletBalanceTooltip(
                QStringLiteral("Public Solana balance is temporarily unavailable.")));
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
        if (!m_navSolanaBalance || m_navSolanaBalanceAddress != addr) {
            m_navSolanaFetchInFlight = false;
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(raw).object();
        const QJsonObject result = root.value("result").toObject();
        if (netError != QNetworkReply::NoError || !result.contains("value")) {
            queryNavSolanaBalance(addr, endpointIndex + 1); // stays in-flight
            return;
        }
        m_navSolanaFetchInFlight = false;
        m_navSolanaFetchedMs = QDateTime::currentMSecsSinceEpoch();
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
            postNotification(QStringLiteral("Public wallet balance increased"),
                             QStringLiteral(
                                 "%1 received by your external self-custodial "
                                 "wallet. New public balance: %2")
                                 .arg(amount, balance),
                             false, QStringLiteral("emblem-default"));
        }
        settings.setValue(lastBalanceKey, QString::number(lamports));
        const QString cur = solanaDisplayCurrency();
        if (cur != QLatin1String("sol")) {
            m_navSolanaBalance->setText(
                QStringLiteral("%1 ...").arg(fiatCurrencySymbol(cur)));
            m_navSolanaBalance->setToolTip(
                externalWalletBalanceTooltip(
                    QStringLiteral("Checking SOL/%1 price for %2")
                        .arg(cur.toUpper(), balance)));
            queryNavSolanaUsdPrice(addr, lamports);
            return;
        }
        m_navSolanaBalance->setText(balance);
        m_navSolanaBalance->setToolTip(
            externalWalletBalanceTooltip(
                QStringLiteral("Your public Solana balance: %1")
                    .arg(balance)));
    });
}

void MainWindow::queryNavSolanaUsdPrice(const QString &addr, qint64 lamports)
{
    const QString cur = solanaDisplayCurrency();
    if (cur == QLatin1String("sol"))
        return;
    if (m_navFiatFetchInFlight)
        return;
    m_navFiatFetchInFlight = true;
    QNetworkRequest request(QUrl(
        QStringLiteral("https://api.coingecko.com/api/v3/simple/price"
                       "?ids=solana&vs_currencies=%1").arg(cur)));
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, addr, lamports, cur]() {
        const QByteArray raw = reply->readAll();
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        m_navFiatFetchInFlight = false;
        const double rate =
            QJsonDocument::fromJson(raw).object()
                .value(QStringLiteral("solana")).toObject()
                .value(cur).toDouble();
        // Stamp the attempt either way: a 0 rate marks "tried and failed" so
        // renderNavSolanaBalance backs off instead of retrying on every pass.
        if (netError != QNetworkReply::NoError || rate <= 0.0)
            m_navFiatRates[cur] = {0.0, QDateTime::currentMSecsSinceEpoch()};
        if (!m_navSolanaBalance || m_navSolanaBalanceAddress != addr ||
            solanaDisplayCurrency() != cur)
            return;

        const QString solBalance = formatSolanaBalance(lamports);
        if (netError != QNetworkReply::NoError || rate <= 0.0) {
            m_navSolanaBalance->setText(solBalance);
            m_navSolanaBalance->setToolTip(
                externalWalletBalanceTooltip(
                    QStringLiteral("SOL/%1 price unavailable. Public balance: %2")
                        .arg(cur.toUpper(), solBalance)));
            return;
        }

        m_navFiatRates[cur] = {rate, QDateTime::currentMSecsSinceEpoch()};
        const QString fiatBalance = formatFiatBalance(lamports, rate, cur);
        m_navSolanaBalance->setText(fiatBalance);
        m_navSolanaBalance->setToolTip(
            externalWalletBalanceTooltip(
                QStringLiteral("Your public wallet balance: %1 "
                               "(%2 at %3%4/SOL)")
                    .arg(fiatBalance, solBalance, fiatCurrencySymbol(cur),
                         QString::number(rate, 'f', 2))));
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
            text += " (this machine)";
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

        // Temporary world-chat visitors are chat users, not network nodes —
        // keep them out of this window too (adhoc #308).
        QList<MemberInfo> nodes;
        for (const MemberInfo &m : std::as_const(m_homeRoster)) {
            if (!isTemporaryChatGuest(m))
                nodes.append(m);
        }
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

            // Icon: real avatar if known, else a generated machine tile.
            QPixmap avatar = m_avatars.value(node.id);
            if (avatar.isNull())
                avatar = nodeMachineFavicon(node.id.isEmpty() ? node.name : node.id,
                                            48);
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
                         node.self ? " <span style='color:#8b949e'>(this "
                                     "machine)</span>"
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
            lines << QStringLiteral("Public wallet balance: %1")
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

// The Repos rail opens the network-wide repository list, so its badge counts
// the rows that page shows — every repository on the relay — rather than this
// machine's own copies, which read as a wrong number next to that table
// (adhoc #118). Until the catalog has been rendered once the local repo menu is
// the only count we have, so it stands in.
void MainWindow::updateReposNavBadge()
{
    if (auto *railButton =
            dynamic_cast<ActivityRailButton *>(m_reposNavButton))
        railButton->setBadgeCount(m_networkRepoRowCount >= 0
                                      ? m_networkRepoRowCount
                                      : m_repoMenuEntries.size());
}

void MainWindow::updateRepoSwitcher()
{
    if (!m_repoMenuButton)
        return;
    updateReposNavBadge();
    // Mid node-switch: the repo list belongs to the node being loaded, so keep
    // the button visible with a "Loading…" label (the spinner icon is driven by
    // startRepoSwitchSpin) instead of revealing a count or repo name until the
    // switch completes.
    if (m_nodeSwitching) {
        m_repoMenuButton->setVisible(true);
        if (m_repoLabel)
            m_repoLabel->hide();
        m_repoMenuButton->setText(QString::fromUtf8("Loading\xE2\x80\xA6"));
        return;
    }
    // Nothing in the repo area when the selected node has no repos.
    const bool hasRepos = !m_repoMenuEntries.isEmpty();
    m_repoMenuButton->setVisible(hasRepos);
    // The Code button is primary section nav now, so it stays visible even with
    // no repos (it just lands on the empty Home view).
    if (m_repoLabel)
        m_repoLabel->hide();
    if (!hasRepos)
        return;
    const QString caret = QString::fromUtf8("\xE2\x96\xBE");
    QString label = QStringLiteral("Repositories");
    QString owner;
    if (m_repoDetailIndex >= 0 && m_repoDetailIndex < m_repositories.size()) {
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        owner = repo.owner;
        label = repo.owner + QLatin1Char('/') + repo.name;
    }
    int ownerRepoCount = 0;
    for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries)) {
        if (entry.index >= 0 && entry.index < m_repositories.size() &&
            m_repositories.at(entry.index).owner == owner)
            ++ownerRepoCount;
    }
    // Read like a Git hosting identity. It becomes a switcher only when the
    // current user/organization actually has another repository to choose.
    if (ownerRepoCount > 1)
        label += QStringLiteral("  ") + caret;
    m_repoMenuButton->setText(label);
    m_repoMenuButton->setEnabled(ownerRepoCount > 1);
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

// Keep the open repo's sync-derived indicators in step after anything that may
// have changed the push state (a new local commit, a completed publish/sync).
// The floating "Sync (N)" pill this used to paint above the Code tab is gone
// (adhoc #374), and with it the off-thread ahead/behind walks that fed only its
// label and tooltip — what's left is the activity rail's spinning Git glyph and
// the commit list's "waiting to sync" markers.
void MainWindow::refreshRepoSyncIndicators()
{
    // Only while the commit list is on screen; a cheap no-op otherwise.
    refreshCommitMarkersIfStale();

    // The activity rail's Git icon spins while the open repo is pushing or
    // publishing (adhoc #357). A quiet background auto-sync (the periodic
    // mirror refresh) must not light this up — it isn't something the user
    // did, so a spinner tied to it reads as unexplained (adhoc #81).
    const int index = m_repoDetailIndex;
    if (m_railGitButton)
        m_railGitButton->setSyncing(
            index >= 0 &&
            (m_pushingRepos.contains(index) ||
             (m_syncingRepos.contains(index) && !m_syncingRepos.value(index))));
}

// Canonicalize and hash the stdout of `git for-each-ref
// --format=%(objectname) %(refname) refs/heads/ refs/tags/` into the sha256 the
// relay pins. This MUST stay byte-for-byte identical to the worker's
// advertised_refs_canonical() so the relay's integrity pin matches what we sign
// (the worker rejects every clone whose live refs don't hash to the pin).
static QString hashForEachRefOutput(const QByteArray &out)
{
    return PublicMirrorRuntime::refsSha256FromForEachRef(out);
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
// warning only surfaces there — as a caution triangle on the self row/dot in the
// Mirror nodes panel (m_repoPinMismatch, see loadMirrorNodesPanel), not a
// top-bar toast; the "Reset integrity pin" action lives in that panel's header.
//
// And because the gates below only let the check run on the node that CAN fix
// the pin (owner key + working copy — the source of truth), a detected mismatch
// also re-attests immediately instead of leaving clones rejected until the
// 15-minute reattestStalePins tick or a manual "Reset integrity pin" click: the
// source of truth defines the correct state, so it should never sit failing its
// own pin. Rate-limited per repo so a re-publish the relay keeps refusing can't
// loop into a write storm.
void MainWindow::refreshRepoPinBanner()
{
    if (!m_topMessage)
        return;
    const bool wasFlagged = m_repoPinMismatch;
    m_repoPinMismatch = false;
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
            [this, git, index, owner, name,
             wasFlagged](int code, QProcess::ExitStatus status) {
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
                        [this, reply, index, owner, name, localHash, wasFlagged] {
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
                            if (found && !pinned.isEmpty() && pinned != localHash) {
                                m_repoPinMismatch = true;
                                logSystem(
                                    "Integrity pin: clones of " + owner + "/" + name +
                                    " are being rejected — the relay's pinned hash no "
                                    "longer matches the refs this machine serves.");
                                loadMirrorNodesPanel(); // paint the caution triangle now
                                // This node holds the working copy and the owner
                                // key (the gates at the top of this function), so
                                // it is the source of truth — re-sign the refs it
                                // actually serves right now rather than waiting
                                // for reattestStalePins or a manual reset.
                                const QString healKey = owner + "/" + name;
                                const qint64 nowMs =
                                    QDateTime::currentMSecsSinceEpoch();
                                if (index >= 0 && index < m_repositories.size() &&
                                    nowMs - m_repoPinAutoHealAtMs.value(healKey) >=
                                        30000) {
                                    m_repoPinAutoHealAtMs.insert(healKey, nowMs);
                                    logSystem(
                                        "Integrity pin: this node is the source of "
                                        "truth for " + owner + "/" + name +
                                        " — re-attesting its current refs "
                                        "automatically.");
                                    // The RELAY's record is what drifted, so the
                                    // local publish fingerprint may still read
                                    // "unchanged" — drop it so the unchanged-skip
                                    // gate can't swallow this corrective write
                                    // (same as reattestStalePins).
                                    m_catalogPublishedFingerprint.remove(
                                        catalogPublishKey(
                                            m_repositories.at(index)));
                                    publishRepository(index, false);
                                    // Re-check once the signed write has had a
                                    // moment to land; a match clears the triangle.
                                    QTimer::singleShot(1500, this, [this, index] {
                                        if (m_repoDetailIndex == index)
                                            refreshRepoPinBanner();
                                    });
                                }
                            } else if (wasFlagged) {
                                // The pin healed (re-attest landed, or the relay
                                // caught up); repaint so the caution triangle
                                // doesn't linger until the next panel rebuild.
                                loadMirrorNodesPanel();
                            }
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
    if (!m_networkAccess || !hasOwnerSigningCapability())
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
                            // The RELAY's record is what drifted, so the local
                            // publish fingerprint may still read "unchanged" —
                            // drop it so the unchanged-skip gate can't swallow
                            // this corrective write.
                            m_catalogPublishedFingerprint.remove(
                                catalogPublishKey(m_repositories.at(c.index)));
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
    // clears the caution triangle if the pin now matches, or re-flags it if not.
    QTimer::singleShot(1500, this, [this, index] {
        if (m_repoDetailIndex == index)
            refreshRepoPinBanner();
    });
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
            refreshRepoSyncIndicators();
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
    refreshRepoSyncIndicators();

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
                refreshRepoSyncIndicators();
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
                auto *viewBtn = box.addButton(QStringLiteral("View code"),
                                              QMessageBox::ActionRole);
                auto *cancelBtn = box.addButton(QStringLiteral("Cancel push"),
                                                QMessageBox::RejectRole);
                auto *bypassBtn = box.addButton(QStringLiteral("Push anyway"),
                                                QMessageBox::DestructiveRole);
                box.setDefaultButton(cancelBtn);
                box.exec();
                if (box.clickedButton() == viewBtn) {
                    // Jumping to the code cancels the push: the point is to remove
                    // the credential first. Copy the path/line out before the
                    // navigation below, which pumps the event loop (and can rebuild
                    // the findings' owning state) across its git reads.
                    const QString path = scan.findings.first().path;
                    const int line = scan.findings.first().line;
                    m_pushingRepos.remove(index);
                    refreshRepoSyncIndicators();
                    openRepoDetail(index);
                    // Switch to the Code tab (index 0) so the highlighted line is
                    // visible; openRepoFileAtLine alone only touches the (currently
                    // hidden) files panel.
                    if (m_repoDetailTabs && m_repoDetailTabs->button(0))
                        m_repoDetailTabs->button(0)->setChecked(true);
                    if (m_repoDetailStack)
                        m_repoDetailStack->setCurrentIndex(0);
                    openRepoFileAtLine(path, line);
                    return;
                }
                if (box.clickedButton() != bypassBtn) {
                    m_pushingRepos.remove(index);
                    refreshRepoSyncIndicators();
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
                refreshRepoSyncIndicators();
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
    refreshRepoSyncIndicators();
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
                refreshRepoSyncIndicators();
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
                refreshRepoSyncIndicators();
            });
    process->start(QStringLiteral("git"),
                   {QStringLiteral("-C"), repo.localPath, QStringLiteral("push")});
}

void MainWindow::showRepoMenu()
{
    if (!m_repoMenuButton || m_repoDetailIndex < 0 ||
        m_repoDetailIndex >= m_repositories.size())
        return;
    const QString owner = m_repositories.at(m_repoDetailIndex).owner;
    QList<const RepoMenuEntry *> choices;
    for (const RepoMenuEntry &entry : std::as_const(m_repoMenuEntries)) {
        if (entry.index >= 0 && entry.index < m_repositories.size() &&
            m_repositories.at(entry.index).owner == owner)
            choices.append(&entry);
    }
    if (choices.size() <= 1)
        return;
    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QAction *header = menu.addAction(
        QStringLiteral("%1 repositories (%2)")
            .arg(owner, formatCount(choices.size())));
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

    QList<QAction *> repoActions;
    QStringList repoNames;
    for (const RepoMenuEntry *entry : std::as_const(choices)) {
        const RepoMenuEntry &e = *entry;
        QAction *act = menu.addAction(e.icon, e.label);
        if (!e.detail.isEmpty())
            act->setToolTip(e.detail);
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
    // Opening chat while something else is unread lands on that conversation
    // instead of whatever was last open — the badge is what the click was
    // aiming at, so jump straight to the messages behind it.
    const QString unread = mostRecentUnreadConversation();
    showSection(2);
    if (!unread.isEmpty())
        switchConversation(unread);
}

QString MainWindow::mostRecentUnreadConversation() const
{
    if (m_unread.isEmpty() || m_unread.contains(m_currentConversation))
        return QString();
    // Sidebar order (channels, then DMs) is the tie-breaker, so an unread with
    // no local history still resolves to a stable pick.
    QStringList ordered = m_channels;
    ordered.reserve(m_channels.size() + m_openDms.size());
    for (const QString &peerId : std::as_const(m_openDms))
        ordered.append(dmKey(peerId));

    QString best;
    qint64 bestStamp = -1;
    for (const QString &conversation : std::as_const(ordered)) {
        if (!m_unread.contains(conversation))
            continue;
        qint64 stamp = 0;
        const auto it = m_history.constFind(conversation);
        if (it != m_history.constEnd() && !it->isEmpty())
            stamp = it->last().timestampMs;
        if (stamp > bestStamp) {
            bestStamp = stamp;
            best = conversation;
        }
    }
    return best;
}

void MainWindow::updateChatButton()
{
    if (!m_chatButton)
        return;

    int total = 0;
    for (const int n : std::as_const(m_unreadCounts))
        total += n;

    // The red unread count rides the icon's corner, painted by the rail item
    // itself (setBadgeUrgent(true) at construction picks the red style).
    if (auto *railButton = dynamic_cast<ActivityRailButton *>(m_chatButton))
        railButton->setBadgeCount(total);

    m_chatButton->setToolTip(
        total > 0 ? QString::fromUtf8("Chat \xE2\x80\x94 %1 unread message%2")
                        .arg(total)
                        .arg(total == 1 ? QString() : QStringLiteral("s"))
                  : QStringLiteral("Chat"));

    updateChatUnreadBanner();
}

void MainWindow::updateChatUnreadBanner()
{
    if (!m_chatUnreadBanner)
        return;
    int total = 0;
    for (const int n : std::as_const(m_unreadCounts))
        total += n;
    if (total > 0) {
        if (m_chatUnreadBannerLabel)
            m_chatUnreadBannerLabel->setText(
                QString::fromUtf8("%1 unread message%2")
                    .arg(total)
                    .arg(total == 1 ? QString() : QStringLiteral("s")));
        m_chatUnreadBanner->show();
    } else {
        m_chatUnreadBanner->hide();
    }
}

void MainWindow::markAllChatRead()
{
    if (!m_unread.isEmpty() || !m_unreadCounts.isEmpty()) {
        m_unread.clear();
        m_unreadCounts.clear();
        refreshChannelList();
        refreshDmList();
        updateChatButton();
        // Unread state persists across restarts now; flush the cleared markers so
        // they don't come back after a restart.
        scheduleChatSave();
    }
    // "...and to see them": jump to the newest messages in the open conversation.
    m_stickToBottom = true;
    scrollToBottom();
}

bool MainWindow::isChatViewVisible() const
{
    // Chat is its own section (index 2). It's "being read" only when that
    // section is active and the app window is active (not minimized / behind).
    return isActiveWindow() && m_sectionStack &&
           m_sectionStack->currentIndex() == 2;
}

void MainWindow::clearActiveConversationUnread()
{
    if (!isChatViewVisible() || m_currentConversation.isEmpty())
        return;
    if (m_unread.remove(m_currentConversation)) {
        m_unreadCounts.remove(m_currentConversation);
        refreshChannelList();
        refreshDmList();
        // Unread state persists across restarts now; flush the cleared
        // marker so it doesn't come back after a restart.
        scheduleChatSave();
    }
    updateChatButton();
}

QWidget *MainWindow::buildSolanaNotice()
{
    m_solanaBanner = new QWidget;
    m_solanaBanner->setObjectName("solanaBanner");
    m_solanaBannerLabel = new QLabel(
        "Add a public self-custodial Solana payout address if you want this "
        "mirror to be considered by community reward policies.");
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
    // add a payout address. The only prompt to set one is the explicit mirror
    // reward-settings button on the node profile; the banner stays hidden here.
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

    auto *title = new QLabel("Check payout-address eligibility");
    title->setObjectName("walletVerifyTitle");
    auto *body = new QLabel(
        "Some community reward policies require an active public address. "
        "ForkMesh can validate its format and publish the existing signed node "
        "heartbeat, but neither proves wallet control nor guarantees selection "
        "or payment.<br><b>Non-custodial:</b> ForkMesh accepts only the public "
        "address here. Never enter a private key or recovery phrase; seeds and "
        "mnemonics are also prohibited.");
    body->setObjectName("walletVerifyBody");
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(2);
    textCol->addWidget(title);
    textCol->addWidget(body);

    auto *verifyButton = new QPushButton("Check settings");
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
        this, "Self-custodial payout address",
        "Enter your public Solana payout address only. Never enter a private "
        "key, seed, mnemonic, or recovery phrase:",
        QLineEdit::Normal,
        current, &ok);
    if (!ok)
        return;
    const QString trimmed = address.trimmed();
    if (!trimmed.isEmpty() &&
        !forkmesh::control::isValidSolanaPublicAddress(trimmed)) {
        flashMessage(
            QStringLiteral(
                "That is not a valid Solana public address. No private key or "
                "recovery phrase was accepted."),
            /*error=*/true);
        return;
    }
    saveSolanaAddress(trimmed);
    if (m_solanaEdit)
        m_solanaEdit->setText(trimmed);
    if (m_settingsSolanaEdit)
        m_settingsSolanaEdit->setText(trimmed);
    // The public address is shared with peers on the next connect; the reward
    // settings and wallet-balance displays pick it up immediately.
    updateSolanaNotice();
    updateHomeStats();
}

void MainWindow::enablePaidMirroring()
{
    // Crypto is strictly opt-in: the core flow (clone, mirror, issues, PRs, chat)
    // never routes here. This page only configures a public payout address for
    // an already registered, locally signable mirror identity. It must never
    // launch the historical reserve/donation/finalize funnel.
    QString address = savedSolanaAddress().trimmed();
    if (address.isEmpty()) {
        promptSetSolanaAddress();
        address = savedSolanaAddress().trimmed();
        if (address.isEmpty())
            return; // user cancelled the address prompt — stay opted out
    }
    if (!forkmesh::control::isValidSolanaPublicAddress(address)) {
        flashMessage(
            QStringLiteral(
                "That saved value is not a valid Solana public address. No "
                "private key, seed, mnemonic, or recovery phrase can be used."),
            /*error=*/true);
        return;
    }

    if (!hasOwnerSigningCapability(accountOwner())) {
        flashMessage(
            QStringLiteral(
                "Register or sign in to this node from Account settings first. "
                "Mirror reward settings never create a deposit wallet, reserve "
                "a paid account, or run a donation flow."),
            /*error=*/true);
        return;
    }

    // Bring the update channels up and (re)publish existing mirrors. A configured
    // address is only one eligibility input; it does not promise a reward.
    startRepoHosts();
    for (int i = 0; i < m_repositories.size(); ++i) {
        if (m_repositories.at(i).publishToNetwork)
            publishRepository(i, /*showDialogOnError=*/false);
    }

    updateSolanaNotice();
    updateHomeStats();
    // Refresh the open profile so the button shows its configured state and the
    // public address/QR/balance section appears next to the username.
    if (m_nodeProfilePanel && m_nodeProfilePanel->isVisible())
        showNodeProfile(m_profileNodeId, m_profileNodeName,
                        /*navigate=*/!profilePanelInSettings());
    flashMessage(QStringLiteral(
        "Reward eligibility configured. Selection and payment are not guaranteed."));
}

void MainWindow::ensureSectionBuilt(int index)
{
    if (!m_sectionStack || index < 0 || index >= m_sectionStack->count())
        return;
    QWidget *placeholder = m_sectionStack->widget(index);
    if (!placeholder ||
        !placeholder->property("forkmeshDeferredSection").toBool())
        return;

    QWidget *section = nullptr;
    switch (index) {
    case 1: {
        auto *scroll = new QScrollArea;
        scroll->setObjectName("settingsScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(0);
        scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        scroll->setWidget(buildSettingsSection());
        section = scroll;
        break;
    }
    case 2: section = buildChatSection(); break;
    case 3: section = buildNotificationsSection(); break;
    case 4: section = buildLogSection(); break;
    case 6: section = buildSearchResultsSection(); break;
    // 7 (Hosts), 8 (Relays) and 13 (Nodes) are tabs of the Network section now
    // (adhoc #54); showSection() redirects them there, so their placeholders
    // stay unbuilt and the fixed stack indexes below keep their meaning.
    case 10: section = buildNodeProfileSection(); break;
    case 11: section = buildNetworkReposSection(); break;
    case 12: section = buildNetworkDiagnosticsSection(); break;
    case 14: section = buildControlNodeSection(); break;
    case 15: section = buildOrganizationTasksSection(); break;
    default: break;
    }
    if (!section)
        return;
    m_sectionStack->insertWidget(index, section);
    m_sectionStack->removeWidget(placeholder);
    placeholder->deleteLater();
}

void MainWindow::showSection(int index)
{
    if (index == 9)
        index = kNetworkDiagnosticsSectionIndex;
    // Hosts (7), Relays (8) and Nodes (13) are tabs of the Network section now
    // (adhoc #54). Their section indexes still work — saved navigation state,
    // the command palette and the control node all still ask for them — they
    // just open Network with the matching tab in front.
    if (index == 7)
        return showNetworkTab(kNetworkHostsTab);
    if (index == 8)
        return showNetworkTab(kNetworkRelaysTab);
    if (index == kNodesSectionIndex)
        return showNetworkTab(kNetworkNodesTab);
    // Section 5 was a retired desktop rankings page. Preserve fixed stack
    // indexes for saved navigation state, but land stale history safely at Home.
    if (index == 5)
        index = 0;
    ensureSectionBuilt(index);
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
    else if (index == 1) {
        // The profile panel may be parked in the full-page section; pull it back
        // into the Profile tab (and refresh it) when that tab is the open one.
        syncSettingsProfileTab();
    } else if (index == 2) {
        // Entering Chat clears the unread marker for the open conversation.
        clearActiveConversationUnread();
    } else if (index == 3) {
        // Opening Pings is the moment the website inbox has to be current
        // (adhoc #59); refreshWebAlerts() repaints the table when it lands.
        refreshWebAlerts();
        refreshNotificationsTable();
    } else if (index == 4 && m_settingsLog) {
        // First visit renders the persisted history that buildLogSection()
        // deliberately skipped (see m_networkLogViewStale) — the rebuild replays
        // the newest segment of the in-memory buffer, so lines appended live
        // since launch keep their place in order.
        if (m_networkLogViewStale) {
            m_networkLogViewStale = false;
            rebuildNetworkLogView();
        }
        // Jump to the newest log line whenever the Log section opens.
        m_settingsLog->moveCursor(QTextCursor::End);
    } else if (index == 10) {
        // Back/Forward can land here directly while the panel is on loan to the
        // Settings > Profile tab; bring it home so the page isn't blank.
        hostNodeProfilePanel(false);
    } else if (index == kNetworkReposSectionIndex) {
        refreshNetworkReposPage();
    } else if (index == kNetworkDiagnosticsSectionIndex) {
        refreshFirewallTables();
        refreshNetworkDiagnostics();
    } else if (index == kControlNodeSectionIndex) {
        refreshControlNode();
    } else if (index == kOrganizationTasksSectionIndex) {
        refreshOrganizationTasks();
    }
}

void MainWindow::showNetworkTab(int tabIndex)
{
    showSection(kNetworkDiagnosticsSectionIndex);
    if (!m_networkTabs || tabIndex < 0 || tabIndex >= m_networkTabs->count())
        return;
    const bool alreadyOpen = m_networkTabs->currentIndex() == tabIndex;
    m_networkTabs->setCurrentIndex(tabIndex);
    // A real tab change refreshes through currentChanged; re-opening the tab
    // that is already in front does not, so do it here instead of twice.
    if (alreadyOpen)
        refreshNetworkTab(tabIndex);
}

void MainWindow::refreshNetworkTab(int tabIndex)
{
    switch (tabIndex) {
    case kNetworkRelaysTab:
        // Re-list and re-probe the relays each time the Relays tab opens.
        refreshRelaysTable();
        break;
    case kNetworkNodesTab:
        refreshNodesTable();
        break;
    case kNetworkHostsTab:
        // Re-read the saved host list whenever the Hosts tab opens.
        refreshHostsTable();
        break;
    default:
        // The diagnostics/firewall tabs all read the same two refreshes, which
        // showSection() already runs when the section itself opens.
        refreshFirewallTables();
        refreshNetworkDiagnostics();
        break;
    }
}

// --- Network repositories ---------------------------------------------------

namespace {
// Repos table columns (adhoc #118). The four interactive columns stay first so
// a row's buttons remain reachable without scrolling sideways; every other fact
// the catalog publishes about a repository follows, so this page shows all the
// data we hold for all repos in one compact grid. Two things are deliberately
// left out: the description (prose for the repository's own page, not a grid
// cell) and raw signatures / private-archive locators (proof material rather
// than repository facts — their digests are shown instead).
enum NetworkRepoCol {
    kRepoColName = 0,
    kRepoColLocalFork,
    kRepoColMirrors,
    kRepoColActions,
    kRepoColVisibility,
    kRepoColTerms,
    kRepoColLive,
    kRepoColBranch,
    kRepoColCommit,
    kRepoColCommitSubject,
    kRepoColCommitAuthor,
    kRepoColCommitAt,
    kRepoColIssues,
    kRepoColIssueMax,
    kRepoColCommits,
    kRepoColBranches,
    kRepoColPulls,
    kRepoColDiscussions,
    kRepoColWorktrees,
    kRepoColArtifacts,
    kRepoColActivity,
    kRepoColChangedFiles,
    kRepoColSize,
    kRepoColSource,
    kRepoColHosts,
    kRepoColMachine,
    kRepoColRuntime,
    kRepoColPlatform,
    kRepoColVersion,
    kRepoColAgents,
    kRepoColCi,
    kRepoColCpu,
    kRepoColMemory,
    kRepoColDisk,
    kRepoColClonesServed,
    kRepoColWebsiteServed,
    kRepoColEncryption,
    kRepoColChannel,
    kRepoColSolana,
    kRepoColHostedSince,
    kRepoColLastSync,
    kRepoColUpdated,
    kRepoColRootCommit,
    kRepoColStateHash,
    kRepoColMaintainer,
    kRepoColNodeId,
    kRepoColCloneUrl,
    kRepoColSshUrl,
    kRepoColCount,
};

// Header labels, index-aligned with NetworkRepoCol.
QStringList networkRepoHeaders()
{
    return {QStringLiteral("Repository"),   QStringLiteral("Local fork"),
            QStringLiteral("Mirrors"),      QStringLiteral("Actions"),
            QStringLiteral("Visibility"),   QStringLiteral("Terms"),
            QStringLiteral("Live host"),    QStringLiteral("Branch"),
            QStringLiteral("Commit"),       QStringLiteral("Subject"),
            QStringLiteral("Author"),       QStringLiteral("Last commit"),
            QStringLiteral("Issues"),       QStringLiteral("Max issue"),
            QStringLiteral("Commits"),      QStringLiteral("Branches"),
            QStringLiteral("Pulls"),        QStringLiteral("Discussions"),
            QStringLiteral("Worktrees"),    QStringLiteral("Artifacts"),
            QStringLiteral("Activity 52w"), QStringLiteral("Changed files"),
            QStringLiteral("Size"),         QStringLiteral("Source"),
            QStringLiteral("Hosts"),        QStringLiteral("Machine"),
            QStringLiteral("Runtime"),      QStringLiteral("Platform"),
            QStringLiteral("Version"),      QStringLiteral("Agents"),
            QStringLiteral("Actions (CI)"), QStringLiteral("CPU"),
            QStringLiteral("RAM"),          QStringLiteral("Disk"),
            QStringLiteral("Clones served"),
            QStringLiteral("Website served"),
            QStringLiteral("Encryption"),   QStringLiteral("Channel"),
            QStringLiteral("Solana"),       QStringLiteral("Hosted since"),
            QStringLiteral("Last sync"),    QStringLiteral("Updated"),
            QStringLiteral("Root commit"),  QStringLiteral("State hash"),
            QStringLiteral("Maintainer"),   QStringLiteral("Node id"),
            QStringLiteral("Clone URL"),    QStringLiteral("SSH URL")};
}
} // namespace

QWidget *MainWindow::buildNetworkReposSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(20, 18, 20, 20);
    outer->setSpacing(10);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Repos"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch();

    m_networkReposStatus = new QLabel(QStringLiteral("Not loaded"));
    m_networkReposStatus->setObjectName("mutedLabel");
    header->addWidget(m_networkReposStatus);

    // Create a brand-new repository right from the Repos tab. Reuses the shared
    // New repository dialog (name/description/first prompt/visibility/README/
    // location), so
    // the "info needed to create a repo" is shown inline instead of buried in
    // Settings.
    auto *newRepoButton = new QPushButton(QStringLiteral("New repository\xE2\x80\xA6"));
    newRepoButton->setObjectName("primaryButton");
    newRepoButton->setCursor(Qt::PointingHandCursor);
    newRepoButton->setToolTip(QStringLiteral("Create a brand-new repository"));
    setOcticon(newRepoButton, "repo", 14);
    connect(newRepoButton, &QPushButton::clicked, this,
            &MainWindow::createNewRepository);
    header->addWidget(newRepoButton);

    m_networkReposRefreshButton = new QPushButton(QStringLiteral("Refresh"));
    m_networkReposRefreshButton->setObjectName("ghostButton");
    m_networkReposRefreshButton->setCursor(Qt::PointingHandCursor);
    m_networkReposRefreshButton->setToolTip(QStringLiteral("Refresh network repositories"));
    setOcticon(m_networkReposRefreshButton, "sync", 14);
    connect(m_networkReposRefreshButton, &QPushButton::clicked, this,
            &MainWindow::refreshNetworkReposPage);
    header->addWidget(m_networkReposRefreshButton);
    outer->addLayout(header);

    auto *subtitle = new QLabel(QStringLiteral(
        "Repositories grouped by user or organization across the active relay. "
        "Mirror hosts are combined into one repository row, and every field the "
        "catalog publishes about a repository has its own column \xE2\x80\x94 "
        "use a column's \xE2\x8B\xAF menu to hide, move or sort by it."));
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);

    m_networkReposTable = new QTableWidget(0, kRepoColCount);
    installColumnHeaderMenu(m_networkReposTable);
    m_networkReposTable->setObjectName("issueTable");
    m_networkReposTable->setHorizontalHeaderLabels(networkRepoHeaders());
    m_networkReposTable->verticalHeader()->setVisible(false);
    m_networkReposTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_networkReposTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_networkReposTable->setShowGrid(false);
    m_networkReposTable->setWordWrap(false);
    m_networkReposTable->setAlternatingRowColors(true);
    m_networkReposTable->setSortingEnabled(false);
    m_networkReposTable->setHorizontalScrollMode(
        QAbstractItemView::ScrollPerPixel);
    m_networkReposTable->horizontalHeader()->setStretchLastSection(false);
    // Every column sizes to its own content: with the full field set in play a
    // stretched first column would just push the data columns off-screen, and
    // makeColumnsResizable() turns these into draggable Interactive ones as
    // soon as the first rows land.
    for (int c = 0; c < kRepoColCount; ++c)
        m_networkReposTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    // One line per repository now that the commit/branch/state details each
    // have a column, so a wide grid still reads as a compact list.
    m_networkReposTable->verticalHeader()->setDefaultSectionSize(34);
    m_networkReposTable->verticalHeader()->setMinimumSectionSize(28);
    makeColumnsResizable(m_networkReposTable);
    connect(m_networkReposTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int column) {
                if (column == kRepoColActions || !m_networkReposTable)
                    return;
                QTableWidgetItem *item =
                    m_networkReposTable->item(row, kRepoColName);
                if (!item)
                    return;
                const QString key = item->data(Qt::UserRole).toString();
                const QString owner =
                    item->data(Qt::UserRole + 3).toString();
                const QString name =
                    item->data(Qt::UserRole + 4).toString();
                if (owner.isEmpty() || name.isEmpty())
                    return;
                openNetworkRepo(owner, name,
                                item->data(Qt::UserRole + 1).toString(),
                                item->data(Qt::UserRole + 2).toBool());
            });
    outer->addWidget(m_networkReposTable, 1);

    // Node -> user ownership lets renderNetworkRepos collapse machine
    // namespaces into user namespaces even when those nodes are offline.
    refreshChatUserDirectory();
    return page;
}

void MainWindow::refreshNetworkReposPage()
{
    if (!m_networkReposTable || !m_networkReposStatus)
        return;
    if (!m_networkAccess) {
        m_networkReposStatus->setText(QStringLiteral("Network is not connected."));
        return;
    }

    const int generation = ++m_networkReposLoadGen;
    m_networkReposTable->setRowCount(0);
    m_networkReposStatus->setText(QStringLiteral("Loading repositories..."));
    if (m_networkReposRefreshButton)
        m_networkReposRefreshButton->setEnabled(false);

    QNetworkRequest request(catalogListUrl());
    request.setRawHeader("accept", "application/json");
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();
        if (generation != m_networkReposLoadGen)
            return;
        if (m_networkReposRefreshButton)
            m_networkReposRefreshButton->setEnabled(true);
        if (error != QNetworkReply::NoError) {
            if (m_networkReposStatus)
                m_networkReposStatus->setText(
                    QStringLiteral("Repositories unavailable: %1").arg(errorString));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        const QJsonArray catalog = obj.value("repositories").toArray();

        // Public organization aliases are routing identities (for example
        // forkmesh/forkmesh backed by jett/forkmesh). Fold those aliases into
        // the catalog before rendering so the backing node never becomes the
        // repository's visible owner.
        QUrl orgsUrl = catalogApiUrl();
        orgsUrl.setPath(QStringLiteral("/api/world/organizations"));
        orgsUrl.setQuery(QString());
        QNetworkReply *orgsReply =
            m_networkAccess->get(QNetworkRequest(orgsUrl));
        connect(orgsReply, &QNetworkReply::finished, this,
                [this, orgsReply, catalog, generation] {
            const QByteArray orgBody = orgsReply->readAll();
            const bool orgsOk =
                orgsReply->error() == QNetworkReply::NoError;
            orgsReply->deleteLater();
            if (generation != m_networkReposLoadGen)
                return;
            const QJsonArray organizations =
                QJsonDocument::fromJson(orgBody)
                    .object()
                    .value(QStringLiteral("organizations"))
                    .toArray();
            if (!orgsOk || organizations.isEmpty()) {
                renderNetworkRepos(catalog);
                return;
            }

            auto combined = std::make_shared<QJsonArray>(catalog);
            auto pending = std::make_shared<int>(0);
            for (const QJsonValue &value : organizations) {
                const QString organization =
                    value.toObject().value(QStringLiteral("name"))
                        .toString().trimmed().toLower();
                if (organization.isEmpty())
                    continue;
                ++*pending;
                QUrl reposUrl = catalogApiUrl();
                reposUrl.setPath(QStringLiteral("/api/orgs/%1/repos")
                                     .arg(organization));
                reposUrl.setQuery(QString());
                QNetworkReply *reposReply =
                    m_networkAccess->get(QNetworkRequest(reposUrl));
                connect(reposReply, &QNetworkReply::finished, this,
                        [this, reposReply, catalog, combined, pending,
                         organization, generation] {
                    const QByteArray repoBody = reposReply->readAll();
                    const bool reposOk =
                        reposReply->error() == QNetworkReply::NoError;
                    reposReply->deleteLater();
                    if (generation != m_networkReposLoadGen)
                        return;
                    if (reposOk) {
                        const QJsonArray aliases =
                            QJsonDocument::fromJson(repoBody)
                                .object()
                                .value(QStringLiteral("repos"))
                                .toArray();
                        for (const QJsonValue &aliasValue : aliases) {
                            const QJsonObject alias = aliasValue.toObject();
                            const QString name =
                                alias.value(QStringLiteral("repo"))
                                    .toString().trimmed();
                            const QString node =
                                alias.value(QStringLiteral("node"))
                                    .toString().trimmed();
                            if (name.isEmpty() || node.isEmpty())
                                continue;
                            for (const QJsonValue &catalogValue : catalog) {
                                const QJsonObject base =
                                    catalogValue.toObject();
                                if (base.value(QStringLiteral("owner"))
                                            .toString()
                                            .compare(node,
                                                     Qt::CaseInsensitive) != 0 ||
                                    base.value(QStringLiteral("name"))
                                            .toString()
                                            .compare(name,
                                                     Qt::CaseInsensitive) != 0)
                                    continue;
                                QJsonObject publicAlias = base;
                                publicAlias.insert(QStringLiteral("owner"),
                                                   organization);
                                publicAlias.insert(QStringLiteral("name"), name);
                                publicAlias.insert(
                                    QStringLiteral("source"),
                                    QStringLiteral("organization-alias"));
                                publicAlias.insert(
                                    QStringLiteral("servingOwner"), node);
                                publicAlias.insert(
                                    QStringLiteral("cloneUrl"),
                                    hostedCloneUrl(organization, name));
                                combined->append(publicAlias);
                                break;
                            }
                        }
                    }
                    --*pending;
                    if (*pending == 0)
                        renderNetworkRepos(*combined);
                });
            }
            if (*pending == 0)
                renderNetworkRepos(catalog);
        });
    });
}

int MainWindow::findNetworkRepoIndex(const QString &owner, const QString &name,
                                     bool includePreview) const
{
    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (repo.owner == owner && repo.name == name &&
            (includePreview || !repo.previewOnly))
            return i;
    }
    return -1;
}

int MainWindow::findNetworkLocalForkIndex(const QString &owner,
                                          const QString &name) const
{
    const QString localOwner = accountOwner();
    if (!localOwner.isEmpty()) {
        const int forkIndex = findNetworkRepoIndex(localOwner, name, false);
        if (forkIndex >= 0 && !m_repositories.at(forkIndex).localPath.isEmpty())
            return forkIndex;
    }

    for (int i = 0; i < m_repositories.size(); ++i) {
        const RepositoryRecord &repo = m_repositories.at(i);
        if (!repo.previewOnly && repo.owner == owner && repo.name == name &&
            !repo.localPath.isEmpty())
            return i;
    }
    return -1;
}

void MainWindow::renderNetworkRepos(const QJsonArray &repos)
{
    if (!m_networkReposTable || !m_networkReposStatus)
        return;
    m_networkReposLastPayload = repos;

    // Public user-directory ownership turns machine namespaces back into the
    // account that owns them. Organization aliases inserted by
    // refreshNetworkReposPage take precedence over this map below.
    QHash<QString, QString> nodeOwner;
    for (const MemberInfo &user : std::as_const(m_chatDirectoryUsers)) {
        const QString owner = user.name.trimmed();
        for (const QString &node :
             user.nodeName.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
            const QString key = node.trimmed().toLower();
            if (!key.isEmpty())
                nodeOwner.insert(key, owner);
        }
    }

    QList<QJsonObject> records;
    QSet<QString> seenRecords;
    m_privateCatalogAccessIds.clear();
    static const QRegularExpression opaqueAccessIdPattern(
        QStringLiteral("^[0-9a-f]{64}$"));
    for (const QJsonValue &value : repos) {
        QJsonObject repo = value.toObject();
        const QString owner = repo.value("owner").toString().trimmed();
        const QString name = repo.value("name").toString().trimmed();
        if (owner.isEmpty() || name.isEmpty())
            continue;
        const QString recordKey =
            owner.toLower() + QLatin1Char('/') + name.toLower() +
            QLatin1Char('|') + repo.value("source").toString() +
            QLatin1Char('|') + repo.value("servingOwner").toString();
        if (seenRecords.contains(recordKey))
            continue;
        seenRecords.insert(recordKey);
        const QString key = owner + "/" + name;
        const bool isPrivate =
            repo.value("private").toBool(false) ||
            repo.value("isPrivate").toBool(false);
        if (isPrivate) {
            const QString accessId =
                repo.value(QStringLiteral("privateAccessId"))
                    .toString()
                    .trimmed()
                    .toLower();
            const QString accessPath =
                repo.value(QStringLiteral("privateAccessPath"))
                    .toString()
                    .trimmed();
            const QString expectedPath =
                QStringLiteral("/api/private-replicas/") + accessId;
            if (opaqueAccessIdPattern.match(accessId).hasMatch() &&
                (accessPath.isEmpty() || accessPath == expectedPath)) {
                m_privateCatalogAccessIds.insert(key, accessId);
            }
        }
        QString cloneUrl = repo.value("cloneUrl").toString().trimmed();
        if (cloneUrl.isEmpty())
            cloneUrl = repo.value("clone_url").toString().trimmed();
        if (cloneUrl.isEmpty())
            cloneUrl = hostedCloneUrl(owner, name);
        repo.insert("cloneUrl", cloneUrl);
        records.append(repo);
    }

    // A root commit is the catalog's logical repository identity. Legacy
    // records without one fold into the rooted group with the same repo name,
    // matching the web catalog's compatibility behavior.
    QHash<QString, QString> rootedByName;
    for (const QJsonObject &repo : std::as_const(records)) {
        const QString root =
            repo.value("rootCommit").toString().trimmed().toLower();
        const QString name =
            repo.value("name").toString().trimmed().toLower();
        if (!root.isEmpty() && !name.isEmpty() && !rootedByName.contains(name))
            rootedByName.insert(name, root);
    }
    QList<QList<QJsonObject>> groups;
    QHash<QString, int> groupIndexes;
    for (const QJsonObject &repo : std::as_const(records)) {
        const QString name =
            repo.value("name").toString().trimmed().toLower();
        QString root =
            repo.value("rootCommit").toString().trimmed().toLower();
        if (root.isEmpty())
            root = rootedByName.value(name);
        const QString groupKey = root.isEmpty()
                                     ? QStringLiteral("name:") + name
                                     : QStringLiteral("root:") + root;
        int index = groupIndexes.value(groupKey, -1);
        if (index < 0) {
            index = groups.size();
            groupIndexes.insert(groupKey, index);
            groups.append(QList<QJsonObject>());
        }
        groups[index].append(repo);
    }

    auto cloneIdentity = [](const QString &raw) {
        QString path = raw.trimmed();
        if (path.startsWith(QStringLiteral("git@")) && path.contains(':'))
            path = path.section(':', 1);
        else {
            const QUrl url(path);
            if (url.isValid() && !url.path().isEmpty())
                path = url.path();
        }
        path = path.section('?', 0, 0).section('#', 0, 0);
        const QStringList parts =
            path.split('/', Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return QPair<QString, QString>();
        QString name = parts.last();
        if (name.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive))
            name.chop(4);
        return qMakePair(parts.at(parts.size() - 2), name);
    };

    QList<QJsonObject> rows;
    QSet<QString> shownKeys;
    for (const QList<QJsonObject> &group : std::as_const(groups)) {
        if (group.isEmpty())
            continue;
        int best = 0;
        int bestScore = std::numeric_limits<int>::min();
        for (int i = 0; i < group.size(); ++i) {
            const QJsonObject &candidate = group.at(i);
            const QString source =
                candidate.value("source").toString().trimmed();
            const QString owner =
                candidate.value("owner").toString().trimmed().toLower();
            int score = 0;
            if (source == QLatin1String("organization-alias"))
                score += 1000;
            if (source == QLatin1String("local-node"))
                score += 100;
            if (!nodeOwner.contains(owner))
                score += 20;
            if (candidate.value("liveHost").toBool(false))
                score += 5;
            if (score > bestScore) {
                best = i;
                bestScore = score;
            }
        }

        QJsonObject canonical = group.at(best);
        const QString source =
            canonical.value("source").toString().trimmed();
        const QString rawOwner =
            canonical.value("owner").toString().trimmed();
        QString displayOwner = rawOwner;
        QString routeOwner = rawOwner;
        QString routeName =
            canonical.value("name").toString().trimmed();
        if (source == QLatin1String("organization-alias")) {
            routeOwner = displayOwner;
        } else if (source == QLatin1String("remote-clone")) {
            const auto identity =
                cloneIdentity(canonical.value("cloneUrl").toString());
            if (!identity.first.isEmpty() && !identity.second.isEmpty()) {
                displayOwner = identity.first;
                routeOwner = identity.first;
                routeName = identity.second;
            }
        } else {
            displayOwner =
                nodeOwner.value(rawOwner.toLower(), rawOwner);
        }
        const QString displayName =
            canonical.value("name").toString().trimmed();
        const QString displayKey = displayOwner + "/" + displayName;
        if (displayOwner.isEmpty() || displayName.isEmpty() ||
            shownKeys.contains(displayKey.toLower()))
            continue;
        shownKeys.insert(displayKey.toLower());

        QJsonArray memberOwners;
        QSet<QString> memberOwnerSet;
        for (const QJsonObject &member : group) {
            QString owner = member.value("servingOwner").toString().trimmed();
            if (owner.isEmpty())
                owner = member.value("owner").toString().trimmed();
            const QString key = owner.toLower();
            if (!owner.isEmpty() && !memberOwnerSet.contains(key)) {
                memberOwnerSet.insert(key);
                memberOwners.append(owner);
            }
        }
        canonical.insert("owner", displayOwner);
        canonical.insert("name", displayName);
        canonical.insert("_routeOwner", routeOwner);
        canonical.insert("_routeName", routeName);
        canonical.insert("_memberOwners", memberOwners);
        rows.append(canonical);
    }

    std::sort(rows.begin(), rows.end(), [](const QJsonObject &a,
                                           const QJsonObject &b) {
        const QString ak = a.value("owner").toString() + "/" +
                           a.value("name").toString();
        const QString bk = b.value("owner").toString() + "/" +
                           b.value("name").toString();
        return ak.compare(bk, Qt::CaseInsensitive) < 0;
    });

    m_networkReposTable->setRowCount(rows.size());
    const int generation = m_networkReposLoadGen;
    for (int row = 0; row < rows.size(); ++row) {
        const QJsonObject repo = rows.at(row);
        const QString owner = repo.value("owner").toString().trimmed();
        const QString name = repo.value("name").toString().trimmed();
        const QString key = owner + "/" + name;
        const QString routeOwner =
            repo.value("_routeOwner").toString(owner).trimmed();
        const QString routeName =
            repo.value("_routeName").toString(name).trimmed();
        const QString cloneUrl = repo.value("cloneUrl").toString().trimmed();
        const bool isPrivate = repo.value("private").toBool(false) ||
                               repo.value("isPrivate").toBool(false);
        const QString commit = repo.value("commit").toString().trimmed();
        const QString branch = repo.value("branch").toString().trimmed();
        // The catalog publishes the HEAD commit date as epoch milliseconds, as
        // a string on nodes that advertise it (older nodes omit the key).
        const QJsonValue commitAtValue = repo.value(QStringLiteral("commitAt"));
        const qint64 commitAtMs =
            commitAtValue.isDouble()
                ? qint64(commitAtValue.toDouble())
                : commitAtValue.toString().trimmed().toLongLong();

        // Commit / branch / privacy each have their own column now, so the row
        // itself is one line: the repository name. No "about" blurb either —
        // the description is prose that belongs on the repository's own page.
        auto *repoItem = new QTableWidgetItem(key);
        repoItem->setData(Qt::UserRole, key);
        repoItem->setData(Qt::UserRole + 1, cloneUrl);
        repoItem->setData(Qt::UserRole + 2, isPrivate);
        repoItem->setData(Qt::UserRole + 3, routeOwner);
        repoItem->setData(Qt::UserRole + 4, routeName);
        QStringList repoToolTip;
        repoToolTip << key;
        if (!commit.isEmpty())
            repoToolTip << QStringLiteral("Commit: %1").arg(commit);
        if (!branch.isEmpty())
            repoToolTip << QStringLiteral("Branch: %1").arg(branch);
        if (commitAtMs > 0)
            repoToolTip << QStringLiteral("Last commit: %1")
                               .arg(formatRepoDate(commitAtMs));
        if (!cloneUrl.isEmpty())
            repoToolTip << cloneUrl;
        repoItem->setToolTip(repoToolTip.join('\n'));
        m_networkReposTable->setItem(row, kRepoColName, repoItem);

        int localFork = -1;
        int mirroredIndex = -1;
        for (const QJsonValue &memberOwner :
             repo.value("_memberOwners").toArray()) {
            const QString member = memberOwner.toString();
            if (localFork < 0)
                localFork = findNetworkLocalForkIndex(member, name);
            if (mirroredIndex < 0)
                mirroredIndex = findNetworkRepoIndex(member, name, false);
        }
        if (localFork < 0)
            localFork = findNetworkLocalForkIndex(routeOwner, routeName);
        if (mirroredIndex < 0)
            mirroredIndex =
                findNetworkRepoIndex(routeOwner, routeName, false);
        if (localFork >= 0) {
            const RepositoryRecord &fork = m_repositories.at(localFork);
            const QString path = fork.localPath.isEmpty()
                                     ? QStringLiteral("No checkout path")
                                     : QDir::toNativeSeparators(fork.localPath);
            auto *forkItem = new QTableWidgetItem(QStringLiteral("Available"));
            forkItem->setToolTip(
                QStringLiteral("%1/%2\n%3").arg(fork.owner, fork.name, path));
            m_networkReposTable->setItem(row, kRepoColLocalFork, forkItem);
        } else {
            auto *forkItem = new QTableWidgetItem(QStringLiteral("Not local"));
            forkItem->setForeground(QColor("#8b949e"));
            m_networkReposTable->setItem(row, kRepoColLocalFork, forkItem);
        }

        auto *mirrorsItem = new QTableWidgetItem(
            QString::fromUtf8("\xE2\x80\xA6"));
        mirrorsItem->setForeground(QColor("#8b949e"));
        mirrorsItem->setTextAlignment(Qt::AlignCenter);
        m_networkReposTable->setItem(row, kRepoColMirrors, mirrorsItem);

        fillNetworkRepoDataCells(row, repo);

        auto *actions = new QWidget;
        auto *actionRow = new QHBoxLayout(actions);
        actionRow->setContentsMargins(4, 2, 4, 2);
        actionRow->setSpacing(6);

        // Three groups, left to right: switch to it, get a copy of it
        // (fork/mirror), then — set apart by a gap — remove this machine's copy.
        auto *openButton = new QPushButton(QStringLiteral("Switch"));
        openButton->setObjectName("primaryButton");
        openButton->setCursor(Qt::PointingHandCursor);
        openButton->setToolTip(
            localFork >= 0 ? QStringLiteral("Switch to your local copy")
                           : QStringLiteral("Switch to this repository"));
        setOcticon(openButton, localFork >= 0 ? "repo-forked" : "repo", 13);
        connect(openButton, &QPushButton::clicked, this,
                [this, routeOwner, routeName, cloneUrl, isPrivate, localFork] {
                    if (localFork >= 0)
                        openRepoDetail(localFork);
                    else
                        openNetworkRepo(routeOwner, routeName, cloneUrl,
                                        isPrivate);
                });
        actionRow->addWidget(openButton);

        auto *forkButton = new QPushButton(localFork >= 0
                                               ? QStringLiteral("Forked")
                                               : QStringLiteral("Fork"));
        forkButton->setCursor(Qt::PointingHandCursor);
        forkButton->setToolTip(localFork >= 0 ? QStringLiteral("You already have a local fork")
                                              : QStringLiteral("Fork into your own node"));
        setOcticon(forkButton, "repo-forked", 13);
        if (localFork >= 0) {
            forkButton->setEnabled(false);
        } else {
            forkButton->setObjectName("ghostButton");
            connect(forkButton, &QPushButton::clicked, this,
                    [this, routeOwner, routeName, cloneUrl, isPrivate] {
                        forkNetworkRepo(routeOwner, routeName, cloneUrl,
                                        isPrivate);
                    });
        }
        actionRow->addWidget(forkButton);

        auto *mirrorButton = new QPushButton(mirroredIndex >= 0
                                                 ? QStringLiteral("Mirrored")
                                                 : QStringLiteral("Mirror"));
        mirrorButton->setObjectName("ghostButton");
        mirrorButton->setCursor(Qt::PointingHandCursor);
        mirrorButton->setToolTip(mirroredIndex >= 0
                                     ? QStringLiteral("This machine already mirrors it")
                                     : QStringLiteral("Mirror this repository on this machine"));
        setOcticon(mirrorButton, "sync", 13);
        if (mirroredIndex >= 0) {
            mirrorButton->setEnabled(false);
        } else {
            connect(mirrorButton, &QPushButton::clicked, this,
                    [this, routeOwner, routeName, cloneUrl, isPrivate] {
                        mirrorNetworkRepo(routeOwner, routeName, cloneUrl,
                                          isPrivate);
                    });
        }
        actionRow->addWidget(mirrorButton);

        // Same repository on the public website — useful for sharing a link or
        // browsing it without a local copy. Routed by the repo's own owner, so
        // mirrored rows still open the hosting node's page.
        auto *webButton = new QPushButton(QStringLiteral("Web"));
        webButton->setObjectName("ghostButton");
        webButton->setCursor(Qt::PointingHandCursor);
        webButton->setToolTip(
            QStringLiteral("View %1/%2 on the website")
                .arg(routeOwner, routeName));
        setOcticon(webButton, "link", 13);
        connect(webButton, &QPushButton::clicked, this,
                [this, routeOwner, routeName] {
                    const QUrl url(repositoryWebUrl(routeOwner, routeName));
                    if (url.isValid() && !url.host().isEmpty())
                        QDesktopServices::openUrl(url);
                });
        actionRow->addWidget(webButton);

        // Delete this machine's copy without first opening the repository and
        // digging into its Settings tab. The row already resolved which record
        // is local across every owner it groups — mirror first, then fork; with
        // neither, there is nothing here to delete.
        const int deleteIndex = mirroredIndex >= 0 ? mirroredIndex : localFork;
        auto *deleteButton = new QPushButton(QStringLiteral("Delete"));
        deleteButton->setObjectName("dangerButton");
        deleteButton->setCursor(Qt::PointingHandCursor);
        setOcticon(deleteButton, "trash", 13);
        if (deleteIndex < 0) {
            deleteButton->setEnabled(false);
            deleteButton->setToolTip(
                QStringLiteral("This repository is not on this machine"));
        } else {
            const QString targetOwner = m_repositories.at(deleteIndex).owner;
            const QString targetName = m_repositories.at(deleteIndex).name;
            deleteButton->setToolTip(
                QStringLiteral("Delete %1/%2 from this machine")
                    .arg(targetOwner, targetName));
            connect(deleteButton, &QPushButton::clicked, this,
                    [this, targetOwner, targetName] {
                        // Re-resolve by owner/name rather than capturing the
                        // index: it is only valid for the m_repositories
                        // snapshot this row was built from, which a
                        // fork/mirror/delete elsewhere may since have shifted.
                        const int index =
                            findNetworkRepoIndex(targetOwner, targetName, true);
                        if (index >= 0)
                            deleteRepositoryAt(index, false);
                        // Next tick: re-listing the table tears down this very
                        // button's row, so don't do it from inside its own
                        // click handler. Runs even when the record had already
                        // gone, so the row stops offering a stale action.
                        QTimer::singleShot(0, this,
                                           [this] { refreshNetworkReposPage(); });
                    });
        }
        actionRow->addSpacing(10);
        actionRow->addWidget(deleteButton);
        actionRow->addStretch();
        m_networkReposTable->setCellWidget(row, kRepoColActions, actions);

        fetchNetworkRepoMirrors(routeOwner, routeName, row, generation);
    }

    m_networkReposStatus->setText(
        rows.isEmpty() ? QStringLiteral("No repositories advertised.")
                       : QStringLiteral("%1 repositories").arg(formatCount(rows.size())));
    // The Repos rail badge counts what this page lists, not this machine's own
    // copies (adhoc #118).
    m_networkRepoRowCount = rows.size();
    updateReposNavBadge();
}

// Fills every catalog-data column of one Repos row: the repository's own facts
// (visibility, HEAD, counts, size) plus the publishing node's advertised state
// (machine, platform, telemetry, serve counters). Values the node never
// reported render as an em dash rather than a misleading zero.
void MainWindow::fillNetworkRepoDataCells(int row, const QJsonObject &repo)
{
    if (!m_networkReposTable)
        return;
    const QString dash = QString::fromUtf8("\xE2\x80\x94");

    // Numbers reach us as JSON numbers on some fields and as strings on others
    // (the catalog stores the node-reported counters as text); an invalid
    // QVariant means "never reported", which is not the same as zero.
    auto number = [&repo](const QString &key) {
        const QJsonValue value = repo.value(key);
        if (value.isDouble())
            return QVariant(qint64(value.toDouble()));
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
        return ok ? QVariant(parsed) : QVariant();
    };
    auto text = [&repo](const QString &key) {
        return repo.value(key).toString().trimmed();
    };
    auto textCell = [&](int column, const QString &value,
                        const QString &tip = QString()) {
        auto *item = new QTableWidgetItem(value.isEmpty() ? dash : value);
        if (value.isEmpty())
            item->setForeground(QColor("#8b949e"));
        else if (!tip.isEmpty())
            item->setToolTip(tip);
        m_networkReposTable->setItem(row, column, item);
    };
    // Hashes, keys and addresses are shown by their leading characters with the
    // full value on hover, so one long field can't blow out the grid.
    auto digestCell = [&](int column, const QString &value,
                          const QString &extraTip = QString()) {
        QString tip = value;
        if (!extraTip.isEmpty())
            tip += QLatin1Char('\n') + extraTip;
        textCell(column, value.left(12), tip);
    };
    auto countCell = [&](int column, const QString &key,
                         const QString &tip = QString()) {
        const QVariant value = number(key);
        auto *item = new QTableWidgetItem;
        if (value.isValid()) {
            item->setData(Qt::DisplayRole, value.toLongLong());
            if (!tip.isEmpty())
                item->setToolTip(tip);
        } else {
            item->setText(dash);
            item->setForeground(QColor("#8b949e"));
        }
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_networkReposTable->setItem(row, column, item);
    };
    // Timestamps are epoch milliseconds (as text on the fields the node signs);
    // the column reads relatively, the exact stamp is on hover.
    auto timeCell = [&](int column, const QString &key) {
        const QVariant value = number(key);
        const qint64 ms = value.toLongLong();
        if (!value.isValid() || ms <= 0) {
            textCell(column, QString());
            return;
        }
        textCell(column, formatIssueRelativeTime(ms), formatRepoDate(ms));
    };
    auto usageText = [&](const QString &usedKey, const QString &totalKey) {
        const QVariant used = number(usedKey);
        const QVariant total = number(totalKey);
        if (!used.isValid() || !total.isValid() || total.toLongLong() <= 0)
            return QString();
        return QStringLiteral("%1 / %2")
            .arg(SystemStats::formatBytes(used.toLongLong()),
                 SystemStats::formatBytes(total.toLongLong()));
    };
    auto servedTip = [&](const QString &atKey, const QString &agentKey) {
        QStringList tip;
        const QVariant at = number(atKey);
        if (at.isValid() && at.toLongLong() > 0)
            tip << QStringLiteral("Last: %1").arg(formatRepoDate(at.toLongLong()));
        const QString agent = text(agentKey);
        if (!agent.isEmpty())
            tip << QStringLiteral("Client: %1").arg(agent);
        return tip.join(QLatin1Char('\n'));
    };
    auto joinArray = [&repo](const QString &key) {
        QStringList values;
        for (const QJsonValue &value : repo.value(key).toArray()) {
            const QString entry = value.toString().trimmed();
            if (!entry.isEmpty())
                values << entry;
        }
        return values;
    };

    const bool isPrivate = repo.value("private").toBool(false) ||
                           repo.value("isPrivate").toBool(false);
    const bool sharedWithMe = repo.value("sharedWithMe").toBool(false);
    textCell(kRepoColVisibility,
             isPrivate ? (sharedWithMe ? QStringLiteral("Private (shared)")
                                       : QStringLiteral("Private"))
                       : QStringLiteral("Public"));
    textCell(kRepoColTerms,
             repo.value("termsFlagged").toBool(false)
                 ? (text(QStringLiteral("termsCategory")).isEmpty()
                        ? QStringLiteral("flagged")
                        : text(QStringLiteral("termsCategory")))
                 : QString());
    textCell(kRepoColLive, repo.value("liveHost").toBool(false)
                               ? QStringLiteral("Live")
                               : QString());

    textCell(kRepoColBranch, text(QStringLiteral("branch")));
    digestCell(kRepoColCommit, text(QStringLiteral("commit")));
    const QString subject = text(QStringLiteral("commitSubject"));
    textCell(kRepoColCommitSubject, subject, subject);
    textCell(kRepoColCommitAuthor, text(QStringLiteral("commitAuthorName")));
    timeCell(kRepoColCommitAt, QStringLiteral("commitAt"));

    countCell(kRepoColIssues, QStringLiteral("issueCount"),
              QStringLiteral("Open issues"));
    countCell(kRepoColIssueMax, QStringLiteral("issueMaxNumber"),
              QStringLiteral("Highest issue number ever assigned"));
    countCell(kRepoColCommits, QStringLiteral("commitCount"));
    countCell(kRepoColBranches, QStringLiteral("branchCount"));
    countCell(kRepoColPulls, QStringLiteral("pullCount"));
    countCell(kRepoColDiscussions, QStringLiteral("discussionCount"));
    countCell(kRepoColWorktrees, QStringLiteral("worktreeCount"));
    countCell(kRepoColArtifacts, QStringLiteral("artifactCount"));

    const QJsonArray activity = repo.value("activityWeeks").toArray();
    if (activity.isEmpty()) {
        textCell(kRepoColActivity, QString());
    } else {
        qint64 activityTotal = 0;
        QStringList weeks;
        for (const QJsonValue &week : activity) {
            activityTotal += qint64(week.toDouble());
            weeks << QString::number(qint64(week.toDouble()));
        }
        auto *item = new QTableWidgetItem;
        item->setData(Qt::DisplayRole, activityTotal);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(QStringLiteral("Commits per week (oldest first):\n%1")
                             .arg(weeks.join(QLatin1Char(' '))));
        m_networkReposTable->setItem(row, kRepoColActivity, item);
    }

    const QStringList changedFiles = joinArray(QStringLiteral("changedFiles"));
    if (changedFiles.isEmpty()) {
        textCell(kRepoColChangedFiles, QString());
    } else {
        auto *item = new QTableWidgetItem;
        item->setData(Qt::DisplayRole, qint64(changedFiles.size()));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(QStringLiteral("Uncommitted on the host:\n%1")
                             .arg(changedFiles.join(QLatin1Char('\n'))));
        m_networkReposTable->setItem(row, kRepoColChangedFiles, item);
    }

    const QVariant sizeBytes = number(QStringLiteral("sizeBytes"));
    textCell(kRepoColSize,
             sizeBytes.isValid() && sizeBytes.toLongLong() > 0
                 ? SystemStats::formatBytes(sizeBytes.toLongLong())
                 : QString());
    textCell(kRepoColSource, text(QStringLiteral("source")));
    const QStringList hosts = joinArray(QStringLiteral("_memberOwners"));
    textCell(kRepoColHosts, hosts.join(QStringLiteral(", ")),
             hosts.join(QLatin1Char('\n')));

    textCell(kRepoColMachine, text(QStringLiteral("machineName")));
    textCell(kRepoColRuntime, text(QStringLiteral("runtimeMode")));
    textCell(kRepoColPlatform, text(QStringLiteral("platform")));
    textCell(kRepoColVersion, text(QStringLiteral("version")));
    textCell(kRepoColAgents,
             joinArray(QStringLiteral("agentProviders"))
                 .join(QStringLiteral(", ")));
    QString ci = text(QStringLiteral("actionsState"));
    if (ci.isEmpty() && repo.contains(QStringLiteral("actionsEnabled")))
        ci = repo.value("actionsEnabled").toBool() ? QStringLiteral("enabled")
                                                   : QStringLiteral("disabled");
    textCell(kRepoColCi, ci);

    const QVariant cpuPercent = number(QStringLiteral("cpuPercent"));
    textCell(kRepoColCpu, cpuPercent.isValid()
                              ? QStringLiteral("%1%").arg(cpuPercent.toLongLong())
                              : QString());
    textCell(kRepoColMemory, usageText(QStringLiteral("memUsedBytes"),
                                       QStringLiteral("memTotalBytes")));
    textCell(kRepoColDisk, usageText(QStringLiteral("diskUsedBytes"),
                                     QStringLiteral("diskTotalBytes")));
    countCell(kRepoColClonesServed, QStringLiteral("clonesServed"),
              servedTip(QStringLiteral("cloneServedAt"),
                        QStringLiteral("cloneServedAgent")));
    countCell(kRepoColWebsiteServed, QStringLiteral("websiteServed"),
              servedTip(QStringLiteral("websiteServedAt"),
                        QStringLiteral("websiteServedAgent")));

    QString encryption = text(QStringLiteral("mirrorEncryption"));
    const QVariant keyEpoch = number(QStringLiteral("keyEpoch"));
    if (!encryption.isEmpty() && keyEpoch.isValid())
        encryption += QStringLiteral(" \xC2\xB7 epoch %1").arg(keyEpoch.toLongLong());
    QStringList encryptionTip;
    if (!text(QStringLiteral("opaqueRepoId")).isEmpty())
        encryptionTip << QStringLiteral("Repo id: %1")
                             .arg(text(QStringLiteral("opaqueRepoId")));
    if (!text(QStringLiteral("encryptedManifestHash")).isEmpty())
        encryptionTip << QStringLiteral("Manifest: %1")
                             .arg(text(QStringLiteral("encryptedManifestHash")));
    textCell(kRepoColEncryption, encryption,
             encryptionTip.join(QLatin1Char('\n')));

    textCell(kRepoColChannel, text(QStringLiteral("channel")));
    digestCell(kRepoColSolana, text(QStringLiteral("solana")));
    timeCell(kRepoColHostedSince, QStringLiteral("hostedSince"));
    timeCell(kRepoColLastSync, QStringLiteral("lastSync"));
    timeCell(kRepoColUpdated, QStringLiteral("updatedAt"));
    digestCell(kRepoColRootCommit, text(QStringLiteral("rootCommit")));
    digestCell(kRepoColStateHash, text(QStringLiteral("stateHash")),
               text(QStringLiteral("stateSig")).isEmpty()
                   ? QStringLiteral("Unsigned")
                   : QStringLiteral("Owner-signed"));
    digestCell(kRepoColMaintainer, text(QStringLiteral("maintainer")));
    digestCell(kRepoColNodeId, text(QStringLiteral("nodeId")));
    const QString cloneUrl = text(QStringLiteral("cloneUrl"));
    textCell(kRepoColCloneUrl, cloneUrl, cloneUrl);
    const QString sshUrl = text(QStringLiteral("sshUrl"));
    textCell(kRepoColSshUrl, sshUrl, sshUrl);
}

void MainWindow::fetchNetworkRepoMirrors(const QString &owner, const QString &name,
                                         int row, int generation)
{
    if (!m_networkAccess || !m_networkReposTable || owner.isEmpty() ||
        name.isEmpty())
        return;

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repo/%1/%2/mirrors")
                    .arg(QString::fromUtf8(QUrl::toPercentEncoding(owner)),
                         QString::fromUtf8(QUrl::toPercentEncoding(name))));
    url.setQuery(QString());

    QNetworkRequest request(url);
    request.setRawHeader("accept", "application/json");
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, owner, name, row, generation] {
                const QByteArray body = reply->readAll();
                const QNetworkReply::NetworkError error = reply->error();
                const QString errorString = reply->errorString();
                reply->deleteLater();
                if (generation != m_networkReposLoadGen || !m_networkReposTable ||
                    row < 0 || row >= m_networkReposTable->rowCount())
                    return;
                QTableWidgetItem *repoItem =
                    m_networkReposTable->item(row, kRepoColName);
                if (!repoItem ||
                    repoItem->data(Qt::UserRole + 3).toString()
                            .compare(owner, Qt::CaseInsensitive) != 0 ||
                    repoItem->data(Qt::UserRole + 4).toString()
                            .compare(name, Qt::CaseInsensitive) != 0)
                    return;

                QTableWidgetItem *mirrorsItem =
                    m_networkReposTable->item(row, kRepoColMirrors);
                if (!mirrorsItem)
                    return;

                if (error != QNetworkReply::NoError) {
                    mirrorsItem->setText(QString::fromUtf8("\xE2\x80\x94"));
                    mirrorsItem->setToolTip(errorString);
                    mirrorsItem->setForeground(QColor("#8b949e"));
                    return;
                }

                const QJsonObject obj = QJsonDocument::fromJson(body).object();
                if (obj.contains("ok") && !obj.value("ok").toBool()) {
                    mirrorsItem->setText(QString::fromUtf8("\xE2\x80\x94"));
                    mirrorsItem->setToolTip(QString());
                    mirrorsItem->setForeground(QColor("#8b949e"));
                    return;
                }

                const QJsonArray mirrors = obj.value("mirrors").toArray();
                QStringList mirrorToolTip;
                QSet<QString> countedNodes;
                for (const QJsonValue &value : mirrors) {
                    const QJsonObject mirror = value.toObject();
                    const QString mirrorSource =
                        mirror.value("source").toString().trimmed();
                    const QString mirrorRepo =
                        mirror.value("repo").toString().trimmed();
                    if (mirrorSource == QLatin1String("local-node") ||
                        (mirror.value("owner").toString().compare(owner, Qt::CaseInsensitive) == 0 &&
                         mirrorRepo.compare(name, Qt::CaseInsensitive) == 0))
                        continue;
                    QString node = mirror.value("node").toString().trimmed();
                    if (node.isEmpty())
                        node = mirror.value("owner").toString().trimmed();
                    if (node.isEmpty())
                        node = mirror.value("name").toString().trimmed();
                    if (node.isEmpty())
                        node = mirror.value("id").toString().trimmed();
                    if (node.isEmpty())
                        continue;
                    const QString nodeKey = node.toLower();
                    if (countedNodes.contains(nodeKey))
                        continue;
                    countedNodes.insert(nodeKey);
                    const QString status =
                        mirror.value("status").toString(
                            mirror.value("cloneStatus").toString()).trimmed();
                    const QString commit =
                        mirror.value("commit").toString().trimmed();
                    QString tip = node;
                    if (!status.isEmpty())
                        tip += QStringLiteral(" - %1").arg(status);
                    if (!commit.isEmpty())
                        tip += QStringLiteral(" - commit %1").arg(commit);
                    mirrorToolTip << tip;
                }

                const int count = countedNodes.size();
                mirrorsItem->setData(Qt::DisplayRole, count);
                mirrorsItem->setTextAlignment(Qt::AlignCenter);
                mirrorsItem->setToolTip(
                    count == 0
                        ? QStringLiteral("No mirror nodes yet")
                        : mirrorToolTip.join('\n'));
                mirrorsItem->setForeground(
                    count == 0 ? QBrush(QColor("#8b949e")) : QBrush());
            });
}

void MainWindow::openNetworkRepo(const QString &owner, const QString &name,
                                 const QString &cloneUrl, bool isPrivate)
{
    if (owner.isEmpty() || name.isEmpty())
        return;

    int index = findNetworkRepoIndex(owner, name, true);
    if (index >= 0) {
        if (m_repositories.at(index).isPrivate &&
            PrivateMirrorStore::isOpaqueId(
                m_privateCatalogAccessIds.value(owner + "/" + name))) {
            m_repositories[index].privateReplicaId =
                m_privateCatalogAccessIds.value(owner + "/" + name);
        }
        openRepoDetail(index);
        const RepositoryRecord &repo = m_repositories.at(index);
        if (repo.previewOnly && !m_syncingRepos.contains(index) &&
            !QDir(repo.mirrorPath).exists())
            syncRepository(index);
        return;
    }

    RepositoryRecord repo;
    repo.owner = owner;
    repo.name = name;
    repo.cloneUrl = cloneUrl.isEmpty() ? hostedCloneUrl(owner, name) : cloneUrl;
    repo.isPrivate = isPrivate;
    if (isPrivate) {
        const QString accessId =
            m_privateCatalogAccessIds.value(owner + "/" + name);
        if (PrivateMirrorStore::isOpaqueId(accessId))
            repo.privateReplicaId = accessId;
    }
    repo.previewOnly = true;
    repo.actionsEnabled = false;
    repo.hostedSinceMs = QDateTime::currentMSecsSinceEpoch();
    repo.mirrorPath = repositoryPreviewPath(owner, name);
    m_repositories.append(repo);
    index = m_repositories.size() - 1;
    refreshRepositoryList();
    logSystem("Preview: caching " + owner + "/" + name + " from " + repo.cloneUrl);
    openRepoDetail(index);
    syncRepository(index);
}

void MainWindow::forkNetworkRepo(const QString &owner, const QString &name,
                                 const QString &cloneUrl, bool isPrivate)
{
    const int localFork = findNetworkLocalForkIndex(owner, name);
    if (localFork >= 0) {
        openRepoDetail(localFork);
        return;
    }
    openNetworkRepo(owner, name, cloneUrl, isPrivate);
    QTimer::singleShot(0, this, [this, owner, name] {
        if (m_repoDetailIndex < 0 || m_repoDetailIndex >= m_repositories.size())
            return;
        const RepositoryRecord &repo = m_repositories.at(m_repoDetailIndex);
        if (repo.owner != owner || repo.name != name) {
            flashMessage(QStringLiteral("Open %1/%2 before forking it.")
                             .arg(owner, name),
                         true);
            return;
        }
        forkCurrentRepo();
    });
}

void MainWindow::mirrorNetworkRepo(const QString &owner, const QString &name,
                                   const QString &cloneUrl, bool isPrivate)
{
    if (findNetworkRepoIndex(owner, name, false) >= 0) {
        flashMessage(QStringLiteral("This machine already mirrors %1/%2.")
                         .arg(owner, name));
        refreshNetworkReposPage();
        return;
    }

    const QString source = cloneUrl.isEmpty() ? hostedCloneUrl(owner, name) : cloneUrl;
    if (source.isEmpty()) {
        flashMessage(QStringLiteral("No clone URL is available for %1/%2.")
                         .arg(owner, name),
                     true);
        return;
    }

    mirrorCatalogRepo(owner, name, source, isPrivate);
    flashMessage(QStringLiteral("Mirroring %1/%2.").arg(owner, name));
    refreshNetworkReposPage();
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
    // Compact page chrome (adhoc #315): tighter margins/spacing everywhere so
    // all areas — install form, Vultr provisioning, live output and the host
    // list — fit on screen together, while every hint keeps its full text. It is
    // a tab of the Network section now (adhoc #54), so the page title is gone
    // and the horizontal margins belong to the section, not the page.
    outer->setContentsMargins(0, 8, 0, 0);
    outer->setSpacing(8);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addStretch(1);
    // Bulk one-click install: make every saved host install the current
    // published, checksum-verified release in parallel. This deliberately does
    // not upload QCoreApplication::applicationFilePath(): a developer may be
    // running an un-packaged source build even though it reports the release
    // version.
    m_hostInstallAllButton = new QPushButton(QStringLiteral("Install from binary (all hosts)"));
    m_hostInstallAllButton->setCursor(Qt::PointingHandCursor);
    m_hostInstallAllButton->setToolTip(QStringLiteral(
        "Install the published ForkMesh v" FORKMESH_VERSION
        " binary on every saved host. Each host verifies the release checksum, "
        "version and exact source commit, keeps its identity, keys and mirrored "
        "data, and restarts its node."));
    setOcticon(m_hostInstallAllButton, "download", 14);
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
        "install the published ForkMesh v" FORKMESH_VERSION
        " binary and re-link it to your account."));
    setOcticon(m_hostReinstallAllButton, "sync", 14);
    connect(m_hostReinstallAllButton, &QPushButton::clicked, this,
            &MainWindow::runHostReinstallAllFromBinary);
    titleRow->addWidget(m_hostReinstallAllButton);
    // One-click "update from source" (adhoc): for every saved host, pull the
    // latest source, rebuild the client and restart its daemon — so the fleet
    // can be updated straight from source without cutting a release each time.
    m_hostUpdateAllSourceButton =
        new QPushButton(QStringLiteral("Update from source (all hosts)"));
    m_hostUpdateAllSourceButton->setCursor(Qt::PointingHandCursor);
    m_hostUpdateAllSourceButton->setToolTip(QStringLiteral(
        "For every saved host: SSH in, pull the latest ForkMesh source, rebuild "
        "the client from it and restart the node — without publishing a release. "
        "The node's identity key and mirrored data are kept."));
    setOcticon(m_hostUpdateAllSourceButton, "sync", 14);
    connect(m_hostUpdateAllSourceButton, &QPushButton::clicked, this,
            &MainWindow::runHostUpdateAllFromSource);
    titleRow->addWidget(m_hostUpdateAllSourceButton);
    outer->addLayout(titleRow);

    auto *subtitle = new QLabel(QString::fromUtf8(
        "Provision a remote machine onto the network. Enter its address and SSH "
        "login and give it a node name, then click Add host to save it. With the "
        "host saved, click Install ForkMesh and it will SSH in and run the hosted "
        "installer in a plain shell. ForkMesh remembers the first host key in "
        "its private trust store and rejects later mismatches. Your SSH agent, "
        "default keys and ~/.ssh/config are used when the optional password is "
        "blank; entered passwords remain in memory only. When installation "
        "finishes the new node joins the network and shows up in each "
        "repository's Mirror nodes list. Install "
        "(binary) on one saved host uploads this app's own binary. Install from "
        "binary (all hosts) instead makes every host download and checksum-verify "
        "the current published release, then confirms the installed version and "
        "exact source commit. No server yet? Create a Vultr mirror below "
        "provisions a brand-new VPS from just an API key."));
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
    bodyCol->setSpacing(10);

    // --- Install form ------------------------------------------------------
    auto *formCard = new QFrame;
    formCard->setObjectName("leaderboardCard");
    formCard->setFrameShape(QFrame::StyledPanel);
    auto *formCol = new QVBoxLayout(formCard);
    formCol->setContentsMargins(12, 10, 12, 10);
    formCol->setSpacing(6);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(6);

    m_hostIpEdit = new QLineEdit;
    m_hostIpEdit->setPlaceholderText(QStringLiteral("203.0.113.10"));
    form->addRow(QStringLiteral("Host IP / address"), m_hostIpEdit);

    m_hostUserEdit = new QLineEdit;
    m_hostUserEdit->setPlaceholderText(QStringLiteral("root"));
    form->addRow(QStringLiteral("SSH username"), m_hostUserEdit);

    m_hostPassEdit = new QLineEdit;
    m_hostPassEdit->setEchoMode(QLineEdit::Password);
    m_hostPassEdit->setPlaceholderText(
        QStringLiteral("Optional — SSH agent/default key is preferred"));
    m_hostPassEdit->setToolTip(QStringLiteral(
        "Leave blank to use your SSH agent, default key, or ~/.ssh/config. "
        "A password entered here is kept only until ForkMesh exits and is "
        "never written to settings."));
    form->addRow(QStringLiteral("SSH password (optional)"), m_hostPassEdit);

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

    // --- Create a Vultr mirror (adhoc #315) --------------------------------
    // Fully automated alternative to the manual form above: given only a Vultr
    // API key, deploy a brand-new VPS (cheapest supported plan, newest Debian), with the
    // SSH key created and managed by ForkMesh, then run the same hosted
    // installer over SSH so the node auto-links to this account and starts
    // mirroring/syncing on its own.
    auto *vultrCard = new QFrame;
    vultrCard->setObjectName("leaderboardCard");
    vultrCard->setFrameShape(QFrame::StyledPanel);
    auto *vultrCol = new QVBoxLayout(vultrCard);
    vultrCol->setContentsMargins(12, 10, 12, 10);
    vultrCol->setSpacing(6);

    auto *vultrTitle = new QLabel(QStringLiteral("Create a Vultr mirror"));
    QFont vtf = vultrTitle->font();
    vtf.setBold(true);
    vultrTitle->setFont(vtf);
    vultrCol->addWidget(vultrTitle);

    auto *vultrHint = new QLabel(QString::fromUtf8(
        "One click deploys a brand-new cloud mirror on your Vultr account: "
        "ForkMesh picks the cheapest available US IPv4 plan with at least 1 GB "
        "RAM, preferring New Jersey/New York metro and then Atlanta (smaller "
        "plans cannot hold the encrypted mirror's temporary "
        "working set; Vultr's IPv6-only tiers are also unreachable for the "
        "mesh) running the latest Debian, "
        "creates and manages the SSH key for it automatically, boots the "
        "instance, installs ForkMesh over SSH and links the new node to your "
        "account so it starts mirroring and syncing right away. The API key "
        "(Vultr panel \xE2\x86\x92 Account \xE2\x86\x92 API) is saved once "
        "Vultr accepts it \xE2\x80\x94 into this device's VULTR_API_KEY "
        "variable (Settings \xE2\x86\x92 Variables / Secrets) and into "
        "cloudflare_worker/.env.production \xE2\x80\x94 so you never have to "
        "enter it again; it travels only in the Authorization header of this "
        "app's HTTPS calls to Vultr, never in a command line. When a "
        "CLOUDFLARE_API_TOKEN device variable and a Cloudflare zone are "
        "configured, the new node also gets a <node>.<zone> DNS record so it "
        "joins the mesh under a stable name like your other mirrors. The "
        "instance is billed by Vultr to your account until you destroy it "
        "there."));
    vultrHint->setObjectName("mutedLabel");
    vultrHint->setWordWrap(true);
    vultrCol->addWidget(vultrHint);

    auto *vultrForm = new QFormLayout;
    vultrForm->setLabelAlignment(Qt::AlignRight);
    vultrForm->setSpacing(6);
    m_vultrApiKeyEdit = new QLineEdit;
    m_vultrApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_vultrApiKeyEdit->setPlaceholderText(
        QStringLiteral("Vultr API key — saved after the first successful run"));
    // Prefill from whatever this device already stores (this page's own saves,
    // Settings > Quick setup, or a hand-added variable), so a returning
    // operator only has to press the button (adhoc #127).
    m_vultrApiKeyEdit->setText(
        forkmesh::control::vultrApiKeyFromVariables(ActionStore::variables()));
    vultrForm->addRow(QStringLiteral("Vultr API key"), m_vultrApiKeyEdit);
    m_vultrNameEdit = new QLineEdit;
    m_vultrNameEdit->setPlaceholderText(QString::fromUtf8(
        "Optional \xE2\x80\x94 defaults to the next free mirrorN "
        "(mirror5, mirror6, \xE2\x80\xA6)"));
    vultrForm->addRow(QStringLiteral("Node name"), m_vultrNameEdit);
    vultrCol->addLayout(vultrForm);

    // Agent CLIs on the new mirror (adhoc #418). A headless VPS has no browser
    // to sign either provider in with, so the installed binaries would sit
    // there unusable; copying this device's own logins is what makes the fresh
    // node able to run agent sessions on our access from the first minute.
    m_vultrAgentClisCheck = new QCheckBox(QString::fromUtf8(
        "Also install Claude Code + Codex and sign them in with this device's "
        "access"));
    m_vultrAgentClisCheck->setObjectName(
        QStringLiteral("vultrInstallAgentClisCheck"));
    m_vultrAgentClisCheck->setChecked(true);
    m_vultrAgentClisCheck->setToolTip(QString::fromUtf8(
        "After ForkMesh is installed, the official Claude Code and Codex CLIs "
        "are installed on the new mirror and this device's own logins "
        "(~/.claude/.credentials.json, ~/.codex/auth.json, and the agent API "
        "keys from Settings for a provider you have no CLI login for) are "
        "copied to it, so it can run agent sessions immediately. The "
        "credentials travel only on the SSH session's stdin \xE2\x80\x94 never "
        "in a command line or in the log below."));
    vultrCol->addWidget(m_vultrAgentClisCheck);

    auto *vultrRow = new QHBoxLayout;
    vultrRow->setContentsMargins(0, 0, 0, 0);
    m_vultrCreateButton = new QPushButton(QStringLiteral("Create Vultr mirror"));
    m_vultrCreateButton->setObjectName("primaryButton");
    m_vultrCreateButton->setCursor(Qt::PointingHandCursor);
    m_vultrCreateButton->setToolTip(QStringLiteral(
        "Deploy the cheapest Debian instance on your Vultr account, install "
        "ForkMesh v" FORKMESH_VERSION " on it and link it to your account. "
        "Progress streams into Live output below."));
    setOcticon(m_vultrCreateButton, "rocket", 14);
    connect(m_vultrCreateButton, &QPushButton::clicked, this,
            &MainWindow::createVultrMirrorFromForm);
    vultrRow->addWidget(m_vultrCreateButton);
    m_vultrStatus = new QLabel;
    m_vultrStatus->setObjectName("mutedLabel");
    m_vultrStatus->setWordWrap(true);
    vultrRow->addWidget(m_vultrStatus, 1);
    vultrCol->addLayout(vultrRow);
    bodyCol->addWidget(vultrCard);

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
    m_hostInstallLog->setMinimumHeight(170);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_hostInstallLog->setFont(mono);
    m_hostInstallLog->setPlaceholderText(QString::fromUtf8(
        "The SSH session and installer output will stream here\xE2\x80\xA6"));
    bodyCol->addWidget(m_hostInstallLog);

    // --- Parallel fleet deploy: split live output --------------------------
    // When an "all hosts" action runs, every saved host deploys at once and
    // streams into its own pane here (arranged in a roughly square grid), so the
    // whole fleet's live output is visible side by side instead of one host at a
    // time in the single log above. Hidden until such a run starts.
    m_hostDeployLabel = new QLabel(QStringLiteral("Live output — per host"));
    QFont dlf = m_hostDeployLabel->font();
    dlf.setBold(true);
    m_hostDeployLabel->setFont(dlf);
    m_hostDeployLabel->setVisible(false);
    bodyCol->addWidget(m_hostDeployLabel);

    m_hostDeployPanel = new QWidget;
    m_hostDeployGrid = new QGridLayout(m_hostDeployPanel);
    m_hostDeployGrid->setContentsMargins(0, 0, 0, 0);
    m_hostDeployGrid->setSpacing(10);
    m_hostDeployPanel->setVisible(false);
    bodyCol->addWidget(m_hostDeployPanel);

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
        "that host. Click Actions to enable its executor and optionally replace "
        "its device-local variables through a one-shot SSH stdin request; secret "
        "values are never saved by this controller. Click Logs to open a live "
        "SSH tail for that host, or Size map to browse what is filling that "
        "host's disk. Click Remove to drop a host from this list "
        "without touching it \xE2\x80\x94 no SSH session is opened. "
        "Double-click a host instead to reload it into the form "
        "above for editing."));
    hostsHint->setObjectName("mutedLabel");
    hostsHint->setWordWrap(true);
    bodyCol->addWidget(hostsHint);

    m_hostsTable = new QTableWidget(0, 7);
    installColumnHeaderMenu(m_hostsTable); // 3-dots per-column menu (issue #318)
    m_hostsTable->setObjectName("issueTable");
    m_hostsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Node name"), QStringLiteral("Address"),
         QStringLiteral("User"), QStringLiteral("Status"),
         QStringLiteral("Claude"), QStringLiteral("Codex"), QString()});
    m_hostsTable->verticalHeader()->setVisible(false);
    m_hostsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hostsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hostsTable->setShowGrid(false);
    // Stretch the Status column and keep the two agent capability columns and
    // trailing action column compact.
    m_hostsTable->horizontalHeader()->setStretchLastSection(false);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_hostsTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_hostsTable); // spreadsheet-style draggable columns (#263)
    // Double-clicking a saved host reloads its server info into the install
    // form so the installer can be re-run. A password is available only if it
    // was entered or migrated during this process; otherwise key auth is used.
    connect(m_hostsTable, &QTableWidget::cellDoubleClicked, this,
            &MainWindow::loadHostIntoForm);
    bodyCol->addWidget(m_hostsTable);
    if (!m_hostProbeTimer) {
        m_hostProbeTimer = new QTimer(this);
        m_hostProbeTimer->setInterval(30000);
        connect(m_hostProbeTimer, &QTimer::timeout, this,
                &MainWindow::probeSavedHosts);
        m_hostProbeTimer->start();
    }

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    refreshHostsTable();
    QTimer::singleShot(0, this, &MainWindow::probeSavedHosts);
    return page;
}

void MainWindow::refreshHostsTable()
{
    if (!m_hostsTable)
        return;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    m_hostsTable->setRowCount(hosts.size());
    for (int i = 0; i < hosts.size(); ++i) {
        const QJsonObject h = hosts.at(i).toObject();
        const QString status = h.value("status").toString(QStringLiteral("installed"));
        const QString name = h.value("name").toString();
        const QString ip = h.value("ip").toString();
        const QString user = h.value("user").toString();
        const QString key =
            forkmesh::control::savedHostCredentialKey(name, ip, user);
        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, key);
        m_hostsTable->setItem(i, 0, nameItem);
        m_hostsTable->setItem(i, 1,
            new QTableWidgetItem(ip));
        m_hostsTable->setItem(i, 2,
            new QTableWidgetItem(user));
        const QString reachability = m_hostReachability.value(key);
        const QString provider = h.value(QStringLiteral("provider")).toString();
        const QString planType = h.value(QStringLiteral("planType")).toString();
        const QString region = h.value(QStringLiteral("region")).toString();
        const double monthlyCost =
            h.value(QStringLiteral("monthlyCost")).toDouble(-1.0);
        QStringList hostFacts;
        if (!provider.isEmpty())
            hostFacts.append(provider);
        if (!planType.isEmpty())
            hostFacts.append(planType);
        if (!region.isEmpty())
            hostFacts.append(region);
        if (monthlyCost >= 0.0)
            hostFacts.append(QStringLiteral("$%1/mo").arg(
                QString::number(monthlyCost, 'f',
                                monthlyCost == qFloor(monthlyCost) ? 0 : 2)));
        const QString displayedStatus =
            reachability.isEmpty() ? status : reachability;
        m_hostsTable->setItem(
            i, 3,
            new QTableWidgetItem(
                hostFacts.isEmpty()
                    ? displayedStatus
                    : displayedStatus + QStringLiteral("\n") +
                          hostFacts.join(QStringLiteral(" · "))));
        m_hostsTable->setItem(
            i, 4,
            new QTableWidgetItem(
                m_hostClaudeAvailability.value(
                    key, QString::fromUtf8("Checking\xE2\x80\xA6"))));
        m_hostsTable->setItem(
            i, 5,
            new QTableWidgetItem(
                m_hostCodexAvailability.value(
                    key, QString::fromUtf8("Checking\xE2\x80\xA6"))));

        // Per-row Update button: reload the saved host into the install form and
        // re-run the hosted installer against it. The installer is idempotent, so
        // re-running it pulls the latest ForkMesh release onto that host.
        auto *cell = new QWidget;
        auto *cellRow = new QHBoxLayout(cell);
        cellRow->setContentsMargins(4, 2, 4, 2);
        cellRow->setSpacing(6);
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

        // Per-row Destroy button: delete the server itself on Vultr. Uninstall
        // above only wipes ForkMesh (the VPS keeps running and billing), so a
        // mirror that is no longer wanted still has to be torn down by hand in
        // the Vultr panel — this does it from here and then forgets the host.
        auto *destroyBtn = new QPushButton(QStringLiteral("Destroy"));
        destroyBtn->setObjectName(QStringLiteral("hostDestroyButton"));
        destroyBtn->setCursor(Qt::PointingHandCursor);
        destroyBtn->setToolTip(QStringLiteral(
            "Destroy this server on Vultr: the instance is deleted, billing "
            "stops and everything on it is gone permanently. Needs the Vultr "
            "API key from the field above (or a stored VULTR_API_KEY "
            "variable)."));
        setOcticon(destroyBtn, "alert", 12);
        connect(destroyBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { destroyVultrHostAtRow(i); });
        });
        cellRow->addWidget(destroyBtn);

        // Per-row Remove button: drop this host from the saved list only. Unlike
        // Uninstall, this opens no SSH session and changes nothing on the remote
        // host \xe2\x80\x94 it just stops the app tracking it here (e.g. to clean
        // up a host that was already reformatted/decommissioned elsewhere).
        auto *removeBtn = new QPushButton(QStringLiteral("Remove"));
        removeBtn->setCursor(Qt::PointingHandCursor);
        setOcticon(removeBtn, "x", 12);
        connect(removeBtn, &QPushButton::clicked, this, [this, i] {
            const QString name =
                m_hostsTable->item(i, 0) ? m_hostsTable->item(i, 0)->text() : QString();
            const auto reply = QMessageBox::question(
                this, QStringLiteral("Remove saved host"),
                QString::fromUtf8(
                    "Remove \"%1\" from this list? This only forgets it here "
                    "\xE2\x80\x94 ForkMesh is NOT uninstalled from that host.")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply != QMessageBox::Yes)
                return;
            QTimer::singleShot(0, this, [this, i] { forgetHostAtRow(i); });
        });
        cellRow->addWidget(removeBtn);

        auto *viewLogsBtn = new QPushButton(QStringLiteral("Logs"));
        viewLogsBtn->setCursor(Qt::PointingHandCursor);
        setOcticon(viewLogsBtn, "terminal", 12);
        connect(viewLogsBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                viewHostLogsForSelection(i);
            });
        });
        cellRow->addWidget(viewLogsBtn);

        // Per-row Size map button: browse that host's disk usage over the same
        // authenticated SSH channel, one read-only `du` level at a time, so a
        // host that is filling up can be diagnosed from here.
        auto *diskBtn = new QPushButton(QStringLiteral("Size map"));
        diskBtn->setObjectName(QStringLiteral("hostDiskUsageButton"));
        diskBtn->setCursor(Qt::PointingHandCursor);
        diskBtn->setToolTip(QStringLiteral(
            "Browse what is using disk space on this host over SSH. Read-only: "
            "it only runs du, one directory level at a time."));
        setOcticon(diskBtn, "pie-chart", 12);
        connect(diskBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                browseHostDiskUsageForSelection(i);
            });
        });
        cellRow->addWidget(diskBtn);

        auto *actionsBtn = new QPushButton(QStringLiteral("Actions"));
        actionsBtn->setObjectName(QStringLiteral("hostActionsButton"));
        actionsBtn->setCursor(Qt::PointingHandCursor);
        actionsBtn->setToolTip(QStringLiteral(
            "Configure this mirror's Actions state and device-local variables "
            "over authenticated SSH. Values travel only on stdin."));
        setOcticon(actionsBtn, "workflow", 12);
        connect(actionsBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                configureHostActionsForSelection(i);
            });
        });
        cellRow->addWidget(actionsBtn);

        auto *installAgentsBtn =
            new QPushButton(QStringLiteral("Install Claude + Codex"));
        installAgentsBtn->setObjectName(
            QStringLiteral("hostInstallAgentClisButton"));
        installAgentsBtn->setCursor(Qt::PointingHandCursor);
        installAgentsBtn->setToolTip(QStringLiteral(
            "Install the official user-scoped Claude Code and Codex CLI "
            "binaries on this mirror over its pinned SSH connection. This "
            "does not copy tokens or sign either provider in."));
        setOcticon(installAgentsBtn, "terminal", 12);
        connect(installAgentsBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                installAgentClisForHost(i);
            });
        });
        cellRow->addWidget(installAgentsBtn);

        // The installer copies no provider tokens, so signing in is a separate
        // interactive step. This opens the same live shell the install opens on
        // its own, for a host whose CLIs are already there.
        auto *signInBtn = new QPushButton(QStringLiteral("Sign in"));
        signInBtn->setObjectName(QStringLiteral("hostAgentLoginButton"));
        signInBtn->setCursor(Qt::PointingHandCursor);
        signInBtn->setToolTip(QStringLiteral(
            "Open a live terminal on this mirror to finish the Claude Code and "
            "Codex sign-ins. What you type goes only to the host over its "
            "pinned SSH connection."));
        setOcticon(signInBtn, "key", 12);
        connect(signInBtn, &QPushButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] {
                openHostAgentLoginTerminalForSelection(i);
            });
        });
        cellRow->addWidget(signInBtn);
        // Table-row sizing: the default QPushButton padding makes each of these
        // 35px tall, far more than a text row, so the view squashed the whole
        // action cell down to the item height and Qt silently dropped every
        // label — the row read as six anonymous icon pills (adhoc #376). The
        // "sm" size keeps them inside a table row so the words stay visible.
        for (QPushButton *b : cell->findChildren<QPushButton *>())
            b->setProperty("buttonSize", "sm");
        m_hostsTable->setCellWidget(i, 6, cell);
    }
    // ...and the rows still have to be tall enough for the buttons, and the
    // action column wide enough that no label is elided. The view lays a cell
    // widget out inside the item rect minus #issueTable::item's 6px/8px
    // padding, so both need that much more than the cell's own hint.
    if (!hosts.isEmpty()) {
        // Deferred: makeColumnsResizable() fits the columns on the first
        // rowsInserted from its own singleShot(0), and would otherwise land
        // after this and undo it.
        QTimer::singleShot(0, m_hostsTable, [this] {
            QWidget *cell = m_hostsTable ? m_hostsTable->cellWidget(0, 6) : nullptr;
            if (!cell)
                return;
            // setColumnWidth() only takes on an Interactive section; that is
            // also the mode makeColumnsResizable() leaves behind, so this just
            // gets there whether or not it has run yet.
            m_hostsTable->horizontalHeader()->setSectionResizeMode(
                6, QHeaderView::Interactive);
            m_hostsTable->setColumnWidth(6, cell->sizeHint().width() + 16);
            const int rowHeight = cell->sizeHint().height() + 12;
            for (int r = 0; r < m_hostsTable->rowCount(); ++r)
                m_hostsTable->setRowHeight(r, rowHeight);
        });
    }

    updateNetworkCounts(-1, -1, hosts.size());
}

forkmesh::control::AgentCliCredentials MainWindow::localAgentCliCredentials()
{
    forkmesh::control::AgentCliCredentials credentials;
    const auto readFile = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    credentials.claudeCredentials = readFile(
        QDir::homePath() + QStringLiteral("/.claude/.credentials.json"));
    credentials.codexAuth =
        readFile(QDir::homePath() + QStringLiteral("/.codex/auth.json"));
    // Only fall back to an API key for the provider whose CLI login we could
    // not copy: Claude Code warns that auth "may not work as expected" when an
    // ANTHROPIC_API_KEY sits next to a logged-in session, and Codex would
    // bypass the ChatGPT plan the same way (see agentRunConfig).
    QSettings settings;
    if (credentials.claudeCredentials.trimmed().isEmpty()) {
        const QString key =
            settings.value(kClaudeApiKeySetting).toString().trimmed();
        if (!key.isEmpty())
            credentials.env.insert(QStringLiteral("ANTHROPIC_API_KEY"), key);
    }
    if (credentials.codexAuth.trimmed().isEmpty()) {
        const QString key =
            settings.value(kCodexApiKeySetting).toString().trimmed();
        if (!key.isEmpty())
            credentials.env.insert(QStringLiteral("OPENAI_API_KEY"), key);
    }
    return credentials;
}

void MainWindow::probeSavedHosts()
{
    if (!m_hostsTable)
        return;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    for (const QJsonValue &value : hosts) {
        const QJsonObject host = value.toObject();
        probeSavedHost(
            host.value(QStringLiteral("name")).toString().trimmed(),
            host.value(QStringLiteral("ip")).toString().trimmed(),
            host.value(QStringLiteral("user")).toString().trimmed(),
            host.value(QStringLiteral("status")).toString().trimmed());
    }
}

void MainWindow::probeSavedHost(const QString &name, const QString &ip,
                                const QString &user,
                                const QString &savedStatus)
{
    if (!m_hostsTable || name.isEmpty() || ip.isEmpty() || user.isEmpty())
        return;
    const QString key =
        forkmesh::control::savedHostCredentialKey(name, ip, user);
    if (m_hostProbesInFlight.contains(key))
        return;
    m_hostProbesInFlight.insert(key);

    const auto rowForKey = [this](const QString &candidate) -> int {
        if (!m_hostsTable)
            return -1;
        for (int row = 0; row < m_hostsTable->rowCount(); ++row) {
            const QTableWidgetItem *item = m_hostsTable->item(row, 0);
            if (item && item->data(Qt::UserRole).toString() == candidate)
                return row;
        }
        return -1;
    };
    const auto render = [this, rowForKey, key](
                            const QString &status,
                            const QString &claude,
                            const QString &codex,
                            const QColor &color) {
        m_hostReachability.insert(key, status);
        m_hostClaudeAvailability.insert(key, claude);
        m_hostCodexAvailability.insert(key, codex);
        const int row = rowForKey(key);
        if (row < 0 || !m_hostsTable)
            return;
        for (const auto &entry : {
                 qMakePair(3, status),
                 qMakePair(4, claude),
                 qMakePair(5, codex),
             }) {
            if (QTableWidgetItem *item =
                    m_hostsTable->item(row, entry.first)) {
                item->setText(entry.second);
                item->setForeground(color);
            }
        }
    };
    render(QString::fromUtf8("\xE2\x97\x8C Checking\xE2\x80\xA6"),
           QString::fromUtf8("Checking\xE2\x80\xA6"),
           QString::fromUtf8("Checking\xE2\x80\xA6"),
           QColor(QStringLiteral("#8b949e")));

    QString password;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    for (const QJsonValue &value : hosts) {
        const QJsonObject host = value.toObject();
        if (host.value(QStringLiteral("name")).toString().trimmed() == name &&
            host.value(QStringLiteral("ip")).toString().trimmed() == ip &&
            host.value(QStringLiteral("user")).toString().trimmed() == user) {
            password = m_hostSessionPasswords.value(key);
            break;
        }
    }
    const QString identityFile = savedHostIdentityFile(name, ip, user);
    if (!identityFile.isEmpty())
        password.clear();
    const QString remoteCommand = QStringLiteral(
        "sh -lc 'probe_home=\"$HOME\"; "
        "if test \"$(id -u)\" = 0 && id forkmesh-node >/dev/null 2>&1; then "
        "probe_home=$(getent passwd forkmesh-node | cut -d: -f6); fi; "
        "export PATH=\"$probe_home/.local/bin:$probe_home/.claude/bin:$PATH\"; "
        "printf \"FORKMESH=%s CLAUDE=%s CODEX=%s\\\\n\" "
        "\"$(command -v forkmesh >/dev/null 2>&1 && echo 1 || echo 0)\" "
        "\"$(command -v claude >/dev/null 2>&1 && echo 1 || echo 0)\" "
        "\"$(command -v codex >/dev/null 2>&1 && echo 1 || echo 0)\"'");
    QString error;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            ip, user, password, remoteCommand, &error, identityFile);
    if (ssh.program.isEmpty()) {
        m_hostProbesInFlight.remove(key);
        render(
            QString::fromUtf8("\xE2\x97\x8F Attention"),
            QString::fromUtf8("\xE2\x80\x94"),
            QString::fromUtf8("\xE2\x80\x94"),
            QColor(QStringLiteral("#d29922")));
        const int row = rowForKey(key);
        if (row >= 0) {
            if (QTableWidgetItem *item = m_hostsTable->item(row, 3))
                item->setToolTip(error);
        }
        return;
    }

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProcessEnvironment(ssh.environment);
    connect(process, &QProcess::readyReadStandardOutput, this,
            [process] {
        QByteArray output = process->property("forkmeshHostProbe").toByteArray();
        output += process->readAllStandardOutput();
        if (output.size() > 4096)
            output = output.right(4096);
        process->setProperty("forkmeshHostProbe", output);
    });
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    deadline->setInterval(35000);
    connect(deadline, &QTimer::timeout, process, [process] {
        process->setProperty("forkmeshHostProbeTimedOut", true);
        process->kill();
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, deadline, key, render](
                QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart ||
            process->property("forkmeshHostProbeDone").toBool()) {
            return;
        }
        process->setProperty("forkmeshHostProbeDone", true);
        deadline->stop();
        m_hostProbesInFlight.remove(key);
        render(
            QString::fromUtf8("\xE2\x97\x8F Attention \xC2\xB7 SSH unavailable"),
            QString::fromUtf8("\xE2\x80\x94"),
            QString::fromUtf8("\xE2\x80\x94"),
            QColor(QStringLiteral("#cf222e")));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this,
            [this, process, deadline, key, savedStatus, render, rowForKey](
                int exitCode, QProcess::ExitStatus exitStatus) {
        if (process->property("forkmeshHostProbeDone").toBool())
            return;
        process->setProperty("forkmeshHostProbeDone", true);
        deadline->stop();
        QByteArray bytes =
            process->property("forkmeshHostProbe").toByteArray();
        bytes += process->readAllStandardOutput();
        const QString output = QString::fromUtf8(bytes);
        const bool online =
            exitStatus == QProcess::NormalExit &&
            exitCode == 0 &&
            output.contains(QStringLiteral("FORKMESH="));
        if (online) {
            const bool forkmesh =
                output.contains(QStringLiteral("FORKMESH=1"));
            const bool claude =
                output.contains(QStringLiteral("CLAUDE=1"));
            const bool codex =
                output.contains(QStringLiteral("CODEX=1"));
            render(
                forkmesh
                    ? QString::fromUtf8("\xE2\x97\x8F Online")
                    : QString::fromUtf8("\xE2\x97\x8F Online \xC2\xB7 ForkMesh missing"),
                claude
                    ? QString::fromUtf8("\xE2\x9C\x93 Installed")
                    : QStringLiteral("Not installed"),
                codex
                    ? QString::fromUtf8("\xE2\x9C\x93 Installed")
                    : QStringLiteral("Not installed"),
                forkmesh
                    ? QColor(QStringLiteral("#2da44e"))
                    : QColor(QStringLiteral("#d29922")));
            const int row = rowForKey(key);
            if (row >= 0 && m_hostsTable) {
                if (QTableWidgetItem *item = m_hostsTable->item(row, 4)) {
                    item->setForeground(QColor(
                        claude ? QStringLiteral("#2da44e")
                               : QStringLiteral("#8b949e")));
                }
                if (QTableWidgetItem *item = m_hostsTable->item(row, 5)) {
                    item->setForeground(QColor(
                        codex ? QStringLiteral("#2da44e")
                              : QStringLiteral("#8b949e")));
                }
            }
        } else {
            const bool rejected =
                output.contains(QStringLiteral("Permission denied"),
                                Qt::CaseInsensitive);
            const bool timedOut =
                process->property("forkmeshHostProbeTimedOut").toBool() ||
                output.contains(QStringLiteral("Connection timed out"),
                                Qt::CaseInsensitive);
            const bool provisioning =
                savedStatus.contains(QStringLiteral("install"),
                                     Qt::CaseInsensitive) ||
                savedStatus.contains(QStringLiteral("vultr"),
                                     Qt::CaseInsensitive) ||
                savedStatus == QStringLiteral("added");
            const QString status =
                rejected
                    ? QString::fromUtf8("\xE2\x97\x8F Attention \xC2\xB7 SSH key rejected")
                    : timedOut && provisioning
                        ? QString::fromUtf8("\xE2\x97\x8C Provisioning \xC2\xB7 waiting for SSH")
                        : QString::fromUtf8("\xE2\x97\x8F Offline \xC2\xB7 unreachable");
            render(
                status,
                QString::fromUtf8("\xE2\x80\x94"),
                QString::fromUtf8("\xE2\x80\x94"),
                rejected
                    ? QColor(QStringLiteral("#cf222e"))
                    : QColor(QStringLiteral("#8b949e")));
            const int row = rowForKey(key);
            if (row >= 0) {
                if (QTableWidgetItem *item = m_hostsTable->item(row, 3))
                    item->setToolTip(output.trimmed().left(1000));
            }
        }
        m_hostProbesInFlight.remove(key);
        process->deleteLater();
    });
    process->start(ssh.program, ssh.arguments);
    deadline->start();
}

void MainWindow::installAgentClisForHost(int row)
{
    if (m_hostAgentInstallProcess &&
        m_hostAgentInstallProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A mirror agent-CLI install is already running."));
        return;
    }
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    const auto cellText = [this, row](int column) {
        const QTableWidgetItem *item = m_hostsTable->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    const QString node = cellText(0);
    const QString ip = cellText(1);
    const QString user = cellText(2);
    if (node.isEmpty() || ip.isEmpty() || user.isEmpty())
        return;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    QString pass;
    for (const QJsonValue &value : hosts) {
        const QJsonObject host = value.toObject();
        if (host.value(QStringLiteral("name")).toString() == node &&
            host.value(QStringLiteral("ip")).toString() == ip &&
            host.value(QStringLiteral("user")).toString() == user) {
            pass = m_hostSessionPasswords.value(
                forkmesh::control::savedHostCredentialKey(node, ip, user));
            break;
        }
    }
    const QString identityFile = savedHostIdentityFile(node, ip, user);
    // A Vultr mirror provisioned by ForkMesh has a pinned per-host identity.
    // Use only that identity even if this process still has an old session
    // password in memory; this avoids an opaque password fallback and makes
    // the authentication path match every later headless-agent connection.
    const QString sshPassword =
        identityFile.isEmpty() ? pass : QString();
    if (m_hostInstallLog)
        m_hostInstallLog->clear();
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(
            QStringLiteral("Installing agent CLIs on %1...").arg(node));
    // This button deliberately installs the binaries only; the Vultr flow's
    // opt-in is what copies logins to a node this device just created.
    runAgentCliInstall(node, ip, user, sshPassword, identityFile,
                       /*copyCredentials=*/false,
                       [this](bool, QString message) {
                           if (m_hostInstallStatus)
                               m_hostInstallStatus->setText(message);
                       });
}

void MainWindow::runAgentCliInstall(
    const QString &node, const QString &ip, const QString &user,
    const QString &sshPassword, const QString &identityFile,
    bool copyCredentials, std::function<void(bool, QString)> onFinished)
{
    const auto report = [onFinished](bool ok, const QString &message) {
        if (onFinished)
            onFinished(ok, message);
    };
    if (m_hostAgentInstallProcess &&
        m_hostAgentInstallProcess->state() != QProcess::NotRunning) {
        report(false,
               QStringLiteral("A mirror agent-CLI install is already running."));
        return;
    }
    // Secrets are collected here and live only in the payload byte array and
    // the child's stdin pipe; the remote command below is fixed and secret-free.
    QByteArray payload;
    QString credentialSummary;
    if (copyCredentials) {
        const forkmesh::control::AgentCliCredentials credentials =
            localAgentCliCredentials();
        if (forkmesh::control::agentCliCredentialsAreEmpty(credentials)) {
            report(false, QStringLiteral(
                "This device has no Claude Code or Codex login to copy to %1. "
                "Sign in here first, then use Install Claude + Codex on that "
                "host.").arg(node));
            return;
        }
        QString payloadError;
        payload = forkmesh::control::buildAgentCliBootstrapPayload(
            credentials, &payloadError);
        if (payload.isEmpty()) {
            report(false, payloadError);
            return;
        }
        credentialSummary =
            forkmesh::control::describeAgentCliCredentials(credentials);
    }
    const QString remoteCmd =
        forkmesh::control::agentCliBootstrapRemoteCommand(copyCredentials);
    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            ip, user, sshPassword, remoteCmd, &sshError, identityFile);
    if (ssh.program.isEmpty()) {
        payload.fill('\0');
        report(false, sshError);
        return;
    }
    appendHostInstallLog(
        QStringLiteral("Installing Claude Code and Codex on %1 (%2@%3)...\n")
            .arg(node, user, ip));
    appendHostInstallLog(
        identityFile.isEmpty()
            ? QStringLiteral(
                  "No managed key is saved for this host; using the current "
                  "session credential or the system SSH agent.\n")
            : QStringLiteral(
                  "Using the ForkMesh-managed SSH identity for this host.\n"));
    if (copyCredentials)
        appendHostInstallLog(
            QStringLiteral("Copying this device's agent access (%1) over the "
                           "SSH session's stdin.\n")
                .arg(credentialSummary));
    auto *proc = new QProcess(this);
    m_hostAgentInstallProcess = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProcessEnvironment(ssh.environment);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        const QByteArray chunk = proc->readAllStandardOutput();
        QByteArray transcript =
            proc->property("forkmeshAgentInstallOutput").toByteArray();
        transcript += chunk;
        if (transcript.size() > 8192)
            transcript = transcript.right(8192);
        proc->setProperty("forkmeshAgentInstallOutput", transcript);
        appendHostInstallLog(QString::fromUtf8(chunk));
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, report](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (m_hostAgentInstallProcess == proc)
            m_hostAgentInstallProcess = nullptr;
        report(false,
               QStringLiteral("Could not start the pinned SSH installer."));
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, node, ip, user, identityFile, copyCredentials,
             report](
                int code, QProcess::ExitStatus status) {
        if (m_hostAgentInstallProcess == proc)
            m_hostAgentInstallProcess = nullptr;
        const bool ok =
            code == 0 && status == QProcess::NormalExit;
        appendHostInstallLog(
            ok ? QStringLiteral("\nAgent CLI installation finished.\n")
               : QStringLiteral("\nAgent CLI installation failed (exit %1).\n")
                     .arg(code));
        const QString output = QString::fromUtf8(
            proc->property("forkmeshAgentInstallOutput").toByteArray());
        const bool timedOut = output.contains(
            QStringLiteral("Connection timed out"),
            Qt::CaseInsensitive);
        const bool keyRejected = output.contains(
            QStringLiteral("Permission denied"),
            Qt::CaseInsensitive);
        report(ok,
            ok
                ? (copyCredentials
                       ? QStringLiteral(
                             "Claude Code and Codex are installed on %1 and "
                             "signed in with this device's access.")
                             .arg(node)
                       : QStringLiteral(
                             "Claude Code and Codex are installed on %1. "
                             "Finish the provider sign-ins in the terminal "
                             "that just opened.")
                             .arg(node))
                : timedOut
                    ? QStringLiteral(
                          "SSH could not reach %1 on port 22. The saved "
                          "key was not reached; use the host's reachable "
                          "public/stable address or connect this device to "
                          "the private network, then retry.")
                          .arg(ip)
                    : keyRejected && !identityFile.isEmpty()
                        ? QStringLiteral(
                              "The mirror rejected its saved ForkMesh SSH "
                              "key. Re-provision or replace that host key, "
                              "then retry.")
                : QStringLiteral(
                      "Agent CLI installation failed on %1; see Live output.")
                      .arg(node));
        QTimer::singleShot(0, this, &MainWindow::probeSavedHosts);
        proc->deleteLater();
        // A run without copied credentials leaves the mirror with the binaries
        // and no provider session. Hand the operator the shell to enter them in
        // as soon as the binaries exist, instead of leaving "login is still
        // required" as a dead end. A copyCredentials run is already signed in,
        // so it gets no window.
        if (ok && !copyCredentials)
            openHostAgentLoginTerminal(node, ip, user);
    });
    proc->start(ssh.program, ssh.arguments);
    // The credentials leave this process only here, on the child's stdin, and
    // the buffer is wiped as soon as it is handed over.
    if (!payload.isEmpty()) {
        proc->write(payload);
        payload.fill('\0');
    }
    proc->closeWriteChannel();
}

void MainWindow::openHostAgentLoginTerminalForSelection(int row)
{
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    const auto cellText = [this, row](int column) {
        const QTableWidgetItem *item = m_hostsTable->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    openHostAgentLoginTerminal(cellText(0), cellText(1), cellText(2));
}

void MainWindow::openHostAgentLoginTerminal(const QString &node,
                                            const QString &ip,
                                            const QString &user)
{
    if (node.isEmpty() || ip.isEmpty() || user.isEmpty())
        return;
    // A headless node has no desktop to show a sign-in window on; it would
    // strand a live SSH child behind an invisible dialog.
    if (m_headless)
        return;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    QString pass;
    for (const QJsonValue &value : hosts) {
        const QJsonObject host = value.toObject();
        if (host.value(QStringLiteral("name")).toString() == node &&
            host.value(QStringLiteral("ip")).toString() == ip &&
            host.value(QStringLiteral("user")).toString() == user) {
            pass = m_hostSessionPasswords.value(
                forkmesh::control::savedHostCredentialKey(node, ip, user));
            break;
        }
    }
    const QString identityFile = savedHostIdentityFile(node, ip, user);
    // Same rule as the installer: a pinned managed key is the only credential
    // used when one exists, so the sign-in shell rides the exact transport
    // every later headless-agent connection does.
    const QString sshPassword = identityFile.isEmpty() ? pass : QString();
    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostInteractiveSshCommand(
            ip, user, sshPassword,
            forkmesh::control::buildHostAgentLoginRemoteCommand(), &sshError,
            identityFile);
    if (ssh.program.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(sshError);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("hostAgentLoginDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Sign in \xE2\x80\x94 %1").arg(node));
    dialog->resize(900, 560);
    auto *layout = new QVBoxLayout(dialog);
    auto *notice = new QLabel(QStringLiteral(
        "This is a live shell on <b>%1@%2</b>. Run <code>claude</code> and then "
        "<code>/login</code> inside it, and <code>codex login</code>, and paste "
        "each provider's code here. ForkMesh never reads or stores what you "
        "type in this window. Drag to select text and it's copied "
        "automatically (or use Ctrl+Shift+C / right-click).")
                                  .arg(user.toHtmlEscaped(), ip.toHtmlEscaped()),
                              dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *autoOpenStatus = new QLabel(dialog);
    autoOpenStatus->setObjectName(QStringLiteral("hostAgentLoginAutoOpenStatus"));
    autoOpenStatus->setWordWrap(true);
    autoOpenStatus->hide();
    layout->addWidget(autoOpenStatus);
    auto *terminal = new TerminalWidget(dialog);
    terminal->setObjectName(QStringLiteral("hostAgentLoginTerminal"));
    layout->addWidget(terminal, 1);
    // The CLI prints its own "paste this URL" fallback for when it can't open
    // a browser itself; that's exactly the case here, on a headless remote
    // shell, so open it for the operator instead of leaving them to select
    // and copy the wrapped, multi-line URL by hand.
    connect(terminal, &TerminalWidget::signInUrlDetected, autoOpenStatus,
            [autoOpenStatus](const QUrl &) {
                autoOpenStatus->setText(QStringLiteral(
                    "Opened the Claude sign-in page in your browser."));
                autoOpenStatus->show();
            });
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Close"), dialog);
    close->setObjectName(QStringLiteral("hostAgentLoginCloseButton"));
    connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    // sshpass reads the session password from SSHPASS only; it never becomes a
    // word of the command line the PTY shell sees.
    QStringList extraEnv;
    if (!sshPassword.isEmpty())
        extraEnv << QStringLiteral("SSHPASS=") + sshPassword;
    dialog->show();
    dialog->raise();
    terminal->runCommand(forkmesh::control::hostSshCommandLine(ssh),
                         QDir::homePath(), extraEnv);
    terminal->setFocus();
}

void MainWindow::configureHostActionsForSelection(int row)
{
    if (m_hostActionsProcess &&
        m_hostActionsProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus) {
            m_hostInstallStatus->setText(
                QStringLiteral("A mirror Actions configuration is already running."));
        }
        return;
    }
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;

    const auto cellText = [this, row](int column) {
        const QTableWidgetItem *item = m_hostsTable->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    const QString node = cellText(0);
    const QString host = cellText(1);
    const QString user = cellText(2);
    QString password = m_hostSessionPasswords.value(
        forkmesh::control::savedHostCredentialKey(node, host, user));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Mirror Actions — %1").arg(node));
    dialog.setMinimumSize(640, 470);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(QStringLiteral(
        "SSH access to <b>%1@%2</b> authorizes this one change. ForkMesh sends "
        "a bounded configuration document to the node helper on stdin. Variable "
        "values never enter command arguments, logs, the public mirror catalog, "
        "or this desktop's saved settings.")
                                  .arg(user.toHtmlEscaped(),
                                       host.toHtmlEscaped()));
    notice->setWordWrap(true);
    layout->addWidget(notice);

    auto *enabled =
        new QCheckBox(QStringLiteral("Enable Actions on this mirror node"));
    enabled->setObjectName(QStringLiteral("hostActionsEnabledCheck"));
    enabled->setChecked(false);
    layout->addWidget(enabled);
    auto *replace = new QCheckBox(
        QStringLiteral("Replace the node's Actions variables with this list"));
    replace->setObjectName(QStringLiteral("hostActionsReplaceVariablesCheck"));
    replace->setChecked(false);
    layout->addWidget(replace);

    auto *variables = new QTableWidget(0, 2);
    variables->setObjectName(QStringLiteral("hostActionsVariablesTable"));
    variables->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Value")});
    variables->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    variables->horizontalHeader()->setSectionResizeMode(1,
                                                         QHeaderView::Stretch);
    variables->verticalHeader()->setVisible(false);
    variables->setSelectionBehavior(QAbstractItemView::SelectRows);
    variables->setSelectionMode(QAbstractItemView::SingleSelection);
    variables->setEditTriggers(QAbstractItemView::NoEditTriggers);
    variables->setEnabled(false);
    layout->addWidget(variables, 1);

    auto *variableButtons = new QHBoxLayout;
    auto *addVariable = new QPushButton(QStringLiteral("Add variable"));
    addVariable->setObjectName(QStringLiteral("hostActionsAddVariableButton"));
    auto *removeVariable =
        new QPushButton(QStringLiteral("Remove selected"));
    removeVariable->setObjectName(
        QStringLiteral("hostActionsRemoveVariableButton"));
    addVariable->setEnabled(false);
    removeVariable->setEnabled(false);
    variableButtons->addWidget(addVariable);
    variableButtons->addWidget(removeVariable);
    variableButtons->addStretch();
    layout->addLayout(variableButtons);
    connect(replace, &QCheckBox::toggled, variables,
            &QWidget::setEnabled);
    connect(replace, &QCheckBox::toggled, addVariable,
            &QWidget::setEnabled);
    connect(replace, &QCheckBox::toggled, removeVariable,
            &QWidget::setEnabled);

    connect(addVariable, &QPushButton::clicked, &dialog,
            [this, variables] {
                bool ok = false;
                const QString name = QInputDialog::getText(
                    this, QStringLiteral("Actions variable"),
                    QStringLiteral("Variable name:"), QLineEdit::Normal,
                    QString(), &ok).trimmed();
                if (!ok || name.isEmpty())
                    return;
                const QString value = QInputDialog::getText(
                    this, QStringLiteral("Actions variable"),
                    QStringLiteral("Secret value:"), QLineEdit::Password,
                    QString(), &ok);
                if (!ok)
                    return;
                int rowForName = -1;
                for (int row = 0; row < variables->rowCount(); ++row) {
                    const QTableWidgetItem *item = variables->item(row, 0);
                    if (item && item->text() == name) {
                        rowForName = row;
                        break;
                    }
                }
                if (rowForName < 0) {
                    rowForName = variables->rowCount();
                    variables->insertRow(rowForName);
                    variables->setItem(rowForName, 0,
                                       new QTableWidgetItem(name));
                    variables->setItem(rowForName, 1,
                                       new QTableWidgetItem);
                }
                QTableWidgetItem *valueItem =
                    variables->item(rowForName, 1);
                valueItem->setText(QStringLiteral("••••••••"));
                valueItem->setData(Qt::UserRole, value);
                valueItem->setToolTip(
                    QStringLiteral("Value is hidden and will be discarded after sending."));
            });
    connect(removeVariable, &QPushButton::clicked, &dialog,
            [variables] {
                const int row = variables->currentRow();
                if (row < 0)
                    return;
                if (QTableWidgetItem *item = variables->item(row, 1)) {
                    QString secret = item->data(Qt::UserRole).toString();
                    secret.fill(QChar::Null);
                    item->setData(Qt::UserRole, QString());
                }
                variables->removeRow(row);
            });

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *apply = buttons->addButton(
        QStringLiteral("Send to mirror"), QDialogButtonBox::AcceptRole);
    apply->setObjectName(QStringLiteral("hostActionsApplyButton"));
    apply->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog,
            &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog,
            &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        for (int variableRow = 0; variableRow < variables->rowCount();
             ++variableRow) {
            if (QTableWidgetItem *item =
                    variables->item(variableRow, 1)) {
                QString secret = item->data(Qt::UserRole).toString();
                secret.fill(QChar::Null);
                item->setData(Qt::UserRole, QString());
            }
        }
        return;
    }

    forkmesh::control::MirrorActionsConfigurationRequest request;
    request.requestId =
        QUuid::createUuid().toString(QUuid::Id128).toLower();
    request.host = host;
    request.sshUser = user;
    request.nodeName = node;
    request.actionsEnabled = enabled->isChecked();
    request.replaceVariables = replace->isChecked();
    if (request.replaceVariables) {
        for (int variableRow = 0; variableRow < variables->rowCount();
             ++variableRow) {
            const QTableWidgetItem *nameItem =
                variables->item(variableRow, 0);
            QTableWidgetItem *valueItem =
                variables->item(variableRow, 1);
            if (!nameItem || !valueItem)
                continue;
            request.variables.insert(
                nameItem->text(),
                valueItem->data(Qt::UserRole).toString());
            QString secret = valueItem->data(Qt::UserRole).toString();
            secret.fill(QChar::Null);
            valueItem->setData(Qt::UserRole, QString());
            valueItem->setText(QStringLiteral("discarded"));
        }
    }
    runHostActionsConfiguration(std::move(request), password);
    password.fill(QChar::Null);
}

void MainWindow::runHostActionsConfiguration(
    forkmesh::control::MirrorActionsConfigurationRequest request,
    const QString &sshPassword)
{
    QString error;
    forkmesh::control::MirrorActionsSshCommand command =
        forkmesh::control::buildMirrorActionsSshCommand(
            request, sshPassword, &error,
            savedHostIdentityFile(request.nodeName, request.host,
                                  request.sshUser));
    if (command.program.isEmpty()) {
        for (QString &value : request.variables)
            value.fill(QChar::Null);
        command.standardInput.fill('\0');
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(error);
        return;
    }

    const QString requestId = request.requestId;
    const QString node = request.nodeName.trimmed();
    const QString host = request.host.trimmed();
    const bool enabled = request.actionsEnabled;
    const bool replaceVariables = request.replaceVariables;
    const int variableCount = request.variables.size();
    for (QString &value : request.variables) {
        value.fill(QChar::Null);
    }
    request.variables.clear();

    auto rawOutput = QSharedPointer<QByteArray>::create();
    auto outputOverflow = QSharedPointer<bool>::create(false);
    auto completed = QSharedPointer<bool>::create(false);
    auto *process = new QProcess(this);
    m_hostActionsProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProcessEnvironment(command.environment);
    if (m_hostInstallStatus) {
        m_hostInstallStatus->setText(
            QStringLiteral(
                "Sending Actions state to %1 over authenticated SSH…")
                .arg(node));
    }
    appendHostInstallLog(
        QStringLiteral(
            "\n[Actions] Sending %1 state and %2 variable name(s) to %3@%4; "
            "values are stdin-only and redacted.\n")
            .arg(enabled ? QStringLiteral("enabled")
                         : QStringLiteral("disabled"))
            .arg(replaceVariables ? variableCount : 0)
            .arg(request.sshUser.trimmed(), host));

    connect(process, &QProcess::readyReadStandardOutput, this,
            [process, rawOutput, outputOverflow] {
                QByteArray chunk = process->readAllStandardOutput();
                constexpr qsizetype kMaximumOutput = 64 * 1024;
                if (rawOutput->size() + chunk.size() <= kMaximumOutput) {
                    rawOutput->append(chunk);
                } else {
                    *outputOverflow = true;
                    const qsizetype remaining =
                        qMax<qsizetype>(0, kMaximumOutput - rawOutput->size());
                    rawOutput->append(chunk.first(remaining));
                }
                // Never render remote output from this secret-bearing request.
                // A hostile/broken helper could echo stdin verbatim or encode
                // it, which cannot be made safe by ordinary string redaction.
                chunk.fill('\0');
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, rawOutput, completed](
                QProcess::ProcessError processError) {
                if (processError == QProcess::FailedToStart) {
                    if (*completed)
                        return;
                    *completed = true;
                    appendHostInstallLog(
                        QStringLiteral(
                            "[Actions] Could not start SSH. Install OpenSSH"
                            " (and sshpass for password login).\n"));
                    if (m_hostInstallStatus) {
                        m_hostInstallStatus->setText(
                            QStringLiteral(
                                "Mirror Actions configuration failed: "
                                "could not start SSH."));
                    }
                    rawOutput->fill('\0');
                    rawOutput->clear();
                    QProcessEnvironment scrubbed =
                        QProcessEnvironment::systemEnvironment();
                    scrubbed.remove(QStringLiteral("SSHPASS"));
                    process->setProcessEnvironment(scrubbed);
                    if (m_hostActionsProcess == process)
                        m_hostActionsProcess = nullptr;
                    process->deleteLater();
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, rawOutput, outputOverflow, completed,
             requestId, node, enabled, replaceVariables, variableCount](
                int exitCode, QProcess::ExitStatus exitStatus) {
                if (*completed)
                    return;
                *completed = true;
                QByteArray finalChunk =
                    process->readAllStandardOutput();
                constexpr qsizetype kMaximumOutput = 64 * 1024;
                if (rawOutput->size() + finalChunk.size() <=
                    kMaximumOutput) {
                    rawOutput->append(finalChunk);
                } else {
                    *outputOverflow = true;
                    const qsizetype remaining =
                        qMax<qsizetype>(
                            0, kMaximumOutput - rawOutput->size());
                    rawOutput->append(finalChunk.first(remaining));
                }
                finalChunk.fill('\0');
                QString resultError;
                const QJsonObject result =
                    *outputOverflow
                        ? QJsonObject()
                        : forkmesh::control::
                              parseMirrorActionsConfigurationResult(
                                  *rawOutput, requestId, node, &resultError);
                const bool confirmed =
                    exitStatus == QProcess::NormalExit && exitCode == 0 &&
                    !result.isEmpty() &&
                    result.value(QStringLiteral("ok")).toBool() &&
                    result.value(QStringLiteral("actionsEnabled")).toBool() ==
                        enabled &&
                    result.value(QStringLiteral("variablesReplaced")).toBool() ==
                        replaceVariables &&
                    result.value(QStringLiteral("variableCount")).toInt() ==
                        (replaceVariables ? variableCount : 0);
                if (confirmed) {
                    const QString summary =
                        QStringLiteral(
                            "Mirror %1 confirmed Actions %2%3.")
                            .arg(
                                node,
                                enabled ? QStringLiteral("enabled")
                                        : QStringLiteral("disabled"),
                                replaceVariables
                                    ? QStringLiteral(
                                          " and replaced %1 variable(s)")
                                          .arg(variableCount)
                                    : QString());
                    appendHostInstallLog(
                        QStringLiteral("[Actions] %1\n").arg(summary));
                    if (m_hostInstallStatus)
                        m_hostInstallStatus->setText(summary);
                } else {
                    if (*outputOverflow) {
                        resultError = QStringLiteral(
                            "The remote helper returned more than 64 KiB.");
                    } else if (resultError.isEmpty()) {
                        resultError = QStringLiteral(
                            "The host did not confirm the requested state.");
                    }
                    appendHostInstallLog(
                        QStringLiteral(
                            "[Actions] Configuration was not confirmed "
                            "(exit %1): %2\n")
                            .arg(exitCode)
                            .arg(resultError));
                    if (m_hostInstallStatus) {
                        m_hostInstallStatus->setText(
                            QStringLiteral(
                                "Mirror Actions configuration failed: %1")
                                .arg(resultError));
                    }
                }
                rawOutput->fill('\0');
                rawOutput->clear();
                QProcessEnvironment scrubbed =
                    QProcessEnvironment::systemEnvironment();
                scrubbed.remove(QStringLiteral("SSHPASS"));
                process->setProcessEnvironment(scrubbed);
                if (m_hostActionsProcess == process)
                    m_hostActionsProcess = nullptr;
                process->deleteLater();
            });

    process->start(command.program, command.arguments);
    process->write(command.standardInput);
    process->closeWriteChannel();
    const QPointer<QProcess> guardedProcess(process);
    QTimer::singleShot(45 * 1000, this, [this, guardedProcess, completed] {
        if (!guardedProcess || *completed ||
            guardedProcess->state() == QProcess::NotRunning) {
            return;
        }
        appendHostInstallLog(
            QStringLiteral(
                "[Actions] SSH configuration timed out after 45 seconds.\n"));
        guardedProcess->kill();
    });
    command.standardInput.fill('\0');
    command.standardInput.clear();
    command.environment = QProcessEnvironment();
}

// --- Nodes ------------------------------------------------------------------
//
// A sortable directory of every registered node the relay exposes through its
// public user directory, merged with live roster and serving-only nodes. Each row
// carries the node's platform badge, name, online state, owner, advertised
// ForkMesh version, repo/mirror counts and its CPU/RAM/disk telemetry bars.
// Selecting a row opens a detail panel with the node's full details, the repos
// it hosts and the repos it mirrors.
//
// Online state trusts the relay's /api/network/stats "onlineNodes" list (a
// repository update channel or a fresh signed heartbeat) as the canonical set —
// the same signal the Mirror nodes list and the Network page use. This lets
// headless mirror nodes that serve via the relay without joining this client's
// chat room show online (adhoc #27), and, once the relay set is fetched, stops a
// stale roster entry from painting a node online after it stopped serving
// (adhoc #43). Our own node is the exception: it trusts the local backend, since
// it may host only private repos the relay never lists. Before the first relay
// reply we fall back to the encrypted roster's presence flag.

namespace {
enum NodeCol {
    kNodeColName = 0,
    kNodeColStatus,
    kNodeColOwner,
    kNodeColVersion,
    kNodeColPlatform,
    kNodeColRepos,
    kNodeColMirrors,
    kNodeColCpu,
    kNodeColRam,
    kNodeColDisk,
    kNodeColId,
    kNodeColCount,
};
} // namespace

QWidget *MainWindow::buildNodesSection()
{
    // A tab of the Network section (adhoc #54) — see buildRelaysSection() on the
    // missing page title.
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 8, 0, 0);
    outer->setSpacing(12);

    auto *subtitle = new QLabel(QString::fromUtf8(
        "All registered nodes from the relay directory, including offline "
        "nodes. Click a column header to sort or select a node for details. "
        "\"Online (serving)\" means the relay reports it live even when it is "
        "not connected to this client's chat room."));
    subtitle->setObjectName("mutedLabel");
    subtitle->setWordWrap(true);
    outer->addWidget(subtitle);

    // Status line + manual refresh button.
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    m_nodesStatus = new QLabel;
    m_nodesStatus->setObjectName("mutedLabel");
    controls->addWidget(m_nodesStatus, 1);
    m_nodesRefreshButton = new QPushButton(QStringLiteral("Refresh"));
    m_nodesRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_nodesRefreshButton, "sync", 14);
    connect(m_nodesRefreshButton, &QPushButton::clicked, this, [this] {
        refreshChatUserDirectory();
        fetchRelayOnlineNodes(true); // refreshes the table again on reply
        refreshNodesTable();
    });
    controls->addWidget(m_nodesRefreshButton);
    outer->addLayout(controls);

    // Master (sortable table) on the left, node detail panel on the right.
    auto *split = new QHBoxLayout;
    split->setContentsMargins(0, 0, 0, 0);
    split->setSpacing(16);

    m_nodesTable = new QTableWidget(0, kNodeColCount);
    installColumnHeaderMenu(m_nodesTable); // 3-dots per-column menu (issue #318)
    m_nodesTable->setObjectName("issueTable");
    m_nodesTable->setProperty("nodesDirectory", true);
    m_nodesTable->setHorizontalHeaderLabels(
        {QStringLiteral("Node"), QStringLiteral("Status"),
         QStringLiteral("Owner"), QStringLiteral("Version"),
         QStringLiteral("Platform"), QStringLiteral("Repos"),
         QStringLiteral("Mirrors"), QStringLiteral("CPU"),
         QStringLiteral("RAM"), QStringLiteral("Disk"),
         QStringLiteral("Node id")});
    m_nodesTable->verticalHeader()->setVisible(false);
    m_nodesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nodesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodesTable->setShowGrid(false);
    m_nodesTable->setSortingEnabled(true);
    m_nodesTable->horizontalHeader()->setSortIndicatorShown(true);
    m_nodesTable->horizontalHeader()->setSectionResizeMode(kNodeColName,
                                                           QHeaderView::Stretch);
    for (int c = kNodeColName + 1; c < kNodeColCount; ++c)
        m_nodesTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_nodesTable); // spreadsheet-style draggable columns (#263)
    // CPU / RAM / disk columns render as little usage bars (details on hover),
    // the same delegate the repo detail's Mirror nodes table uses.
    auto *resourceBars = new ResourceBarDelegate(m_nodesTable);
    for (int col : {kNodeColCpu, kNodeColRam, kNodeColDisk})
        m_nodesTable->setItemDelegateForColumn(col, resourceBars);
    connect(m_nodesTable, &QTableWidget::cellClicked, this,
            [this](int row, int) { showNodeDetailForRow(row); });
    split->addWidget(m_nodesTable, 2);

    m_nodeDetailScroll = new QScrollArea;
    m_nodeDetailScroll->setWidgetResizable(true);
    m_nodeDetailScroll->setObjectName("nodeDetailPanel");
    m_nodeDetailScroll->setMinimumWidth(260);
    split->addWidget(m_nodeDetailScroll, 1);

    outer->addLayout(split, 1);

    refreshChatUserDirectory();
    fetchRelayOnlineNodes();
    refreshNodesTable();
    return page;
}

void MainWindow::fetchRelayOnlineNodes(bool force)
{
    if (!m_networkAccess)
        return;
    const QString backoffKey = QStringLiteral("relay-online-nodes");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Throttle: refreshNodesTable runs on every roster flicker; only re-ask the
    // relay once a minute (the response is edge-cached there anyway), or after
    // 15s for an explicit Refresh click. The backoff gate keeps a failing/
    // rate-limited relay from being re-queried on each attempt.
    const qint64 minIntervalMs = force ? 15000 : 60000;
    if (m_relayOnlineNodesFetchedMs > 0 &&
        now - m_relayOnlineNodesFetchedMs < minIntervalMs)
        return;
    if (!m_pollBackoff.ready(backoffKey, now))
        return;
    m_relayOnlineNodesFetchedMs = now;

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/network/stats"));
    url.setQuery(QString());
    QNetworkReply *reply = m_networkAccess->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, backoffKey] {
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        const QJsonObject resp = QJsonDocument::fromJson(body).object();
        if (!ok || !resp.value("ok").toBool()) {
            m_pollBackoff.noteFailure(backoffKey,
                                      QDateTime::currentMSecsSinceEpoch());
            return;
        }
        m_pollBackoff.noteSuccess(backoffKey);
        QSet<QString> online;
        for (const QJsonValue &v : resp.value("onlineNodes").toArray()) {
            const QString name = v.toString().trimmed().toLower();
            if (!name.isEmpty())
                online.insert(name);
        }
        const bool firstReply = !m_relayOnlineNodesFetched;
        m_relayOnlineNodesFetched = true;
        if (!firstReply && online == m_relayOnlineNodes)
            return;
        m_relayOnlineNodes = online;
        refreshNodesTable();
    });
}

void MainWindow::refreshNodesTable()
{
    // No early return on a missing table: the Nodes page is built lazily, but the
    // rail badge has to show the real node count from the first launch on, and
    // this is the only place that knows which roster entries are actually nodes.

    // Ask the relay who is serving right now (throttled internally), so mirror
    // nodes outside this client's chat room still show online. Only while the
    // Nodes page is actually visible — this also runs on every roster tick, and
    // a hidden page must not keep polling the quota-limited relay.
    if (m_nodesTable && m_nodesTable->isVisible())
        fetchRelayOnlineNodes();

    // The roster record (version / owner / telemetry / mirrors) for a node.
    // Prefer an online entry when a reinstall left the same name in the roster
    // twice (old key's session heartbeating beside the new one, adhoc #46), but
    // still backfill the identity fields (owner / version / solana / platform)
    // from the other duplicate when the preferred entry left them blank — a
    // headless heartbeat often omits the owner the earlier hello carried, which
    // is why the Owner column was empty for nodes we clearly know (adhoc #43).
    auto rosterInfo = [this](const QString &name) -> MemberInfo {
        MemberInfo best;
        bool found = false;
        for (const MemberInfo &m : std::as_const(m_homeRoster)) {
            if (nodeListIdentityKey(m) != name)
                continue;
            if (!found || (m.online && !best.online)) {
                best = m;
                found = true;
                continue;
            }
            if (best.ownerUser.trimmed().isEmpty())
                best.ownerUser = m.ownerUser;
            if (best.version.trimmed().isEmpty())
                best.version = m.version;
            if (best.solanaAddress.trimmed().isEmpty())
                best.solanaAddress = m.solanaAddress;
            if (best.platform.trimmed().isEmpty())
                best.platform = m.platform;
            if (best.mirrors.isEmpty() && !m.mirrors.isEmpty()) {
                best.mirrors = m.mirrors;
                best.mirrorDetails = m.mirrorDetails;
            }
        }
        return best;
    };
    auto relayOnline = [this](const QString &name) {
        return m_relayOnlineNodes.contains(name.trimmed().toLower());
    };
    const QString dash = QString::fromUtf8("\xE2\x80\x94");

    // Build the database-backed node -> owning-user map. The public user
    // directory deliberately exposes linked node names but no private profile
    // fields, and unlike the live roster it retains offline nodes.
    QHash<QString, QString> directoryOwner;
    for (const MemberInfo &user : std::as_const(m_chatDirectoryUsers)) {
        const QString owner = user.name.trimmed();
        const QStringList nodes =
            user.nodeName.split(QStringLiteral(", "), Qt::SkipEmptyParts);
        for (const QString &node : nodes) {
            const QString key = node.trimmed().toLower();
            if (!key.isEmpty())
                directoryOwner.insert(key, owner);
        }
    }

    // Drop entries whose roster identity is a plain user account (a chat-only
    // human/bot, accountKind "user") rather than a real serving node — e.g.
    // ForkBot's relayed replies or a desktop profile signed in as a user, not a
    // linked node. Missing accountKind (older peers, or a name only known via
    // a locally hosted repo's owner field) still counts as a node.
    //
    // Our own row is the same story: when this desktop is signed in as a user
    // account (it owns a node fleet), the account name is a *user*, not a node —
    // its nodes show as their own rows. The backend stamps this same predicate as
    // accountKind "user" on the self roster entry, but that self row also carries
    // live telemetry and can be painted before the "user" kind propagates, which
    // left the user showing as a node (adhoc #37: "jett" listed as a node). Gate
    // the self row on the local predicate directly so it never leaks through.
    const bool selfIsUserAccount =
        m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty();
    // A name the public account directory lists as a *user* and that no account
    // lists as a linked node is a person, not a node. Those reach the switcher
    // list purely as repository owners (refreshRepositoryList adds an entry for
    // every repo owner) and never carry a roster identity to be filtered by
    // accountKind, so every account with a listed repo was being counted and
    // drawn as a node (adhoc #26). An owner whose machine node shares the account
    // name stays: the relay's live set still reports it serving.
    auto isDirectoryUserOnly = [&](const QString &key) {
        return m_chatDirectoryUsers.contains(key) &&
               !directoryOwner.contains(key) && !relayOnline(key);
    };
    QList<NodeMenuEntry> visible;
    QList<MemberInfo> visibleRoster;
    QSet<QString> visibleNames;
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        MemberInfo mi = rosterInfo(e.name);
        if (mi.accountKind == QLatin1String("user"))
            continue;
        // Temporary world-chat visitors are filtered before they become menu
        // entries (refreshRepositoryList); re-check here so one can never show
        // as a node even if it slips in by another path (adhoc #308).
        if (isTemporaryChatGuest(mi))
            continue;
        if (e.self && selfIsUserAccount)
            continue;
        const QString key = e.name.trimmed().toLower();
        if (key.isEmpty() || visibleNames.contains(key))
            continue;
        if (!e.self && isDirectoryUserOnly(key))
            continue;
        if (mi.ownerUser.trimmed().isEmpty())
            mi.ownerUser = directoryOwner.value(key);
        visible.append(e);
        visibleRoster.append(mi);
        visibleNames.insert(key);
    }
    // Add every linked database node that is not currently in the roster.
    // Repository counts are filled from the local/catalog cache when available;
    // the relay's online set supplies liveness for headless nodes.
    QStringList directoryNodes = directoryOwner.keys();
    std::sort(directoryNodes.begin(), directoryNodes.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    for (const QString &key : std::as_const(directoryNodes)) {
        if (visibleNames.contains(key))
            continue;
        NodeMenuEntry entry;
        entry.name = key;
        entry.online = relayOnline(key);
        for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
            if (!repo.previewOnly &&
                repo.owner.compare(key, Qt::CaseInsensitive) == 0)
                ++entry.repoCount;
        }
        MemberInfo info = rosterInfo(key);
        info.ownerUser = directoryOwner.value(key);
        visible.append(entry);
        visibleRoster.append(info);
        visibleNames.insert(key);
    }
    // A serving-only node can be present in the relay's authoritative live set
    // before its owning user's cached directory record reaches this client.
    QStringList servingNodes = m_relayOnlineNodes.values();
    std::sort(servingNodes.begin(), servingNodes.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    for (const QString &key : std::as_const(servingNodes)) {
        if (key.isEmpty() || visibleNames.contains(key))
            continue;
        NodeMenuEntry entry;
        entry.name = key;
        entry.online = true;
        MemberInfo info = rosterInfo(key);
        visible.append(entry);
        visibleRoster.append(info);
        visibleNames.insert(key);
    }

    // The Nodes count is the rows this page would show — real serving nodes —
    // and nothing else. It used to be re-stamped with m_nodeMenuEntries.size()
    // right after this function ran (updateNodeSwitcher), which is the *unfiltered*
    // switcher list: every chat user account, world-chat guest and repo owner in
    // it was counted as a node, so a mesh of four nodes badged "21" (adhoc #26).
    updateNetworkCounts(-1, visible.size(), -1);
    // Same filtered list feeds the chrome line's node dots, so they show the
    // mesh from launch instead of only while a repo's Mirror-nodes tab is open
    // (adhoc #79). Also runs before the page is built, for the same reason the
    // count above does.
    m_nodeDotEntries = visible;
    refreshNodeDotMatrix();
    if (!m_nodesTable)
        return; // page not built yet — the count above is all that's on screen

    // Which node the detail panel is currently showing, so a rebuild can keep it.
    const QString shown = m_nodesTable->property("shownNode").toString();

    // Populate with sorting off so inserted rows don't reshuffle mid-fill.
    m_nodesTable->setSortingEnabled(false);
    m_nodesTable->setRowCount(visible.size());
    int online = 0;
    for (int i = 0; i < visible.size(); ++i) {
        const NodeMenuEntry &e = visible.at(i);
        const MemberInfo &mi = visibleRoster.at(i);
        // The relay's /api/network/stats "onlineNodes" is the network-canonical
        // live set (an update channel or a fresh signed heartbeat) — the same
        // signal the Mirror nodes list and the Network page trust. Once we have
        // fetched it, it is authoritative even over a roster entry that still
        // says "online": a node whose chat socket lingers after the machine
        // stopped serving was being painted online here when it no longer was
        // (adhoc #43). Our own node is the exception — trust the local backend
        // for it, since we may host only private repos the relay never lists.
        const bool relayLive = relayOnline(e.name);
        const bool rosterLive = e.online;
        bool isOnline;
        if (e.self)
            isOnline = rosterLive;
        else if (m_relayOnlineNodesFetched)
            isOnline = relayLive;
        else
            isOnline = rosterLive || relayLive; // pre-fetch fallback
        // "serving" = live via the relay without being a live chat-room member.
        const bool serving = isOnline && !e.self && !rosterLive;
        if (isOnline)
            ++online;

        QString label = e.name.isEmpty() ? QStringLiteral("(unnamed)") : e.name;
        if (e.self)
            label += QStringLiteral("  (this machine)");
        auto *nameItem =
            new QTableWidgetItem(osBadgeIcon(e.platform, isOnline, 16), label);
        // Stash the real node name so a row stays identifiable after re-sorting.
        nameItem->setData(Qt::UserRole, e.name);
        nameItem->setData(Qt::UserRole + 1, mi.ownerUser.trimmed());
        nameItem->setData(Qt::UserRole + 2, e.platform.trimmed());
        nameItem->setData(Qt::UserRole + 3, e.repoCount);
        nameItem->setData(Qt::UserRole + 4, int(mi.mirrors.size()));
        m_nodesTable->setItem(i, kNodeColName, nameItem);

        auto *statusItem = new QTableWidgetItem(
            !isOnline ? QStringLiteral("Offline")
                      : (serving ? QStringLiteral("Online (serving)")
                                 : QStringLiteral("Online")));
        if (serving)
            statusItem->setToolTip(QStringLiteral(
                "The relay reports this node live (update channel / signed "
                "heartbeat) even though it isn't in this client's chat room."));
        m_nodesTable->setItem(i, kNodeColStatus, statusItem);

        const QString owner = mi.ownerUser.trimmed();
        m_nodesTable->setItem(i, kNodeColOwner,
                              new QTableWidgetItem(owner.isEmpty() ? dash : owner));

        const QString version = mi.version.trimmed();
        m_nodesTable->setItem(i, kNodeColVersion, new QTableWidgetItem(
            version.isEmpty() ? dash : version));

        // Platform / node id as text columns too, matching the repo detail's
        // Mirror nodes table (the badge on the name only hints the platform).
        QString platformText = e.platform.trimmed();
        if (platformText.isEmpty())
            platformText = mi.platform.trimmed();
        m_nodesTable->setItem(i, kNodeColPlatform, new QTableWidgetItem(
            platformText.isEmpty() ? dash : platformText));

        auto *repoItem = new QTableWidgetItem;
        repoItem->setData(Qt::DisplayRole, e.repoCount); // int -> numeric sort
        repoItem->setTextAlignment(Qt::AlignCenter);
        m_nodesTable->setItem(i, kNodeColRepos, repoItem);

        auto *mirrorItem = new QTableWidgetItem;
        mirrorItem->setData(Qt::DisplayRole, int(mi.mirrors.size()));
        mirrorItem->setTextAlignment(Qt::AlignCenter);
        m_nodesTable->setItem(i, kNodeColMirrors, mirrorItem);

        // CPU / RAM / disk usage bars from the node's advertised telemetry
        // (empty bar cell when the node didn't advertise the metric).
        m_nodesTable->setItem(i, kNodeColCpu, makeCpuUsageCell(mi.cpuPercent));
        m_nodesTable->setItem(i, kNodeColRam,
            makeByteUsageCell(QStringLiteral("RAM"), mi.memUsedBytes,
                              mi.memTotalBytes));
        m_nodesTable->setItem(i, kNodeColDisk,
            makeByteUsageCell(QStringLiteral("Disk"), mi.diskUsedBytes,
                              mi.diskTotalBytes));

        // Stable node id (public key), shortened like the Mirror nodes table;
        // the full key stays readable via the tooltip.
        const QString nodeId = mi.id.trimmed();
        auto *idItem = new QTableWidgetItem(
            nodeId.isEmpty()
                ? dash
                : nodeId.left(12) + (nodeId.size() > 12
                                         ? QString::fromUtf8("\xE2\x80\xA6")
                                         : QString()));
        if (!nodeId.isEmpty())
            idItem->setToolTip(nodeId);
        m_nodesTable->setItem(i, kNodeColId, idItem);
    }
    m_nodesTable->setSortingEnabled(true);

    if (m_nodesStatus) {
        m_nodesStatus->setText(visible.isEmpty()
            ? QStringLiteral("No nodes known yet.")
            : QString::fromUtf8("%1 node%2 \xC2\xB7 %3 online")
                  .arg(visible.size())
                  .arg(visible.size() == 1 ? "" : "s")
                  .arg(online));
    }
    // Re-open the previously shown node's detail (find it by name post-sort), or
    // default to the first row.
    if (m_nodesTable->rowCount() > 0) {
        int target = 0;
        for (int r = 0; r < m_nodesTable->rowCount(); ++r) {
            QTableWidgetItem *it = m_nodesTable->item(r, 0);
            if (it && it->data(Qt::UserRole).toString() == shown) {
                target = r;
                break;
            }
        }
        m_nodesTable->selectRow(target);
        showNodeDetailForRow(target);
    } else {
        showNodeDetailForRow(-1);
    }
}

void MainWindow::showNodeDetailForRow(int row)
{
    if (!m_nodeDetailScroll)
        return;

    // Placeholder when there is no valid selection.
    if (!m_nodesTable || row < 0 || row >= m_nodesTable->rowCount() ||
        !m_nodesTable->item(row, 0)) {
        if (m_nodesTable)
            m_nodesTable->setProperty("shownNode", QString());
        auto *empty =
            new QLabel(QStringLiteral("Select a node to see its details."));
        empty->setObjectName("mutedLabel");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_nodeDetailScroll->setWidget(empty);
        return;
    }

    const QString node = m_nodesTable->item(row, 0)->data(Qt::UserRole).toString();
    QTableWidgetItem *nodeItem = m_nodesTable->item(row, 0);
    m_nodesTable->setProperty("shownNode", node);

    // The dropdown entry (platform / online / repo count) and the roster record
    // (version / owner / telemetry) for this node.
    NodeMenuEntry entry;
    for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
        if (e.name == node) { entry = e; break; }
    }
    MemberInfo mi;
    bool inRoster = false;
    for (const MemberInfo &m : std::as_const(m_homeRoster)) {
        if (nodeListIdentityKey(m) != node)
            continue;
        // Prefer an online entry when a reinstall left the name twice (adhoc #46),
        // but backfill blank identity fields from the other duplicate so the
        // owner/version don't drop out with a headless heartbeat (adhoc #43).
        if (!inRoster || (m.online && !mi.online)) {
            mi = m;
        } else {
            if (mi.ownerUser.trimmed().isEmpty())
                mi.ownerUser = m.ownerUser;
            if (mi.version.trimmed().isEmpty())
                mi.version = m.version;
            if (mi.solanaAddress.trimmed().isEmpty())
                mi.solanaAddress = m.solanaAddress;
            if (mi.platform.trimmed().isEmpty())
                mi.platform = m.platform;
            if (mi.mirrors.isEmpty() && !m.mirrors.isEmpty()) {
                mi.mirrors = m.mirrors;
                mi.mirrorDetails = m.mirrorDetails;
            }
        }
        inRoster = true;
    }
    if (mi.ownerUser.trimmed().isEmpty() && nodeItem)
        mi.ownerUser = nodeItem->data(Qt::UserRole + 1).toString();
    if (entry.platform.trimmed().isEmpty() && nodeItem)
        entry.platform = nodeItem->data(Qt::UserRole + 2).toString();
    if (entry.repoCount == 0 && nodeItem)
        entry.repoCount = nodeItem->data(Qt::UserRole + 3).toInt();
    // Liveness mirrors refreshNodesTable(): the relay's authoritative live set
    // wins over a lingering roster entry once we have fetched it; our own node
    // trusts the local backend (adhoc #43).
    const bool relayLive = m_relayOnlineNodes.contains(node.trimmed().toLower());
    const bool rosterLive = entry.online;
    bool isOnline;
    if (entry.self)
        isOnline = rosterLive;
    else if (m_relayOnlineNodesFetched)
        isOnline = relayLive;
    else
        isOnline = rosterLive || relayLive;
    const bool serving = isOnline && !entry.self && !rosterLive;

    auto *content = new QWidget;
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(16, 16, 16, 16);
    col->setSpacing(8);

    // Header: platform badge + node name.
    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    auto *badge = new QLabel;
    badge->setPixmap(osBadgeIcon(entry.platform, isOnline, 28).pixmap(28, 28));
    head->addWidget(badge);
    auto *nameLbl =
        new QLabel(node.isEmpty() ? QStringLiteral("(unnamed node)") : node);
    QFont nf = nameLbl->font();
    nf.setPointSizeF(nf.pointSizeF() + 3);
    nf.setBold(true);
    nameLbl->setFont(nf);
    nameLbl->setWordWrap(true);
    head->addWidget(nameLbl, 1);
    col->addLayout(head);

    auto addRow = [&](const QString &k, const QString &v) {
        if (v.trimmed().isEmpty())
            return;
        auto *l = new QLabel(
            QStringLiteral("<b>%1:</b> %2").arg(k, v.toHtmlEscaped()));
        l->setTextFormat(Qt::RichText);
        l->setWordWrap(true);
        col->addWidget(l);
    };

    addRow(QStringLiteral("Status"),
           !isOnline
               ? QStringLiteral("Offline")
               : (serving
                      ? QString::fromUtf8(
                            "Online \xE2\x80\x94 serving via the relay (host "
                            "tunnel / signed heartbeat), not in this client's "
                            "chat room")
                      : QStringLiteral("Online")));
    if (entry.self)
        addRow(QStringLiteral("This machine"), QStringLiteral("Yes"));
    QString platform = entry.platform.trimmed();
    if (platform.isEmpty())
        platform = mi.platform.trimmed();
    addRow(QStringLiteral("Platform"),
           platform.isEmpty() ? QStringLiteral("unknown") : platform);
    addRow(QStringLiteral("Version"), mi.version.trimmed());
    addRow(QStringLiteral("Owner"), mi.ownerUser.trimmed());
    addRow(QStringLiteral("Solana"), mi.solanaAddress.trimmed());
    // The stable node id (public key) direct messages are addressed to.
    // Shortened: the full key is long and unbroken, which stretches the panel.
    if (!mi.id.trimmed().isEmpty()) {
        const QString id = mi.id.trimmed();
        addRow(QStringLiteral("Node ID"),
               id.size() > 20 ? id.left(20) + QString::fromUtf8("\xE2\x80\xA6")
                              : id);
    }
    addRow(QStringLiteral("Repositories"), QString::number(entry.repoCount));
    addRow(QStringLiteral("Mirrors"), QString::number(mi.mirrors.size()));

    // Host telemetry, when the node advertised it.
    if (mi.cpuPercent >= 0.0)
        addRow(QStringLiteral("CPU"),
               QStringLiteral("%1%").arg(mi.cpuPercent, 0, 'f', 0));
    if (mi.memTotalBytes > 0)
        addRow(QStringLiteral("Memory"),
               QStringLiteral("%1 / %2").arg(
                   SystemStats::formatBytes(mi.memUsedBytes),
                   SystemStats::formatBytes(mi.memTotalBytes)));
    if (mi.diskTotalBytes > 0)
        addRow(QStringLiteral("Disk"),
               QStringLiteral("%1 / %2").arg(
                   SystemStats::formatBytes(mi.diskUsedBytes),
                   SystemStats::formatBytes(mi.diskTotalBytes)));

    // Repositories hosted by this node.
    auto *reposLbl = new QLabel(QStringLiteral("Repositories"));
    QFont rlf = reposLbl->font();
    rlf.setBold(true);
    reposLbl->setFont(rlf);
    reposLbl->setContentsMargins(0, 8, 0, 0);
    col->addWidget(reposLbl);

    int shownRepos = 0;
    for (const RepositoryRecord &repo : std::as_const(m_repositories)) {
        if (repo.owner != node || repo.previewOnly)
            continue;
        ++shownRepos;
        auto *r = new QLabel;
        r->setTextFormat(Qt::RichText);
        r->setWordWrap(true);
        QString line = QStringLiteral("\xE2\x80\xA2 <b>%1</b>")
                           .arg(repo.name.toHtmlEscaped());
        if (repo.isPrivate)
            line += QStringLiteral(" \xC2\xB7 private");
        if (!repo.description.trimmed().isEmpty())
            line += QStringLiteral(
                        "<br><span style='color:#8b949e'>%1</span>")
                        .arg(repo.description.trimmed().toHtmlEscaped());
        r->setText(line);
        col->addWidget(r);
    }
    if (shownRepos == 0) {
        auto *none =
            new QLabel(QStringLiteral("No repositories hosted by this node."));
        none->setObjectName("mutedLabel");
        none->setWordWrap(true);
        col->addWidget(none);
    }

    // Repositories this node advertises mirroring, with each mirror's branch,
    // served commit, size and how recently it synced (when advertised).
    auto *mirrorsLbl = new QLabel(QStringLiteral("Mirrored repositories"));
    QFont mlf = mirrorsLbl->font();
    mlf.setBold(true);
    mirrorsLbl->setFont(mlf);
    mirrorsLbl->setContentsMargins(0, 8, 0, 0);
    col->addWidget(mirrorsLbl);

    int shownMirrors = 0;
    for (const MirrorAdvert &advert : std::as_const(mi.mirrorDetails)) {
        const QString name = !advert.source.trimmed().isEmpty()
                                 ? advert.source.trimmed()
                                 : advert.ownerName.trimmed();
        if (name.isEmpty())
            continue;
        ++shownMirrors;
        QString line = QStringLiteral("\xE2\x80\xA2 <b>%1</b>")
                           .arg(name.toHtmlEscaped());
        if (!advert.branch.trimmed().isEmpty()) {
            line += QStringLiteral(" \xC2\xB7 %1")
                        .arg(advert.branch.trimmed().toHtmlEscaped());
            if (!advert.commit.trimmed().isEmpty())
                line += QStringLiteral(" @ %1")
                            .arg(advert.commit.trimmed().left(8).toHtmlEscaped());
        }
        if (advert.sizeBytes > 0)
            line += QStringLiteral(" \xC2\xB7 %1")
                        .arg(formatByteSize(advert.sizeBytes));
        if (advert.updatedMs > 0)
            line += QStringLiteral(" \xC2\xB7 synced %1 ago")
                        .arg(formatShortRelativeTime(advert.updatedMs / 1000));
        // The same per-mirror tallies the repo detail's Mirror nodes table
        // shows, when the node advertised them (-1 = older peer / unknown).
        auto appendCount = [&line](int value, const char *noun) {
            if (value >= 0)
                line += QString::fromUtf8(" \xC2\xB7 %1 %2")
                            .arg(value)
                            .arg(QLatin1String(noun));
        };
        appendCount(advert.commitCount, "commits");
        appendCount(advert.branchCount, "branches");
        appendCount(advert.issueCount, "issues");
        appendCount(advert.pullCount, "pulls");
        appendCount(advert.discussionCount, "discussions");
        appendCount(advert.artifactCount, "artifacts");
        auto *m = new QLabel(line);
        m->setTextFormat(Qt::RichText);
        m->setWordWrap(true);
        col->addWidget(m);
    }
    // Older peers advertise mirror names without per-repo detail.
    if (shownMirrors == 0) {
        for (const QString &name : std::as_const(mi.mirrors)) {
            if (name.trimmed().isEmpty())
                continue;
            ++shownMirrors;
            auto *m = new QLabel(QStringLiteral("\xE2\x80\xA2 <b>%1</b>")
                                     .arg(name.trimmed().toHtmlEscaped()));
            m->setTextFormat(Qt::RichText);
            m->setWordWrap(true);
            col->addWidget(m);
        }
    }
    if (shownMirrors == 0) {
        auto *none =
            new QLabel(QStringLiteral("No mirrors advertised by this node."));
        none->setObjectName("mutedLabel");
        none->setWordWrap(true);
        col->addWidget(none);
    }

    col->addStretch();
    m_nodeDetailScroll->setWidget(content);
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
    // A tab of the Network section (adhoc #54), so no page title of its own —
    // the section header above the tab bar already says "Network".
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 8, 0, 0);
    outer->setSpacing(12);

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
    updateNetworkCounts(m_servers.size(), -1, -1);
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
        // Same measurement the relay dropdown wants, so keep its cache warm
        // (adhoc #124) rather than re-probing the host a second time.
        setRelayLinkSpeed(host, reply->error() == QNetworkReply::NoError
                                    ? static_cast<int>(elapsed)
                                    : -1);
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

// --- Firewall ---------------------------------------------------------------

namespace {

int requestFirewallPort(const QUrl &url)
{
    if (url.port() > 0)
        return url.port();
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https") || scheme == QLatin1String("wss"))
        return 443;
    if (scheme == QLatin1String("http") || scheme == QLatin1String("ws"))
        return 80;
    return -1;
}

QString requestFirewallDestination(const QUrl &url)
{
    const QString host = url.host().toLower();
    const int port = requestFirewallPort(url);
    return port > 0 ? QStringLiteral("%1:%2").arg(host).arg(port) : host;
}

QString requestFirewallUser()
{
#ifndef Q_OS_WIN
    return QString::number(getuid());
#else
    const QString user = qEnvironmentVariable("USERNAME");
    return user.isEmpty() ? QStringLiteral("current user") : user;
#endif
}

BackoffNetworkAccessManager *requestFirewallManager(QNetworkAccessManager *manager)
{
    return qobject_cast<BackoffNetworkAccessManager *>(manager);
}

} // namespace

QWidget *MainWindow::buildFirewallSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Firewall"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    outer->addWidget(title);

    auto *summary = new QLabel(QStringLiteral(
        "Whitelist only. Unlisted ForkMesh HTTP and relay-tunnel destinations "
        "ask before connecting."));
    summary->setObjectName("mutedLabel");
    summary->setWordWrap(true);
    outer->addWidget(summary);

    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    m_requestFirewallEnabledCheck = new QCheckBox(QStringLiteral("Whitelist only"));
    m_requestFirewallEnabledCheck->setCursor(Qt::PointingHandCursor);
    const bool enabled =
        QSettings().value(kRequestFirewallEnabledSetting, true).toBool();
    m_requestFirewallEnabledCheck->setChecked(enabled);
    connect(m_requestFirewallEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) {
                QSettings().setValue(kRequestFirewallEnabledSetting, on);
                if (auto *manager = requestFirewallManager(m_networkAccess))
                    manager->setFirewallEnabled(on);
                logSystem(on ? QStringLiteral("Firewall: whitelist-only mode enabled.")
                             : QStringLiteral("Firewall: whitelist-only mode disabled."));
                refreshFirewallTables();
            });
    controls->addWidget(m_requestFirewallEnabledCheck);
    m_requestFirewallStatus = new QLabel;
    m_requestFirewallStatus->setObjectName("mutedLabel");
    controls->addWidget(m_requestFirewallStatus, 1);
    auto *clearHistory = new QPushButton(QStringLiteral("Clear history"));
    clearHistory->setObjectName("repoAction");
    clearHistory->setCursor(Qt::PointingHandCursor);
    setOcticon(clearHistory, "trash", 16);
    connect(clearHistory, &QPushButton::clicked, this,
            &MainWindow::clearFirewallHistory);
    controls->addWidget(clearHistory);
    outer->addLayout(controls);

    auto *ruleRow = new QHBoxLayout;
    ruleRow->setContentsMargins(0, 0, 0, 0);
    m_requestFirewallRuleEdit = new QLineEdit;
    m_requestFirewallRuleEdit->setPlaceholderText(
        QStringLiteral("host, host:port, *.domain, https://host/path, scheme:https, *"));
    ruleRow->addWidget(m_requestFirewallRuleEdit, 1);
    auto *addRule = new QPushButton(QStringLiteral("Add"));
    addRule->setObjectName("repoAction");
    addRule->setCursor(Qt::PointingHandCursor);
    setOcticon(addRule, "plus", 16);
    connect(addRule, &QPushButton::clicked, this,
            &MainWindow::addFirewallRuleFromEdit);
    connect(m_requestFirewallRuleEdit, &QLineEdit::returnPressed, this,
            &MainWindow::addFirewallRuleFromEdit);
    ruleRow->addWidget(addRule);
    m_requestFirewallRemoveButton = new QPushButton(QStringLiteral("Remove"));
    m_requestFirewallRemoveButton->setObjectName("repoAction");
    m_requestFirewallRemoveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_requestFirewallRemoveButton, "trash", 16);
    connect(m_requestFirewallRemoveButton, &QPushButton::clicked, this,
            &MainWindow::removeSelectedFirewallRules);
    ruleRow->addWidget(m_requestFirewallRemoveButton);
    outer->addLayout(ruleRow);

    auto *rulesLabel = new QLabel(QStringLiteral("Whitelist"));
    rulesLabel->setObjectName("sectionLabel");
    outer->addWidget(rulesLabel);

    m_requestFirewallRulesTable = new QTableWidget(0, 2);
    installColumnHeaderMenu(m_requestFirewallRulesTable);
    m_requestFirewallRulesTable->setObjectName("issueTable");
    m_requestFirewallRulesTable->setHorizontalHeaderLabels(
        {QStringLiteral("Rule"), QStringLiteral("Stored as")});
    m_requestFirewallRulesTable->verticalHeader()->setVisible(false);
    m_requestFirewallRulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_requestFirewallRulesTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_requestFirewallRulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestFirewallRulesTable->setShowGrid(false);
    m_requestFirewallRulesTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_requestFirewallRulesTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    makeColumnsResizable(m_requestFirewallRulesTable);
    outer->addWidget(m_requestFirewallRulesTable, 1);

    auto *historyLabel = new QLabel(QStringLiteral("Recent requests"));
    historyLabel->setObjectName("sectionLabel");
    outer->addWidget(historyLabel);

    m_requestFirewallHistoryTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_requestFirewallHistoryTable);
    m_requestFirewallHistoryTable->setObjectName("issueTable");
    m_requestFirewallHistoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Decision"), QStringLiteral("Method"),
         QStringLiteral("Destination"), QStringLiteral("Rule"),
         QStringLiteral("When")});
    m_requestFirewallHistoryTable->verticalHeader()->setVisible(false);
    m_requestFirewallHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_requestFirewallHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestFirewallHistoryTable->setShowGrid(false);
    m_requestFirewallHistoryTable->setSortingEnabled(true);
    m_requestFirewallHistoryTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    for (int c : {0, 1, 3, 4})
        m_requestFirewallHistoryTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_requestFirewallHistoryTable);
    outer->addWidget(m_requestFirewallHistoryTable, 1);

    refreshFirewallTables();
    return page;
}

void MainWindow::refreshFirewallTables()
{
    auto *manager = requestFirewallManager(m_networkAccess);
    const QStringList rules =
        manager ? manager->firewallRules() : requestFirewallWhitelistWithDefaults();
    const bool enabled =
        manager ? manager->firewallEnabled()
                : QSettings().value(kRequestFirewallEnabledSetting, true).toBool();

    if (m_requestFirewallEnabledCheck) {
        QSignalBlocker block(m_requestFirewallEnabledCheck);
        m_requestFirewallEnabledCheck->setChecked(enabled);
    }
    if (m_requestFirewallStatus) {
        int blocked = 0;
        for (const FirewallHistoryEntry &entry : std::as_const(m_requestFirewallHistory))
            if (!entry.allowed)
                ++blocked;
        m_requestFirewallStatus->setText(
            QStringLiteral("%1 - %2 whitelist rule(s), %3 blocked this session")
                .arg(enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"))
                .arg(rules.size())
                .arg(blocked));
    }

    if (m_requestFirewallRulesTable) {
        TableRepaintGuard repaintGuard(m_requestFirewallRulesTable);
        m_requestFirewallRulesTable->setRowCount(0);
        for (const QString &rule : rules) {
            const int row = m_requestFirewallRulesTable->rowCount();
            m_requestFirewallRulesTable->insertRow(row);
            auto *label = new QTableWidgetItem(
                BackoffNetworkAccessManager::firewallRuleLabel(rule));
            label->setData(Qt::UserRole, rule);
            auto *raw = new QTableWidgetItem(rule);
            raw->setData(Qt::UserRole, rule);
            m_requestFirewallRulesTable->setItem(row, 0, label);
            m_requestFirewallRulesTable->setItem(row, 1, raw);
        }
        m_requestFirewallRulesTable->resizeColumnsToContents();
        m_requestFirewallRulesTable->resizeRowsToContents();
    }

    if (m_requestFirewallHistoryTable) {
        TableRepaintGuard repaintGuard(m_requestFirewallHistoryTable);
        m_requestFirewallHistoryTable->setSortingEnabled(false);
        m_requestFirewallHistoryTable->setRowCount(0);
        for (const FirewallHistoryEntry &entry : std::as_const(m_requestFirewallHistory)) {
            const int row = m_requestFirewallHistoryTable->rowCount();
            m_requestFirewallHistoryTable->insertRow(row);
            auto *decision =
                new QTableWidgetItem(entry.allowed ? QStringLiteral("Allowed")
                                                   : QStringLiteral("Blocked"));
            decision->setForeground(entry.allowed ? QColor(QStringLiteral("#3fb950"))
                                                  : QColor(QStringLiteral("#f85149")));
            m_requestFirewallHistoryTable->setItem(row, 0, decision);
            m_requestFirewallHistoryTable->setItem(row, 1,
                new QTableWidgetItem(entry.method));
            auto *dest = new QTableWidgetItem(entry.destination);
            dest->setToolTip(entry.url);
            m_requestFirewallHistoryTable->setItem(row, 2, dest);
            m_requestFirewallHistoryTable->setItem(row, 3,
                new QTableWidgetItem(
                    entry.rule.isEmpty()
                        ? QStringLiteral("-")
                        : BackoffNetworkAccessManager::firewallRuleLabel(entry.rule)));
            auto *when = new QTableWidgetItem(formatRepoDate(entry.timestampMs));
            when->setData(Qt::UserRole, static_cast<qlonglong>(entry.timestampMs));
            m_requestFirewallHistoryTable->setItem(row, 4, when);
        }
        m_requestFirewallHistoryTable->setSortingEnabled(true);
        m_requestFirewallHistoryTable->sortItems(4, Qt::DescendingOrder);
        m_requestFirewallHistoryTable->resizeColumnsToContents();
        m_requestFirewallHistoryTable->resizeRowsToContents();
    }
}

// --- Network diagnostics ----------------------------------------------------

namespace {

constexpr int kNetworkEndpointKindRole = Qt::UserRole + 100;
constexpr int kNetworkEndpointMethodRole = Qt::UserRole + 101;
constexpr int kNetworkEndpointUrlRole = Qt::UserRole + 102;

QString networkDiagText(const QJsonObject &object, const char *key,
                        const QString &fallback = QString())
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    const QString text = value.toString();
    return text.isEmpty() ? fallback : text;
}

qint64 networkDiagInt(const QJsonObject &object, const char *key)
{
    return qRound64(object.value(QString::fromLatin1(key)).toDouble());
}

QString networkDiagBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', value >= 10.0 ? 1 : 2),
             QString::fromLatin1(units[unit]));
}

QString networkDiagTraffic(const QJsonObject &row, const char *framesKey,
                           const char *bytesKey, const char *controlKey)
{
    const qint64 frames = networkDiagInt(row, framesKey);
    const qint64 bytes = networkDiagInt(row, bytesKey);
    const qint64 control = networkDiagInt(row, controlKey);
    QString text = QStringLiteral("%1 frame(s), %2")
                       .arg(frames)
                       .arg(networkDiagBytes(bytes));
    if (control > 0)
        text += QStringLiteral(" + %1 control").arg(control);
    return text;
}

QString networkDiagLast(const QJsonObject &row, bool outgoing)
{
    const QString type = networkDiagText(row, outgoing ? "lastTxType" : "lastRxType");
    const QString op = networkDiagText(row, outgoing ? "lastTxOp" : "lastRxOp");
    const QString scope =
        networkDiagText(row, outgoing ? "lastTxScope" : "lastRxScope");
    const qint64 bytes =
        networkDiagInt(row, outgoing ? "lastTxBytes" : "lastRxBytes");
    const qint64 when = networkDiagInt(row, outgoing ? "lastTxMs" : "lastRxMs");

    QStringList parts;
    QString label = type;
    if (!op.isEmpty())
        label += QStringLiteral("/") + op;
    if (!label.isEmpty())
        parts << label;
    if (!scope.isEmpty())
        parts << scope;
    if (bytes > 0)
        parts << networkDiagBytes(bytes);
    if (when > 0)
        parts << formatRepoDate(when);
    return parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral(" - "));
}

QTableWidgetItem *networkDiagItem(const QString &text,
                                  const QString &tooltip = QString())
{
    auto *item = new QTableWidgetItem(text);
    if (!tooltip.isEmpty())
        item->setToolTip(tooltip);
    return item;
}

QTableWidgetItem *networkDiagNumberItem(qint64 value, const QString &suffix = QString())
{
    auto *item = new QTableWidgetItem(
        suffix.isEmpty() ? QString::number(value)
                         : QStringLiteral("%1 %2").arg(value).arg(suffix));
    item->setData(Qt::UserRole, static_cast<qlonglong>(value));
    return item;
}

QString networkEndpointStatus(const BackoffNetworkAccessManager::EndpointStats &stats)
{
    if (!stats.lastError.isEmpty())
        return stats.lastStatus > 0
                   ? QStringLiteral("ERR %1").arg(stats.lastStatus)
                   : stats.lastError;
    if (stats.lastStatus > 0)
        return QString::number(stats.lastStatus);
    return QStringLiteral("Called");
}

QString networkEndpointFirewall(const BackoffNetworkAccessManager::EndpointStats &stats)
{
    if (stats.blocked > 0)
        return QStringLiteral("%1 allowed / %2 blocked")
            .arg(stats.allowed)
            .arg(stats.blocked);
    if (stats.allowed > 0)
        return QStringLiteral("Allowed");
    return stats.firewallDecision.isEmpty() ? QStringLiteral("-")
                                            : stats.firewallDecision;
}

QString networkRequestStatus(
    const BackoffNetworkAccessManager::RequestRecord &record)
{
    if (record.blocked)
        return QStringLiteral("Blocked");
    if (!record.error.isEmpty())
        return record.status > 0 ? QStringLiteral("ERR %1").arg(record.status)
                                 : record.error;
    if (record.status > 0)
        return QString::number(record.status);
    return record.finishedMs > 0 ? QStringLiteral("Done")
                                 : QStringLiteral("Pending");
}

constexpr qint64 kNetworkEndpointFlashMs = 2200;

QColor networkEndpointFlashColor(qint64 lastMs, qint64 nowMs)
{
    if (lastMs <= 0 || nowMs < lastMs)
        return QColor();
    const qint64 age = nowMs - lastMs;
    if (age >= kNetworkEndpointFlashMs)
        return QColor();
    const double t = 1.0 - double(age) / double(kNetworkEndpointFlashMs);
    QColor color(QStringLiteral("#238636"));
    color.setAlpha(qBound(0, int(92.0 * t), 92));
    return color;
}

bool networkApplyEndpointFlash(QTableWidget *table, int row, qint64 lastMs,
                               qint64 nowMs)
{
    const QColor color = networkEndpointFlashColor(lastMs, nowMs);
    if (!color.isValid())
        return false;
    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto *item = table->item(row, col))
            item->setBackground(QBrush(color));
    }
    return true;
}

void networkPrepareFullTable(QTableWidget *table)
{
    table->setTextElideMode(Qt::ElideNone);
    table->setWordWrap(false);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->verticalHeader()->setDefaultSectionSize(28);
}

QWidget *networkTabPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    return page;
}

} // namespace

void MainWindow::updateNetworkCounts(int relays, int nodes, int hosts)
{
    if (relays >= 0)
        m_networkRelayCount = relays;
    if (nodes >= 0)
        m_networkNodeCount = nodes;
    if (hosts >= 0)
        m_networkHostCount = hosts;

    // The tabs only exist once the Network section has been built; the rail
    // badge below is live from launch either way.
    if (m_networkTabs) {
        const auto label = [](const QString &name, int count) {
            return count > 0 ? QStringLiteral("%1 (%2)").arg(name).arg(count)
                             : name;
        };
        if (m_networkTabs->count() > kNetworkRelaysTab)
            m_networkTabs->setTabText(
                kNetworkRelaysTab,
                label(QStringLiteral("Relays"), m_networkRelayCount));
        if (m_networkTabs->count() > kNetworkNodesTab)
            m_networkTabs->setTabText(
                kNetworkNodesTab,
                label(QStringLiteral("Nodes"), m_networkNodeCount));
        if (m_networkTabs->count() > kNetworkHostsTab)
            m_networkTabs->setTabText(
                kNetworkHostsTab,
                label(QStringLiteral("Hosts"), m_networkHostCount));
    }

    // One badge for the whole mesh: relays + nodes + hosts, the sum of what the
    // three separate rail buttons used to badge on their own (adhoc #54).
    if (auto *railButton =
            dynamic_cast<ActivityRailButton *>(m_networkNavButton))
        railButton->setBadgeCount(m_networkRelayCount + m_networkNodeCount +
                                  m_networkHostCount);
}

QWidget *MainWindow::buildNetworkDiagnosticsSection()
{
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 24);
    outer->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("Network"));
    title->setObjectName("sectionTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);

    m_networkDiagnosticsStatus = new QLabel;
    m_networkDiagnosticsStatus->setObjectName("mutedLabel");
    header->addWidget(m_networkDiagnosticsStatus, 1);

    m_networkDiagnosticsRefreshButton = new QPushButton(QStringLiteral("Refresh"));
    m_networkDiagnosticsRefreshButton->setObjectName("repoAction");
    m_networkDiagnosticsRefreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_networkDiagnosticsRefreshButton, "sync", 14);
    connect(m_networkDiagnosticsRefreshButton, &QPushButton::clicked, this,
            [this] {
                refreshFirewallTables();
                refreshNetworkDiagnostics();
            });
    header->addWidget(m_networkDiagnosticsRefreshButton);
    outer->addLayout(header);

    auto *summary = new QLabel(QStringLiteral(
        "The relays, nodes and hosts this client talks to, plus live endpoint "
        "usage, websocket Durable Object details and the outbound request "
        "firewall in one place."));
    summary->setObjectName("mutedLabel");
    summary->setWordWrap(true);
    outer->addWidget(summary);

    auto *tabs = new QTabWidget;
    // Its own object name, styled alongside #settingsTabs: sharing that name
    // made this the tab widget a findChild<QTabWidget *>("settingsTabs") walked
    // into once the Network section had been built (Theme.h styles both).
    tabs->setObjectName(QStringLiteral("networkTabs"));
    tabs->setDocumentMode(true);
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outer->addWidget(tabs, 1);
    m_networkTabs = tabs;

    // The mesh itself comes first (adhoc #54): relays, then the nodes on them,
    // then the machines this client installs on. Each tab carries its own count;
    // the total is what the rail's Network badge shows. Their tab order has to
    // match kNetworkRelaysTab / kNetworkNodesTab / kNetworkHostsTab.
    tabs->addTab(buildRelaysSection(), QStringLiteral("Relays"));
    tabs->addTab(buildNodesSection(), QStringLiteral("Nodes"));
    tabs->addTab(buildHostsSection(), QStringLiteral("Hosts"));
    connect(tabs, &QTabWidget::currentChanged, this,
            [this](int index) { refreshNetworkTab(index); });

    auto *endpointsPage = networkTabPage();
    auto *endpointsLayout = qobject_cast<QVBoxLayout *>(endpointsPage->layout());
    m_networkEndpointsTable = new QTableWidget(0, 10);
    installColumnHeaderMenu(m_networkEndpointsTable);
    m_networkEndpointsTable->setObjectName("issueTable");
    m_networkEndpointsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Endpoint"), QStringLiteral("Kind"),
         QStringLiteral("Calls"), QStringLiteral("Status"),
         QStringLiteral("Sent"), QStringLiteral("Received"),
         QStringLiteral("Last"), QStringLiteral("Request data"),
         QStringLiteral("Response data"), QStringLiteral("Firewall")});
    m_networkEndpointsTable->verticalHeader()->setVisible(false);
    m_networkEndpointsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_networkEndpointsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_networkEndpointsTable->setShowGrid(false);
    m_networkEndpointsTable->setSortingEnabled(true);
    networkPrepareFullTable(m_networkEndpointsTable);
    for (int c = 0; c < m_networkEndpointsTable->columnCount(); ++c)
        m_networkEndpointsTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_networkEndpointsTable);
    connect(m_networkEndpointsTable, &QTableWidget::cellClicked, this,
            &MainWindow::showEndpointRequestDetails);
    connect(m_networkEndpointsTable->horizontalHeader(),
            &QHeaderView::sortIndicatorChanged, this,
            [this] { m_networkEndpointsUserSorted = true; });
    endpointsLayout->addWidget(m_networkEndpointsTable, 1);
    tabs->addTab(endpointsPage, QStringLiteral("Endpoints"));

    auto *socketsPage = networkTabPage();
    auto *socketsLayout = qobject_cast<QVBoxLayout *>(socketsPage->layout());
    m_networkDiagnosticsTable = new QTableWidget(0, 9);
    installColumnHeaderMenu(m_networkDiagnosticsTable);
    m_networkDiagnosticsTable->setObjectName("issueTable");
    m_networkDiagnosticsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Connection"), QStringLiteral("Durable object"),
         QStringLiteral("Endpoint"), QStringLiteral("State"),
         QStringLiteral("Sent"), QStringLiteral("Received"),
         QStringLiteral("Last sent"), QStringLiteral("Last received"),
         QStringLiteral("Data carried")});
    m_networkDiagnosticsTable->verticalHeader()->setVisible(false);
    m_networkDiagnosticsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_networkDiagnosticsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_networkDiagnosticsTable->setShowGrid(false);
    m_networkDiagnosticsTable->setSortingEnabled(true);
    networkPrepareFullTable(m_networkDiagnosticsTable);
    for (int c = 0; c < m_networkDiagnosticsTable->columnCount(); ++c)
        m_networkDiagnosticsTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_networkDiagnosticsTable);
    socketsLayout->addWidget(m_networkDiagnosticsTable, 1);
    tabs->addTab(socketsPage, QStringLiteral("WebSocket Durable Objects"));

    auto *whitelistPage = networkTabPage();
    auto *whitelistLayout = qobject_cast<QVBoxLayout *>(whitelistPage->layout());
    auto *firewallRow = new QHBoxLayout;
    firewallRow->setContentsMargins(0, 0, 0, 0);
    firewallRow->setSpacing(8);
    m_requestFirewallEnabledCheck = new QCheckBox(QStringLiteral("Whitelist only"));
    m_requestFirewallEnabledCheck->setCursor(Qt::PointingHandCursor);
    m_requestFirewallEnabledCheck->setChecked(
        QSettings().value(kRequestFirewallEnabledSetting, true).toBool());
    connect(m_requestFirewallEnabledCheck, &QCheckBox::toggled, this,
            [this](bool on) {
                QSettings().setValue(kRequestFirewallEnabledSetting, on);
                if (auto *manager = requestFirewallManager(m_networkAccess))
                    manager->setFirewallEnabled(on);
                logSystem(on ? QStringLiteral("Firewall: whitelist-only mode enabled.")
                             : QStringLiteral("Firewall: whitelist-only mode disabled."));
                refreshFirewallTables();
                refreshNetworkDiagnostics();
            });
    firewallRow->addWidget(m_requestFirewallEnabledCheck);

    m_requestFirewallStatus = new QLabel;
    m_requestFirewallStatus->setObjectName("mutedLabel");
    firewallRow->addWidget(m_requestFirewallStatus, 1);
    whitelistLayout->addLayout(firewallRow);

    auto *ruleRow = new QHBoxLayout;
    ruleRow->setContentsMargins(0, 0, 0, 0);
    ruleRow->setSpacing(8);
    m_requestFirewallRuleEdit = new QLineEdit;
    m_requestFirewallRuleEdit->setPlaceholderText(
        QStringLiteral("Add firewall rule: host, host:port, *.domain, https://host/path, scheme:https, *"));
    ruleRow->addWidget(m_requestFirewallRuleEdit, 1);
    auto *addRule = new QPushButton(QStringLiteral("Add rule"));
    addRule->setObjectName("repoAction");
    addRule->setCursor(Qt::PointingHandCursor);
    setOcticon(addRule, "plus", 14);
    connect(addRule, &QPushButton::clicked, this,
            &MainWindow::addFirewallRuleFromEdit);
    connect(m_requestFirewallRuleEdit, &QLineEdit::returnPressed, this,
            &MainWindow::addFirewallRuleFromEdit);
    ruleRow->addWidget(addRule);
    m_requestFirewallRemoveButton = new QPushButton(QStringLiteral("Remove rule"));
    m_requestFirewallRemoveButton->setObjectName("repoAction");
    m_requestFirewallRemoveButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_requestFirewallRemoveButton, "trash", 14);
    connect(m_requestFirewallRemoveButton, &QPushButton::clicked, this,
            &MainWindow::removeSelectedFirewallRules);
    ruleRow->addWidget(m_requestFirewallRemoveButton);
    whitelistLayout->addLayout(ruleRow);

    m_requestFirewallRulesTable = new QTableWidget(0, 2);
    installColumnHeaderMenu(m_requestFirewallRulesTable);
    m_requestFirewallRulesTable->setObjectName("issueTable");
    m_requestFirewallRulesTable->setHorizontalHeaderLabels(
        {QStringLiteral("Rule"), QStringLiteral("Stored as")});
    m_requestFirewallRulesTable->verticalHeader()->setVisible(false);
    m_requestFirewallRulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_requestFirewallRulesTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_requestFirewallRulesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestFirewallRulesTable->setShowGrid(false);
    networkPrepareFullTable(m_requestFirewallRulesTable);
    for (int c = 0; c < m_requestFirewallRulesTable->columnCount(); ++c)
        m_requestFirewallRulesTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_requestFirewallRulesTable);
    whitelistLayout->addWidget(m_requestFirewallRulesTable, 1);
    tabs->addTab(whitelistPage, QStringLiteral("Firewall Whitelist"));

    auto *decisionsPage = networkTabPage();
    auto *decisionsLayout = qobject_cast<QVBoxLayout *>(decisionsPage->layout());
    auto *decisionsRow = new QHBoxLayout;
    decisionsRow->setContentsMargins(0, 0, 0, 0);
    decisionsRow->setSpacing(8);
    decisionsRow->addStretch();
    auto *clearHistory = new QPushButton(QStringLiteral("Clear decisions"));
    clearHistory->setObjectName("repoAction");
    clearHistory->setCursor(Qt::PointingHandCursor);
    setOcticon(clearHistory, "trash", 14);
    connect(clearHistory, &QPushButton::clicked, this,
            &MainWindow::clearFirewallHistory);
    decisionsRow->addWidget(clearHistory);
    decisionsLayout->addLayout(decisionsRow);

    m_requestFirewallHistoryTable = new QTableWidget(0, 5);
    installColumnHeaderMenu(m_requestFirewallHistoryTable);
    m_requestFirewallHistoryTable->setObjectName("issueTable");
    m_requestFirewallHistoryTable->setHorizontalHeaderLabels(
        {QStringLiteral("Decision"), QStringLiteral("Method"),
         QStringLiteral("Destination"), QStringLiteral("Rule"),
         QStringLiteral("When")});
    m_requestFirewallHistoryTable->verticalHeader()->setVisible(false);
    m_requestFirewallHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_requestFirewallHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_requestFirewallHistoryTable->setShowGrid(false);
    m_requestFirewallHistoryTable->setSortingEnabled(true);
    networkPrepareFullTable(m_requestFirewallHistoryTable);
    for (int c = 0; c < m_requestFirewallHistoryTable->columnCount(); ++c)
        m_requestFirewallHistoryTable->horizontalHeader()->setSectionResizeMode(
            c, QHeaderView::ResizeToContents);
    makeColumnsResizable(m_requestFirewallHistoryTable);
    decisionsLayout->addWidget(m_requestFirewallHistoryTable, 1);
    tabs->addTab(decisionsPage, QStringLiteral("Recent Firewall Decisions"));

    refreshFirewallTables();
    refreshNetworkDiagnostics();
    return page;
}

void MainWindow::refreshNetworkDiagnostics()
{
    if (!m_networkDiagnosticsTable)
        return;
    // Rebuilding both tables re-shapes every cell's text and re-measures every
    // column/row (resizeColumnsToContents → harfbuzz), which the stall watchdog
    // clocked at ~600ms during startup while the section wasn't even on screen
    // (adhoc #33). Only pay that when the Network section is actually visible;
    // showSection() refreshes it on every open, so nothing goes stale.
    if (m_sectionStack &&
        m_sectionStack->currentIndex() != kNetworkDiagnosticsSectionIndex)
        return;

    QList<QJsonObject> rows;
    if (m_backend)
        rows += m_backend->networkDiagnostics();
    for (RepoHost *host : std::as_const(m_repoHosts)) {
        if (host)
            rows.append(host->networkDiagnostics());
    }
    QList<BackoffNetworkAccessManager::EndpointStats> endpointStats;
    if (auto *manager = requestFirewallManager(m_networkAccess))
        endpointStats = manager->endpointStats();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool endpointFadeActive = false;

    int connected = 0;
    qint64 txBytes = 0;
    qint64 rxBytes = 0;
    qint64 txFrames = 0;
    qint64 rxFrames = 0;
    for (const QJsonObject &row : std::as_const(rows)) {
        if (row.value(QStringLiteral("connected")).toBool())
            ++connected;
        txBytes += networkDiagInt(row, "txBytes");
        rxBytes += networkDiagInt(row, "rxBytes");
        txFrames += networkDiagInt(row, "txFrames");
        rxFrames += networkDiagInt(row, "rxFrames");
    }
    if (m_networkDiagnosticsStatus) {
        m_networkDiagnosticsStatus->setText(
            QStringLiteral("%1 endpoint(s) - %2/%3 websocket(s) connected - "
                           "%4 sent in %5 frame(s), %6 received in %7 frame(s)")
                .arg(endpointStats.size() + rows.size())
                .arg(connected)
                .arg(rows.size())
                .arg(networkDiagBytes(txBytes))
                .arg(txFrames)
                .arg(networkDiagBytes(rxBytes))
                .arg(rxFrames));
    }

    // The summary line above sits in the section header and is cheap, so it
    // stays live. The two tables below are the expensive part, and the Relays /
    // Nodes / Hosts tabs (adhoc #54) do not show them — same reasoning as the
    // section-visibility guard, one level down.
    if (m_networkTabs && m_networkTabs->currentIndex() <= kNetworkHostsTab)
        return;

    if (m_networkEndpointsTable) {
        TableRepaintGuard repaintGuard(m_networkEndpointsTable);
        const int sortColumn =
            m_networkEndpointsTable->horizontalHeader()->sortIndicatorSection();
        const Qt::SortOrder sortOrder =
            m_networkEndpointsTable->horizontalHeader()->sortIndicatorOrder();
        m_networkEndpointsTable->setSortingEnabled(false);
        m_networkEndpointsTable->setRowCount(0);

        for (const BackoffNetworkAccessManager::EndpointStats &stats :
             std::as_const(endpointStats)) {
            const int row = m_networkEndpointsTable->rowCount();
            m_networkEndpointsTable->insertRow(row);
            auto *endpoint = networkDiagItem(stats.endpoint, stats.endpoint);
            endpoint->setIcon(themedOcticon(QStringLiteral("link"),
                                            QColor(QStringLiteral("#8b949e")), 15));
            endpoint->setData(kNetworkEndpointKindRole, QStringLiteral("http"));
            endpoint->setData(kNetworkEndpointMethodRole, stats.method);
            endpoint->setData(kNetworkEndpointUrlRole, stats.endpoint);
            m_networkEndpointsTable->setItem(row, 0, endpoint);
            m_networkEndpointsTable->setItem(row, 1,
                                             networkDiagItem(stats.method));
            m_networkEndpointsTable->setItem(row, 2,
                                             networkDiagNumberItem(stats.calls));
            auto *status = networkDiagItem(networkEndpointStatus(stats),
                                           stats.lastError);
            if (stats.blocked > 0)
                status->setForeground(QColor(QStringLiteral("#f85149")));
            else if (stats.failures > 0)
                status->setForeground(QColor(QStringLiteral("#d29922")));
            else
                status->setForeground(QColor(QStringLiteral("#3fb950")));
            m_networkEndpointsTable->setItem(row, 3, status);
            m_networkEndpointsTable->setItem(
                row, 4, networkDiagItem(networkDiagBytes(stats.uploadBytes)));
            m_networkEndpointsTable->setItem(
                row, 5, networkDiagItem(networkDiagBytes(stats.downloadBytes)));
            auto *last = networkDiagItem(formatRepoDate(stats.lastCalledMs));
            last->setData(Qt::UserRole, static_cast<qlonglong>(stats.lastCalledMs));
            m_networkEndpointsTable->setItem(row, 6, last);
            m_networkEndpointsTable->setItem(
                row, 7,
                networkDiagItem(stats.lastRequestData, stats.lastFullUrl));
            m_networkEndpointsTable->setItem(
                row, 8,
                networkDiagItem(stats.lastResponseData, stats.lastResponseData));
            m_networkEndpointsTable->setItem(
                row, 9, networkDiagItem(networkEndpointFirewall(stats)));
            endpointFadeActive |= networkApplyEndpointFlash(
                m_networkEndpointsTable, row, stats.lastCalledMs, nowMs);
        }

        for (const QJsonObject &rowObject : std::as_const(rows)) {
            const int row = m_networkEndpointsTable->rowCount();
            m_networkEndpointsTable->insertRow(row);
            const QString endpointText =
                networkDiagText(rowObject, "endpoint", QStringLiteral("-"));
            const QString kind = networkDiagText(rowObject, "kind",
                                                 QStringLiteral("websocket"));
            auto *endpoint = networkDiagItem(endpointText, endpointText);
            endpoint->setIcon(themedOcticon(
                kind == QLatin1String("repo-host") ? QStringLiteral("repo")
                                                   : QStringLiteral("broadcast"),
                QColor(QStringLiteral("#8b949e")), 15));
            endpoint->setData(kNetworkEndpointKindRole, QStringLiteral("websocket"));
            m_networkEndpointsTable->setItem(row, 0, endpoint);
            m_networkEndpointsTable->setItem(
                row, 1,
                networkDiagItem(kind == QLatin1String("repo-host")
                                    ? QStringLiteral("WS repo")
                                    : QStringLiteral("WS mainnode")));
            const qint64 frameCalls = networkDiagInt(rowObject, "txFrames") +
                                      networkDiagInt(rowObject, "rxFrames") +
                                      networkDiagInt(rowObject, "txControlFrames") +
                                      networkDiagInt(rowObject, "rxControlFrames");
            m_networkEndpointsTable->setItem(row, 2,
                                             networkDiagNumberItem(frameCalls));
            auto *state = networkDiagItem(
                networkDiagText(rowObject, "state", QStringLiteral("Unknown")));
            if (rowObject.value(QStringLiteral("connected")).toBool())
                state->setForeground(QColor(QStringLiteral("#3fb950")));
            else
                state->setForeground(QColor(QStringLiteral("#8b949e")));
            m_networkEndpointsTable->setItem(row, 3, state);
            m_networkEndpointsTable->setItem(
                row, 4,
                networkDiagItem(networkDiagBytes(networkDiagInt(rowObject, "txBytes"))));
            m_networkEndpointsTable->setItem(
                row, 5,
                networkDiagItem(networkDiagBytes(networkDiagInt(rowObject, "rxBytes"))));
            const qint64 lastMs = qMax(networkDiagInt(rowObject, "lastTxMs"),
                                      networkDiagInt(rowObject, "lastRxMs"));
            auto *last = networkDiagItem(formatRepoDate(lastMs));
            last->setData(Qt::UserRole, static_cast<qlonglong>(lastMs));
            m_networkEndpointsTable->setItem(row, 6, last);
            m_networkEndpointsTable->setItem(row, 7,
                                             networkDiagItem(networkDiagLast(
                                                 rowObject, true)));
            m_networkEndpointsTable->setItem(row, 8,
                                             networkDiagItem(networkDiagLast(
                                                 rowObject, false)));
            m_networkEndpointsTable->setItem(row, 9,
                                             networkDiagItem(QStringLiteral("Allowed")));
            endpointFadeActive |= networkApplyEndpointFlash(
                m_networkEndpointsTable, row, lastMs, nowMs);
        }
        m_networkEndpointsTable->setSortingEnabled(true);
        if (m_networkEndpointsUserSorted)
            m_networkEndpointsTable->sortItems(sortColumn, sortOrder);
        else
            m_networkEndpointsTable->sortItems(6, Qt::DescendingOrder);
        m_networkEndpointsTable->resizeColumnsToContents();
        m_networkEndpointsTable->resizeRowsToContents();
    }
    if (endpointFadeActive && !m_networkEndpointFadeScheduled) {
        m_networkEndpointFadeScheduled = true;
        QTimer::singleShot(120, this, [this] {
            m_networkEndpointFadeScheduled = false;
            if (m_sectionStack &&
                m_sectionStack->currentIndex() == kNetworkDiagnosticsSectionIndex)
                refreshNetworkDiagnostics();
        });
    }

    TableRepaintGuard repaintGuard(m_networkDiagnosticsTable);
    m_networkDiagnosticsTable->setSortingEnabled(false);
    m_networkDiagnosticsTable->setRowCount(0);
    for (const QJsonObject &rowObject : std::as_const(rows)) {
        const int row = m_networkDiagnosticsTable->rowCount();
        m_networkDiagnosticsTable->insertRow(row);

        const QString data = networkDiagText(rowObject, "data");
        auto *connection = networkDiagItem(
            networkDiagText(rowObject, "connection", QStringLiteral("WebSocket")),
            data);
        const QString kind = networkDiagText(rowObject, "kind");
        connection->setIcon(themedOcticon(
            kind == QLatin1String("repo-host") ? QStringLiteral("repo")
                                               : QStringLiteral("broadcast"),
            QColor(QStringLiteral("#8b949e")), 15));
        m_networkDiagnosticsTable->setItem(row, 0, connection);

        m_networkDiagnosticsTable->setItem(
            row, 1, networkDiagItem(networkDiagText(rowObject, "durableObject",
                                                    QStringLiteral("-"))));
        m_networkDiagnosticsTable->setItem(
            row, 2, networkDiagItem(networkDiagText(rowObject, "endpoint",
                                                    QStringLiteral("-"))));

        const QString stateText = networkDiagText(rowObject, "state",
                                                  QStringLiteral("Unknown"));
        auto *state = networkDiagItem(
            stateText,
            QStringLiteral("Connected since %1")
                .arg(formatRepoDate(networkDiagInt(rowObject, "connectedAtMs"))));
        if (rowObject.value(QStringLiteral("connected")).toBool())
            state->setForeground(QColor(QStringLiteral("#3fb950")));
        else if (stateText == QLatin1String("Connecting"))
            state->setForeground(QColor(QStringLiteral("#d29922")));
        else
            state->setForeground(QColor(QStringLiteral("#8b949e")));
        m_networkDiagnosticsTable->setItem(row, 3, state);

        m_networkDiagnosticsTable->setItem(
            row, 4,
            networkDiagItem(networkDiagTraffic(rowObject, "txFrames", "txBytes",
                                               "txControlFrames")));
        m_networkDiagnosticsTable->setItem(
            row, 5,
            networkDiagItem(networkDiagTraffic(rowObject, "rxFrames", "rxBytes",
                                               "rxControlFrames")));
        m_networkDiagnosticsTable->setItem(
            row, 6, networkDiagItem(networkDiagLast(rowObject, true)));
        m_networkDiagnosticsTable->setItem(
            row, 7, networkDiagItem(networkDiagLast(rowObject, false)));
        m_networkDiagnosticsTable->setItem(row, 8, networkDiagItem(data, data));
    }
    m_networkDiagnosticsTable->setSortingEnabled(true);
    m_networkDiagnosticsTable->resizeColumnsToContents();
    m_networkDiagnosticsTable->resizeRowsToContents();
}

void MainWindow::showEndpointRequestDetails(int row, int column)
{
    Q_UNUSED(column);
    if (!m_networkEndpointsTable)
        return;

    QTableWidgetItem *endpointItem = m_networkEndpointsTable->item(row, 0);
    if (!endpointItem ||
        endpointItem->data(kNetworkEndpointKindRole).toString() !=
            QLatin1String("http"))
        return;

    const QString method =
        endpointItem->data(kNetworkEndpointMethodRole).toString();
    const QString endpoint =
        endpointItem->data(kNetworkEndpointUrlRole).toString();
    if (method.isEmpty() || endpoint.isEmpty())
        return;

    auto *manager = requestFirewallManager(m_networkAccess);
    if (!manager)
        return;
    const QList<BackoffNetworkAccessManager::RequestRecord> requests =
        manager->endpointRequests(method, endpoint);
    if (requests.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Endpoint requests"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *summary = new QLabel(
        QStringLiteral("%1 request(s) for %2 %3")
            .arg(requests.size())
            .arg(method, endpoint));
    summary->setObjectName("mutedLabel");
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto *table = new QTableWidget(0, 8, &dialog);
    installColumnHeaderMenu(table);
    table->setObjectName("issueTable");
    table->setHorizontalHeaderLabels(
        {QStringLiteral("When"), QStringLiteral("Method"),
         QStringLiteral("Status"), QStringLiteral("Sent"),
         QStringLiteral("Received"), QStringLiteral("Full URL"),
         QStringLiteral("Request data"), QStringLiteral("Response data")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    networkPrepareFullTable(table);
    table->setSortingEnabled(false);

    for (const BackoffNetworkAccessManager::RequestRecord &record :
         std::as_const(requests)) {
        const int detailRow = table->rowCount();
        table->insertRow(detailRow);

        const qint64 whenMs = qMax(record.finishedMs, record.startedMs);
        auto *when = networkDiagItem(formatRepoDate(whenMs));
        when->setData(Qt::UserRole, static_cast<qlonglong>(whenMs));
        table->setItem(detailRow, 0, when);
        table->setItem(detailRow, 1, networkDiagItem(record.method));

        auto *status = networkDiagItem(networkRequestStatus(record),
                                       record.error);
        if (record.blocked || !record.error.isEmpty())
            status->setForeground(QColor(QStringLiteral("#f85149")));
        else if (record.status >= 400)
            status->setForeground(QColor(QStringLiteral("#d29922")));
        else if (record.finishedMs <= 0)
            status->setForeground(QColor(QStringLiteral("#8b949e")));
        else
            status->setForeground(QColor(QStringLiteral("#3fb950")));
        table->setItem(detailRow, 2, status);

        auto *sent = networkDiagItem(networkDiagBytes(record.uploadBytes));
        sent->setData(Qt::UserRole, static_cast<qlonglong>(record.uploadBytes));
        table->setItem(detailRow, 3, sent);
        auto *received = networkDiagItem(networkDiagBytes(record.downloadBytes));
        received->setData(Qt::UserRole,
                          static_cast<qlonglong>(record.downloadBytes));
        table->setItem(detailRow, 4, received);
        table->setItem(detailRow, 5,
                       networkDiagItem(record.url, record.url));
        table->setItem(detailRow, 6,
                       networkDiagItem(record.requestData, record.requestData));
        table->setItem(detailRow, 7,
                       networkDiagItem(record.responseData,
                                       record.responseData));
    }

    table->setSortingEnabled(true);
    table->sortItems(0, Qt::DescendingOrder);
    table->resizeColumnsToContents();
    table->resizeRowsToContents();
    layout->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(1200, 720);
    dialog.exec();
}

void MainWindow::addFirewallRuleFromEdit()
{
    if (!m_requestFirewallRuleEdit)
        return;
    const QString raw = m_requestFirewallRuleEdit->text().trimmed();
    const QString rule = BackoffNetworkAccessManager::canonicalFirewallRule(raw);
    if (rule.isEmpty()) {
        flashMessage(QStringLiteral("Enter a valid firewall rule."), true);
        return;
    }
    auto *manager = requestFirewallManager(m_networkAccess);
    bool added = false;
    if (manager)
        added = manager->addFirewallRule(rule);
    QStringList rules = manager ? manager->firewallRules()
                                : QSettings().value(kRequestFirewallWhitelistSetting)
                                      .toStringList();
    if (!rules.contains(rule)) {
        rules.append(rule);
        rules.sort(Qt::CaseInsensitive);
        added = true;
    }
    QSettings().setValue(kRequestFirewallWhitelistSetting, rules);
    m_requestFirewallRuleEdit->clear();
    refreshFirewallTables();
    flashMessage(added ? QStringLiteral("Firewall rule added.")
                       : QStringLiteral("Firewall rule already exists."));
}

void MainWindow::removeSelectedFirewallRules()
{
    if (!m_requestFirewallRulesTable)
        return;
    QSet<QString> selectedRules;
    const auto ranges = m_requestFirewallRulesTable->selectedRanges();
    for (const QTableWidgetSelectionRange &range : ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            if (auto *item = m_requestFirewallRulesTable->item(row, 0))
                selectedRules.insert(item->data(Qt::UserRole).toString());
        }
    }
    if (selectedRules.isEmpty())
        return;
    auto *manager = requestFirewallManager(m_networkAccess);
    QStringList rules = manager ? manager->firewallRules()
                                : QSettings().value(kRequestFirewallWhitelistSetting)
                                      .toStringList();
    for (const QString &rule : std::as_const(selectedRules)) {
        if (manager)
            manager->removeFirewallRule(rule);
        rules.removeAll(rule);
    }
    QSettings().setValue(kRequestFirewallWhitelistSetting,
                         manager ? manager->firewallRules() : rules);
    refreshFirewallTables();
    flashMessage(QStringLiteral("Firewall rule removed."));
}

void MainWindow::clearFirewallHistory()
{
    m_requestFirewallHistory.clear();
    refreshFirewallTables();
}

bool MainWindow::promptFirewallRequest(const QString &method, const QUrl &url,
                                       QString *allowRuleOut)
{
    if (!url.isValid() || url.host().isEmpty())
        return true;

    const QString defaultRule = BackoffNetworkAccessManager::firewallRuleForUrl(url);
    if (allowRuleOut)
        *allowRuleOut = defaultRule;

    // Offscreen test runs must not block forever behind a modal prompt.
    if (QGuiApplication::platformName().contains(QStringLiteral("offscreen"),
                                                 Qt::CaseInsensitive))
        return false;

    QApplication::alert(this, 0);
    if (!isActiveWindow())
        postNotification(QStringLiteral("ForkMesh firewall"),
                         QStringLiteral("%1 wants to connect to %2")
                             .arg(method, requestFirewallDestination(url)),
                         true, QStringLiteral("dialog-warning"));

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("ForkMesh Firewall"));
    dialog.setModal(true);
    dialog.resize(640, 430);

    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(18, 16, 18, 16);
    outer->setSpacing(12);

    auto *heading = new QLabel(
        QStringLiteral("<b>ForkMesh</b> is connecting to <b>%1</b> on TCP port %2")
            .arg(url.host().toHtmlEscaped())
            .arg(requestFirewallPort(url) > 0
                     ? QString::number(requestFirewallPort(url))
                     : QStringLiteral("unknown")));
    heading->setWordWrap(true);
    outer->addWidget(heading);

    auto *details = new QFormLayout;
    details->setLabelAlignment(Qt::AlignLeft);
    details->addRow(QStringLiteral("Request"),
                    new QLabel(method + QStringLiteral(" ") +
                               url.toDisplayString(QUrl::RemoveUserInfo)));
    details->addRow(QStringLiteral("Executed from"),
                    new QLabel(QCoreApplication::applicationFilePath()));
    details->addRow(QStringLiteral("Destination host"),
                    new QLabel(url.host().toLower()));
    details->addRow(QStringLiteral("Destination port"),
                    new QLabel(requestFirewallPort(url) > 0
                                   ? QString::number(requestFirewallPort(url))
                                   : QStringLiteral("unknown")));
    details->addRow(QStringLiteral("User ID"), new QLabel(requestFirewallUser()));
    details->addRow(QStringLiteral("Process ID"),
                    new QLabel(QString::number(QCoreApplication::applicationPid())));
    outer->addLayout(details);

    auto *ruleCombo = new QComboBox;
    auto addRuleOption = [ruleCombo](const QString &label, const QString &rule) {
        const QString canonical =
            BackoffNetworkAccessManager::canonicalFirewallRule(rule);
        if (!canonical.isEmpty())
            ruleCombo->addItem(label, canonical);
    };
    addRuleOption(QStringLiteral("to this host"),
                  BackoffNetworkAccessManager::firewallRuleForUrl(url));
    addRuleOption(QStringLiteral("to this host and port"),
                  BackoffNetworkAccessManager::firewallRuleForUrl(url, true));
    addRuleOption(QStringLiteral("to this exact URL"),
                  BackoffNetworkAccessManager::firewallRuleForExactUrl(url));
    addRuleOption(QStringLiteral("all %1 requests").arg(url.scheme().toUpper()),
                  QStringLiteral("scheme:") + url.scheme().toLower());
    const QStringList hostParts = url.host().toLower().split(QLatin1Char('.'),
                                                            Qt::SkipEmptyParts);
    if (hostParts.size() > 2) {
        const QString suffix =
            hostParts.mid(hostParts.size() - 2).join(QLatin1Char('.'));
        addRuleOption(QStringLiteral("to *.%1").arg(suffix),
                      QStringLiteral("hostwild:") + suffix);
    }
    details->addRow(QStringLiteral("Allow rule"), ruleCombo);

    auto *buttons = new QDialogButtonBox;
    auto *deny = buttons->addButton(QStringLiteral("Deny once"),
                                    QDialogButtonBox::RejectRole);
    deny->setCursor(Qt::PointingHandCursor);
    setOcticon(deny, "x", 16);
    auto *allow = buttons->addButton(QStringLiteral("Allow"),
                                     QDialogButtonBox::AcceptRole);
    allow->setCursor(Qt::PointingHandCursor);
    setOcticon(allow, "check-circle", 16);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outer->addWidget(buttons);

    const bool allowed = dialog.exec() == QDialog::Accepted;
    if (allowed && allowRuleOut)
        *allowRuleOut = ruleCombo->currentData().toString();
    return allowed;
}

bool MainWindow::authorizeFirewallConnection(const QString &method, const QUrl &url)
{
    auto *manager = requestFirewallManager(m_networkAccess);
    if (!manager || manager->firewallAllowsUrl(url))
        return true;

    QString rule = BackoffNetworkAccessManager::firewallRuleForUrl(url);
    const bool allowed = promptFirewallRequest(method, url, &rule);
    const QString canonical =
        BackoffNetworkAccessManager::canonicalFirewallRule(rule);
    if (allowed && !canonical.isEmpty()) {
        manager->addFirewallRule(canonical);
        QSettings().setValue(kRequestFirewallWhitelistSetting,
                             manager->firewallRules());
    }
    recordFirewallRequest(method, url, canonical, allowed);
    return allowed;
}

void MainWindow::recordFirewallRequest(const QString &method, const QUrl &url,
                                       const QString &rule, bool allowed)
{
    FirewallHistoryEntry entry;
    entry.method = method;
    entry.url = url.toDisplayString(QUrl::RemoveUserInfo);
    entry.destination = requestFirewallDestination(url);
    entry.rule = rule;
    entry.allowed = allowed;
    entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
    m_requestFirewallHistory.prepend(entry);
    while (m_requestFirewallHistory.size() > kRequestFirewallHistoryLimit)
        m_requestFirewallHistory.removeLast();

    const QString message =
        QStringLiteral("Firewall %1 %2 %3")
            .arg(allowed ? QStringLiteral("allowed") : QStringLiteral("blocked"),
                 method, entry.destination);
    logSystem(message);
    if (!allowed)
        addNotification(QStringLiteral("Firewall blocked"), message, true);
    refreshFirewallTables();
    if (m_sectionStack &&
        m_sectionStack->currentIndex() == kNetworkDiagnosticsSectionIndex)
        refreshNetworkDiagnostics();
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
    // Restore only a password entered or migrated during this process. Host
    // credentials are deliberately never reloaded from persistent settings.
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    if (m_hostPassEdit)
        m_hostPassEdit->clear();
    for (int i = 0; i < hosts.size(); ++i) {
        const QJsonObject h = hosts.at(i).toObject();
        if (h.value("name").toString() == name) {
            if (m_hostPassEdit) {
                m_hostPassEdit->setText(m_hostSessionPasswords.value(
                    forkmesh::control::savedHostCredentialKey(
                        h.value(QStringLiteral("name")).toString(),
                        h.value(QStringLiteral("ip")).toString(),
                        h.value(QStringLiteral("user")).toString())));
            }
            break;
        }
    }
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(QString::fromUtf8(
            "Loaded \"%1\". Click Install ForkMesh to run the installer.").arg(name));
}

void MainWindow::viewHostLogsForSelection(int row)
{
    if (m_hostLogProcess &&
        m_hostLogProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host log stream is already running."));
        return;
    }
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    loadHostIntoForm(row, 0);
    const QString ip = m_hostIpEdit ? m_hostIpEdit->text().trimmed() : QString();
    const QString user = m_hostUserEdit ? m_hostUserEdit->text().trimmed() : QString();
    const QString node = m_hostNameEdit ? m_hostNameEdit->text().trimmed() : QString();
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Load a host row first, then click Logs."));
        return;
    }
    const QString pass =
        m_hostPassEdit ? m_hostPassEdit->text() : QString();
    runHostLogSession(ip, user, pass, node);
}

void MainWindow::runHostLogSession(const QString &ip, const QString &user,
                                  const QString &pass, const QString &node)
{
    if (m_hostLogProcess &&
        m_hostLogProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host log stream is already running."));
        return;
    }
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Enter host IP, SSH username and node name."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Host logs: %1").arg(node));
    dialog->setMinimumSize(860, 520);
    auto *layout = new QVBoxLayout(dialog);
    auto *status = new QLabel(
        QString::fromUtf8("Connecting to %1 as %2 \xE2\x80\xA6").arg(ip, user));
    status->setObjectName("mutedLabel");
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *logView = new QPlainTextEdit;
    logView->setObjectName("actionLog");
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->setMinimumHeight(420);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    logView->setFont(mono);
    logView->setPlaceholderText(
        QString::fromUtf8("Remote node output starts streaming here\xE2\x80\xA6"));
    layout->addWidget(logView, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (auto *closeBtn = buttons->button(QDialogButtonBox::Close))
        closeBtn->setDefault(true);
    layout->addWidget(buttons);

    auto appendLog = [logView](const QString &text) {
        if (!logView || text.isEmpty())
            return;
        QTextCursor cursor(logView->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text);
        logView->moveCursor(QTextCursor::End);
        logView->ensureCursorVisible();
    };

    auto *proc = new QProcess(this);
    m_hostLogProcess = proc;
    auto stopLogStream = [this, node, ip, user, proc]() {
        if (!proc || proc->state() == QProcess::NotRunning) {
            if (m_hostLogProcess == proc)
                m_hostLogProcess = nullptr;
            return;
        }
        proc->terminate();
        if (!proc->waitForFinished(250))
            proc->kill();
        proc->deleteLater();
        if (m_hostLogProcess == proc)
            m_hostLogProcess = nullptr;
        if (m_hostInstallStatus) {
            m_hostInstallStatus->setText(
                QString::fromUtf8("Stopped log stream for %1@%2 (%3).")
                    .arg(user, ip, node));
        }
    };

    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this, stopLogStream](int) {
        stopLogStream();
    });

    const auto shq = [](const QString &s) {
        QString out = s;
        out.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
        return QStringLiteral("'") + out + QStringLiteral("'");
    };
    const QString remoteLogCmd =
        QStringLiteral(
            "logPath=\"${XDG_DATA_HOME:-$HOME/.local/share}/forkmesh/node.log\"; "
            "if [ -f \"$logPath\" ]; then "
            "  tail -n 200 -f \"$logPath\"; "
            "elif [ -f \"$HOME/.forkmesh/node.log\" ]; then "
            "  tail -n 200 -f \"$HOME/.forkmesh/node.log\"; "
            "else "
            "  echo \"No ForkMesh node log file found at $logPath or ~/.forkmesh/node.log\"; "
            "  exit 1; "
            "fi");
    const QString remoteCmd = QStringLiteral("sh -lc ") + shq(remoteLogCmd);
    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            ip, user, pass, remoteCmd, &sshError,
            savedHostIdentityFile(node, ip, user));
    if (ssh.program.isEmpty()) {
        appendLog(QStringLiteral("[error] %1\n").arg(sshError));
        status->setText(sshError);
        proc->deleteLater();
        m_hostLogProcess = nullptr;
        dialog->deleteLater();
        return;
    }
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProcessEnvironment(ssh.environment);

    appendLog(QStringLiteral("Attempting SSH log stream to %1@%2\n")
                  .arg(user, ip));
    status->setText(QString::fromUtf8("Connected — streaming logs from host..."));
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(QStringLiteral("Opening host logs..."));

    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, proc, appendLog] {
                appendLog(QString::fromUtf8(proc->readAllStandardOutput()));
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, appendLog, status](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            appendLog(QString::fromUtf8(
                "\n[error] Could not start SSH. Install OpenSSH (and sshpass "
                "only when using password login) and try again.\n"));
            status->setText(
                QString::fromUtf8("Could not start SSH session to host."));
        }
    });
    connect(proc, &QProcess::finished, this,
            [this, ip, user, proc, status, node, appendLog](
                int code, QProcess::ExitStatus exitStatus) {
                if (m_hostLogProcess == proc)
                    m_hostLogProcess = nullptr;
                if (status)
                    status->setText(
                        QString::fromUtf8("Log stream ended for %1@%2.")
                            .arg(user, ip));
                if (code != 0 || exitStatus != QProcess::NormalExit) {
                    appendLog(QStringLiteral(
                        "\n[error] Stream ended with exit %1.\n").arg(code));
                } else {
                    appendLog(QString::fromUtf8("\n[info] Stream ended.\n"));
                }
                proc->deleteLater();
            });

    proc->start(ssh.program, ssh.arguments);
    dialog->exec();
}

void MainWindow::browseHostDiskUsageForSelection(int row)
{
    if (m_hostDiskProcess &&
        m_hostDiskProcess->state() != QProcess::NotRunning) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host size map is already loading."));
        return;
    }
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    // Read the row directly instead of loading it into the install form: this
    // is a read-only inspection and must not disturb whatever the form holds.
    const auto cellText = [this, row](int column) {
        const QTableWidgetItem *item = m_hostsTable->item(row, column);
        return item ? item->text().trimmed() : QString();
    };
    const QString node = cellText(0);
    const QString ip = cellText(1);
    const QString user = cellText(2);
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral(
                "Select a saved host row first, then click Size map."));
        return;
    }
    const QString pass = m_hostSessionPasswords.value(
        forkmesh::control::savedHostCredentialKey(node, ip, user));
    runHostDiskUsageBrowser(ip, user, pass, node);
}

void MainWindow::runHostDiskUsageBrowser(const QString &ip, const QString &user,
                                         const QString &pass,
                                         const QString &node)
{
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("Enter host IP, SSH username and node name."));
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName(QStringLiteral("hostDiskUsageDialog"));
    dialog->setWindowTitle(QStringLiteral("Size map: %1").arg(node));
    dialog->setMinimumSize(1060, 620);
    auto *layout = new QVBoxLayout(dialog);

    auto *hint = new QLabel(QString::fromUtf8(
        "ForkMesh measures one directory level at a time with <b>du</b> over "
        "the same authenticated SSH channel the installer uses \xE2\x80\x94 "
        "nothing is written on the host. Enter any absolute folder, "
        "double-click a folder to drill in, or use a mount map on the right."));
    hint->setObjectName("mutedLabel");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    auto *mainPane = new QWidget(dialog);
    auto *mainLayout = new QVBoxLayout(mainPane);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *nav = new QHBoxLayout;
    auto *upButton = new QPushButton(QStringLiteral("Up"));
    upButton->setObjectName(QStringLiteral("hostDiskUpButton"));
    upButton->setCursor(Qt::PointingHandCursor);
    setOcticon(upButton, "arrow-left", 12);
    nav->addWidget(upButton);
    auto *pathEdit = new QLineEdit(QStringLiteral("/"));
    pathEdit->setObjectName(QStringLiteral("hostDiskPathEdit"));
    pathEdit->setPlaceholderText(
        QStringLiteral("Absolute path on the host, e.g. /var/lib"));
    nav->addWidget(pathEdit, 1);
    auto *openButton = new QPushButton(QStringLiteral("Open"));
    openButton->setObjectName(QStringLiteral("hostDiskOpenButton"));
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setToolTip(
        QStringLiteral("Open the absolute folder entered to the left."));
    setOcticon(openButton, "file-directory", 12);
    nav->addWidget(openButton);
    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"));
    refreshButton->setObjectName(QStringLiteral("hostDiskRefreshButton"));
    refreshButton->setCursor(Qt::PointingHandCursor);
    setOcticon(refreshButton, "sync", 12);
    nav->addWidget(refreshButton);
    mainLayout->addLayout(nav);

    auto *totalLabel = new QLabel;
    totalLabel->setObjectName(QStringLiteral("hostDiskTotalLabel"));
    QFont totalFont = totalLabel->font();
    totalFont.setBold(true);
    totalLabel->setFont(totalFont);
    mainLayout->addWidget(totalLabel);

    auto *table = new QTableWidget(0, 4);
    table->setObjectName("issueTable");
    table->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Size"),
         QStringLiteral("Share of this folder"), QStringLiteral("Type")});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
        3, QHeaderView::ResizeToContents);
    mainLayout->addWidget(table, 1);

    auto *status = new QLabel(QString::fromUtf8(
        "Measuring the host root \xE2\x80\xA6"));
    status->setObjectName("mutedLabel");
    status->setWordWrap(true);
    mainLayout->addWidget(status);
    body->addWidget(mainPane, 1);

    auto *mountPane = new QWidget(dialog);
    mountPane->setObjectName(QStringLiteral("hostDiskMountPane"));
    mountPane->setMinimumWidth(235);
    mountPane->setMaximumWidth(285);
    auto *mountPaneLayout = new QVBoxLayout(mountPane);
    mountPaneLayout->setContentsMargins(10, 10, 10, 10);
    auto *mountHeading = new QLabel(QStringLiteral("Mount points"));
    QFont mountHeadingFont = mountHeading->font();
    mountHeadingFont.setBold(true);
    mountHeading->setFont(mountHeadingFont);
    mountPaneLayout->addWidget(mountHeading);
    auto *mountStatus = new QLabel(QStringLiteral("Loading mount maps..."));
    mountStatus->setObjectName(QStringLiteral("mutedLabel"));
    mountStatus->setWordWrap(true);
    mountPaneLayout->addWidget(mountStatus);
    auto *mountScroll = new QScrollArea(mountPane);
    mountScroll->setObjectName(QStringLiteral("hostDiskMountScroll"));
    mountScroll->setWidgetResizable(true);
    mountScroll->setFrameShape(QFrame::NoFrame);
    auto *mountCards = new QWidget(mountScroll);
    auto *mountCardsLayout = new QVBoxLayout(mountCards);
    mountCardsLayout->setContentsMargins(0, 0, 0, 0);
    mountCardsLayout->setSpacing(8);
    mountCardsLayout->addStretch();
    mountScroll->setWidget(mountCards);
    mountPaneLayout->addWidget(mountScroll, 1);
    body->addWidget(mountPane);
    layout->addLayout(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (auto *closeBtn = buttons->button(QDialogButtonBox::Close))
        closeBtn->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    // The password can change mid-dialog: a host that only accepts password
    // login rejects the key-only first attempt, and the scan then asks for one
    // and retries with it (see the finished handler below).
    const QString identityFile = savedHostIdentityFile(node, ip, user);
    const QString credentialKey =
        forkmesh::control::savedHostCredentialKey(node, ip, user);
    auto sessionPass = std::make_shared<QString>(pass);

    auto currentPath = std::make_shared<QString>(QStringLiteral("/"));
    auto loadPath = std::make_shared<std::function<void(const QString &)>>();
    *loadPath = [this, dialog, table, pathEdit, status, totalLabel, upButton,
                 openButton, refreshButton, currentPath, mountCardsLayout,
                 mountStatus, loadPath, ip, user, sessionPass, identityFile,
                 credentialKey, node](const QString &requested) {
        if (m_hostDiskProcess &&
            m_hostDiskProcess->state() != QProcess::NotRunning) {
            status->setText(QStringLiteral(
                "Still measuring the previous folder on this host."));
            return;
        }
        const QString path =
            forkmesh::control::normalizeRemoteDiskPath(requested);
        QString commandError;
        const QString diskCommand =
            forkmesh::control::buildHostDiskUsageCommand(path, &commandError);
        if (diskCommand.isEmpty()) {
            status->setText(commandError);
            pathEdit->setText(*currentPath);
            return;
        }
        const QString remoteCommand =
            forkmesh::control::buildHostMountUsageCommand() +
            QLatin1Char('\n') + diskCommand;
        QString sshError;
        const forkmesh::control::HostSshCommand ssh =
            forkmesh::control::buildHostSshCommand(
                ip, user, *sessionPass, remoteCommand, &sshError,
                identityFile);
        if (ssh.program.isEmpty()) {
            status->setText(sshError);
            return;
        }

        *currentPath = path;
        pathEdit->setText(path);
        table->setRowCount(0);
        totalLabel->setText(QString());
        status->setText(
            QString::fromUtf8("Measuring %1 on %2@%3 \xE2\x80\xA6 a large "
                              "directory tree can take a minute.")
                .arg(path, user, ip));

        const QList<QWidget *> navWidgets{upButton, openButton, refreshButton,
                                          pathEdit};
        for (QWidget *w : navWidgets)
            w->setEnabled(false);

        auto *proc = new QProcess(dialog);
        m_hostDiskProcess = proc;
        auto output = std::make_shared<QByteArray>();
        proc->setProcessChannelMode(QProcess::MergedChannels);
        proc->setProcessEnvironment(ssh.environment);
        connect(proc, &QProcess::readyReadStandardOutput, dialog,
                [proc, output] { output->append(proc->readAllStandardOutput()); });
        connect(proc, &QProcess::finished, dialog,
                [this, dialog, proc, output, table, status, totalLabel,
                 navWidgets, path, ip, user, mountCardsLayout, mountStatus,
                 loadPath, sessionPass, identityFile, credentialKey](
                    int code, QProcess::ExitStatus exitStatus) {
                    if (m_hostDiskProcess == proc)
                        m_hostDiskProcess = nullptr;
                    for (QWidget *w : navWidgets)
                        w->setEnabled(true);
                    while (QLayoutItem *item = mountCardsLayout->takeAt(0)) {
                        if (QWidget *widget = item->widget())
                            widget->deleteLater();
                        delete item;
                    }
                    const forkmesh::control::HostMountUsageList mounts =
                        forkmesh::control::parseHostMountUsage(*output);
                    if (!mounts.error.isEmpty()) {
                        mountStatus->setText(mounts.error);
                    } else if (!mounts.complete || mounts.mounts.isEmpty()) {
                        mountStatus->setText(QStringLiteral(
                            "Mount details were not reported by this host."));
                    } else {
                        mountStatus->setText(QString::fromUtf8(
                            "%1 filesystem%2 \xE2\x80\x94 click a mini map to "
                            "open any mount.")
                                                 .arg(mounts.mounts.size())
                                                 .arg(mounts.mounts.size() == 1
                                                          ? QString()
                                                          : QStringLiteral("s")));
                        QString activeMountPath;
                        for (const forkmesh::control::HostMountUsage &mount :
                             mounts.mounts) {
                            const bool containsPath =
                                path == mount.path ||
                                (mount.path == QStringLiteral("/")
                                     ? path.startsWith(QLatin1Char('/'))
                                     : path.startsWith(mount.path +
                                                       QLatin1Char('/')));
                            if (containsPath &&
                                mount.path.size() > activeMountPath.size()) {
                                activeMountPath = mount.path;
                            }
                        }
                        for (const forkmesh::control::HostMountUsage &mount :
                             mounts.mounts) {
                            const double usedShare =
                                mount.totalBytes > 0
                                    ? qBound(
                                          0.0,
                                          static_cast<double>(mount.usedBytes) /
                                              static_cast<double>(
                                                  mount.totalBytes),
                                          1.0)
                                    : 0.0;
                            const int filled =
                                qBound(0,
                                       static_cast<int>(
                                           std::lround(usedShare * 12.0)),
                                       12);
                            const QString miniMap =
                                QString(filled, QChar(0x2588)) +
                                QString(12 - filled, QChar(0x2591));
                            auto *mountButton = new QPushButton(
                                QStringLiteral("%1\n%2  %3%\n%4 of %5")
                                    .arg(mount.path, miniMap)
                                    .arg(usedShare * 100.0, 0, 'f', 0)
                                    .arg(forkmesh::control::formatDiskSize(
                                             mount.usedBytes),
                                         forkmesh::control::formatDiskSize(
                                             mount.totalBytes)));
                            mountButton->setObjectName(
                                QStringLiteral("hostDiskMountButton"));
                            mountButton->setProperty("mountPath", mount.path);
                            mountButton->setCursor(Qt::PointingHandCursor);
                            mountButton->setCheckable(true);
                            mountButton->setChecked(
                                mount.path == activeMountPath);
                            mountButton->setToolTip(
                                QString::fromUtf8(
                                    "Open %1 \xE2\x80\x94 %2 available")
                                    .arg(mount.path,
                                         forkmesh::control::formatDiskSize(
                                             mount.availableBytes)));
                            connect(mountButton, &QPushButton::clicked,
                                    mountButton,
                                    [loadPath, mount] {
                                        (*loadPath)(mount.path);
                                    });
                            mountCardsLayout->addWidget(mountButton);
                        }
                    }
                    mountCardsLayout->addStretch();
                    const forkmesh::control::HostDiskUsage usage =
                        forkmesh::control::parseHostDiskUsage(*output, path);
                    proc->deleteLater();
                    if (!usage.error.isEmpty()) {
                        status->setText(usage.error);
                        return;
                    }
                    if (!usage.complete) {
                        const QString tail = forkmesh::control::redactProcessOutput(
                            QString::fromUtf8(output->right(2048)));
                        QString message =
                            QString::fromUtf8(
                                "Could not read the size map for %1 (exit %2).")
                                .arg(path)
                                .arg(exitStatus == QProcess::NormalExit ? code
                                                                        : 255);
                        const QString sshHint =
                            forkmesh::control::sshConnectionFailureHint(
                                exitStatus == QProcess::NormalExit ? code : 255,
                                tail, ip);
                        if (!sshHint.isEmpty())
                            message += QLatin1Char(' ') + sshHint;
                        // Without a password ssh runs BatchMode/publickey-only,
                        // so a password-login host can never finish this scan:
                        // ask for the one credential that would, then retry the
                        // same folder. Hosts pinned to a ForkMesh-managed key
                        // never fall back to a password, so they are left alone.
                        const int sshExit =
                            exitStatus == QProcess::NormalExit ? code : 255;
                        if (identityFile.isEmpty() &&
                            forkmesh::control::sshFailureNeedsPassword(sshExit,
                                                                       tail)) {
                            status->setText(
                                message +
                                QString::fromUtf8(
                                    " Asking for this host's SSH password "
                                    "\xE2\x80\xA6"));
                            // Prompting has to leave this finished handler
                            // first: a modal dialog run inside a QProcess
                            // signal would pump the event loop under it.
                            QTimer::singleShot(
                                0, dialog,
                                [this, guard = QPointer<QDialog>(dialog),
                                 status, loadPath, sessionPass, credentialKey,
                                 path, ip, user] {
                                    QDialog *dialog = guard.data();
                                    if (!dialog)
                                        return;
                                    bool accepted = false;
                                    const QString entered =
                                        QInputDialog::getText(
                                            dialog,
                                            QStringLiteral(
                                                "SSH password needed"),
                                            QString::fromUtf8(
                                                "%1@%2 rejected the login "
                                                "ForkMesh tried. Enter that "
                                                "host's SSH password to "
                                                "measure its disk \xE2\x80\x94 "
                                                "it is kept in memory for this "
                                                "session only and is never "
                                                "written to settings.")
                                                .arg(user, ip),
                                            QLineEdit::Password, *sessionPass,
                                            &accepted);
                                    // getText ran a nested event loop, so the
                                    // size map (and its status label) may be
                                    // gone by the time it returns.
                                    if (!guard)
                                        return;
                                    if (!accepted || entered.isEmpty()) {
                                        status->setText(QString::fromUtf8(
                                            "The size map needs an SSH "
                                            "password (or a working key) for "
                                            "%1@%2. Press Refresh to try "
                                            "again.")
                                                            .arg(user, ip));
                                        return;
                                    }
                                    *sessionPass = entered;
                                    // Remember it for the rest of this session
                                    // so drilling into folders — and every
                                    // other host action — stops re-asking.
                                    m_hostSessionPasswords.insert(credentialKey,
                                                                  entered);
                                    (*loadPath)(path);
                                });
                            return;
                        }
                        status->setText(message);
                        return;
                    }

                    totalLabel->setText(
                        QString::fromUtf8("%1 \xE2\x80\x94 %2 total, %3 entries")
                            .arg(path,
                                 forkmesh::control::formatDiskSize(
                                     usage.totalBytes))
                            .arg(usage.entries.size()));
                    table->setRowCount(usage.entries.size());
                    for (int i = 0; i < usage.entries.size(); ++i) {
                        const forkmesh::control::HostDiskEntry &entry =
                            usage.entries.at(i);
                        auto *nameItem = new QTableWidgetItem(entry.name);
                        nameItem->setIcon(themedOcticon(
                            entry.directory ? QStringLiteral("file-directory")
                                            : QStringLiteral("file"),
                            QColor("#8b949e"), 13));
                        nameItem->setToolTip(entry.path);
                        nameItem->setData(Qt::UserRole, entry.path);
                        nameItem->setData(Qt::UserRole + 1, entry.directory);
                        table->setItem(i, 0, nameItem);
                        table->setItem(
                            i, 1,
                            new QTableWidgetItem(
                                forkmesh::control::formatDiskSize(entry.bytes)));
                        // A share bar drawn in text: cell widgets would be one
                        // extra widget per row on directories that hold
                        // thousands of entries.
                        const double share =
                            usage.totalBytes > 0
                                ? static_cast<double>(entry.bytes) /
                                      static_cast<double>(usage.totalBytes)
                                : 0.0;
                        const int filled = qBound(
                            0, static_cast<int>(std::lround(share * 20.0)), 20);
                        table->setItem(
                            i, 2,
                            new QTableWidgetItem(
                                QStringLiteral("%1 %2%")
                                    .arg(QString(filled, QChar(0x2588)) +
                                         QString(20 - filled, QChar(0x2591)))
                                    .arg(share * 100.0, 0, 'f', 1)));
                        table->setItem(
                            i, 3,
                            new QTableWidgetItem(entry.directory
                                                     ? QStringLiteral("Folder")
                                                     : QStringLiteral("File")));
                    }
                    status->setText(
                        usage.entries.isEmpty()
                            ? QString::fromUtf8(
                                  "%1 holds nothing this SSH user can read.")
                                  .arg(path)
                            : QString::fromUtf8(
                                  "Largest first. Double-click a folder to "
                                  "drill in, or Up to go back."));
                });
        connect(proc, &QProcess::errorOccurred, dialog,
                [this, proc, status, navWidgets](QProcess::ProcessError e) {
                    if (e != QProcess::FailedToStart)
                        return;
                    if (m_hostDiskProcess == proc)
                        m_hostDiskProcess = nullptr;
                    for (QWidget *w : navWidgets)
                        w->setEnabled(true);
                    status->setText(QString::fromUtf8(
                        "Could not start SSH. Install OpenSSH (and sshpass only "
                        "when using password login) and try again."));
                });
        proc->start(ssh.program, ssh.arguments);
    };

    connect(upButton, &QPushButton::clicked, dialog, [currentPath, loadPath] {
        (*loadPath)(*currentPath + QStringLiteral("/.."));
    });
    connect(refreshButton, &QPushButton::clicked, dialog,
            [currentPath, loadPath] { (*loadPath)(*currentPath); });
    connect(openButton, &QPushButton::clicked, dialog,
            [pathEdit, loadPath] { (*loadPath)(pathEdit->text()); });
    connect(pathEdit, &QLineEdit::returnPressed, dialog,
            [pathEdit, loadPath] { (*loadPath)(pathEdit->text()); });
    connect(table, &QTableWidget::cellDoubleClicked, dialog,
            [table, status, loadPath](int row, int) {
                const QTableWidgetItem *item = table->item(row, 0);
                if (!item)
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                if (!item->data(Qt::UserRole + 1).toBool()) {
                    status->setText(
                        QStringLiteral("%1 is a file, not a folder.").arg(path));
                    return;
                }
                (*loadPath)(path);
            });

    // A running measurement outlives its dialog otherwise: `du` over a big
    // tree keeps the SSH session open long after the window is gone.
    connect(dialog, &QDialog::finished, this, [this](int) {
        if (!m_hostDiskProcess)
            return;
        if (m_hostDiskProcess->state() != QProcess::NotRunning) {
            m_hostDiskProcess->terminate();
            if (!m_hostDiskProcess->waitForFinished(250))
                m_hostDiskProcess->kill();
        }
        m_hostDiskProcess = nullptr;
    });

    if (m_hostInstallStatus) {
        m_hostInstallStatus->setText(
            QString::fromUtf8("Opening the size map for %1@%2 (%3)...")
                .arg(user, ip, node));
    }
    (*loadPath)(QStringLiteral("/"));
    dialog->exec();
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
                              const QString &user, const QString &pass, const QString &status,
                              const QString &identityFile,
                              const QJsonObject &metadata)
{
    QSettings settings;
    QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    // Replace any existing row for the same node name, else append.
    QJsonObject entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("ip"), ip);
    entry.insert(QStringLiteral("user"), user);
    entry.insert(QStringLiteral("status"), status);
    if (!identityFile.trimmed().isEmpty())
        entry.insert(QStringLiteral("identityFile"), identityFile.trimmed());
    bool replaced = false;
    for (int i = 0; i < hosts.size(); ++i) {
        if (hosts.at(i).toObject().value("name").toString() == name) {
            const QJsonObject old = hosts.at(i).toObject();
            // A saved managed-key path survives status updates that do not
            // explicitly change it (every install/uninstall re-remember).
            if (!entry.contains(QStringLiteral("identityFile")) &&
                old.contains(QStringLiteral("identityFile"))) {
                entry.insert(QStringLiteral("identityFile"),
                             old.value(QStringLiteral("identityFile")));
            }
            for (const QString &field : {
                     QStringLiteral("provider"),
                     QStringLiteral("planType"),
                     QStringLiteral("displayName"),
                     QStringLiteral("region"),
                     QStringLiteral("monthlyCost"),
                     QStringLiteral("ramMb"),
                     QStringLiteral("diskGb"),
                     QStringLiteral("instanceId")}) {
                if (old.contains(field))
                    entry.insert(field, old.value(field));
            }
            for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it)
                entry.insert(it.key(), it.value());
            const QString oldCredentialKey =
                forkmesh::control::savedHostCredentialKey(
                    old.value(QStringLiteral("name")).toString(),
                    old.value(QStringLiteral("ip")).toString(),
                    old.value(QStringLiteral("user")).toString());
            const QString newCredentialKey =
                forkmesh::control::savedHostCredentialKey(name, ip, user);
            if (oldCredentialKey != newCredentialKey) {
                QString oldPassword =
                    m_hostSessionPasswords.take(oldCredentialKey);
                oldPassword.fill(QChar::Null);
            }
            hosts.replace(i, entry);
            replaced = true;
            break;
        }
    }
    if (!replaced)
        hosts.append(entry);
    const QString credentialKey =
        forkmesh::control::savedHostCredentialKey(name, ip, user);
    if (pass.isEmpty()) {
        QString oldPassword = m_hostSessionPasswords.take(credentialKey);
        oldPassword.fill(QChar::Null);
    } else {
        QString oldPassword = m_hostSessionPasswords.take(credentialKey);
        oldPassword.fill(QChar::Null);
        m_hostSessionPasswords.insert(credentialKey, pass);
    }
    forkmesh::control::saveSavedHosts(settings, kHostsSetting, hosts);
    refreshHostsTable();
}

void MainWindow::forgetHostAtRow(int row)
{
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    QSettings settings;
    QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    if (row >= hosts.size())
        return;
    const QJsonObject removed = hosts.at(row).toObject();
    hosts.removeAt(row);
    const QString credentialKey = forkmesh::control::savedHostCredentialKey(
        removed.value(QStringLiteral("name")).toString(),
        removed.value(QStringLiteral("ip")).toString(),
        removed.value(QStringLiteral("user")).toString());
    QString oldPassword = m_hostSessionPasswords.take(credentialKey);
    oldPassword.fill(QChar::Null);
    forkmesh::control::saveSavedHosts(settings, kHostsSetting, hosts);
    refreshHostsTable();
    if (m_hostInstallStatus) {
        m_hostInstallStatus->setText(QString::fromUtf8(
            "Removed \"%1\" from the saved hosts list.")
                .arg(removed.value(QStringLiteral("name")).toString()));
    }
}

void MainWindow::destroyVultrHostAtRow(int row)
{
    if (!m_hostsTable || row < 0 || row >= m_hostsTable->rowCount())
        return;
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    if (row >= hosts.size())
        return;
    const QJsonObject host = hosts.at(row).toObject();
    const QString name = host.value(QStringLiteral("name")).toString();
    const QString ip = host.value(QStringLiteral("ip")).toString();
    const auto setStatus = [this](const QString &text) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(text);
    };

    // Same key resolution as the create flow: the field first, then a
    // device-local Actions variable. The key stays in memory for this call.
    QString apiKey =
        m_vultrApiKeyEdit ? m_vultrApiKeyEdit->text().trimmed() : QString();
    if (apiKey.isEmpty()) {
        apiKey = forkmesh::control::vultrApiKeyFromVariables(
            ActionStore::variables());
    }
    if (apiKey.isEmpty()) {
        setStatus(QStringLiteral(
            "Enter your Vultr API key above (or store it as a VULTR_API_KEY "
            "variable) to destroy a server."));
        return;
    }

    const auto reply = QMessageBox::question(
        this, QStringLiteral("Destroy server"),
        QString::fromUtf8(
            "This DELETES the Vultr server behind \"%1\" (%2). The instance and "
            "everything on it are gone permanently, billing stops, and the host "
            "is removed from this list. This cannot be undone. Continue?")
            .arg(name, ip),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    const QString recorded = forkmesh::control::savedHostVultrInstanceId(host);
    if (!recorded.isEmpty()) {
        sendVultrInstanceDestroy(apiKey, recorded, name);
        return;
    }
    // Hosts saved before the instance id was recorded (or added by hand) still
    // have an address, so ask Vultr which instance that is. An ambiguous or
    // missing match destroys nothing.
    setStatus(QString::fromUtf8("Looking up \"%1\" on Vultr\xE2\x80\xA6").arg(name));
    vultrApiCall(
        apiKey, QStringLiteral("/v2/instances?per_page=500"),
        QByteArrayLiteral("GET"), {},
        [this, apiKey, name, ip, setStatus](QJsonObject result, QString error) {
            if (!error.isEmpty()) {
                setStatus(QString::fromUtf8("Could not destroy \"%1\": %2")
                              .arg(name, error));
                return;
            }
            const QString instanceId =
                forkmesh::control::vultrInstanceIdForAddress(
                    result.value(QStringLiteral("instances")).toArray(), ip);
            if (instanceId.isEmpty()) {
                setStatus(QString::fromUtf8(
                    "No single Vultr instance matches \"%1\" (%2), so nothing "
                    "was destroyed. Delete it from the Vultr panel instead.")
                              .arg(name, ip));
                return;
            }
            sendVultrInstanceDestroy(apiKey, instanceId, name);
        });
}

void MainWindow::sendVultrInstanceDestroy(const QString &apiKey,
                                          const QString &instanceId,
                                          const QString &name)
{
    const auto setStatus = [this](const QString &text) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(text);
    };
    const QString invalid =
        forkmesh::control::validateVultrDestroyRequest(apiKey, instanceId);
    if (!invalid.isEmpty()) {
        setStatus(invalid);
        return;
    }
    setStatus(QString::fromUtf8("Destroying \"%1\" on Vultr\xE2\x80\xA6").arg(name));
    appendHostInstallLog(
        QString::fromUtf8("Destroying Vultr instance %1 (\"%2\")\xE2\x80\xA6\n")
            .arg(instanceId, name));
    vultrApiCall(
        apiKey, QStringLiteral("/v2/instances/") + instanceId,
        QByteArrayLiteral("DELETE"), {},
        [this, name, instanceId, setStatus](QJsonObject, QString error) {
            if (!error.isEmpty()) {
                setStatus(QString::fromUtf8("Could not destroy \"%1\": %2")
                              .arg(name, error));
                appendHostInstallLog(
                    QString::fromUtf8("Vultr destroy failed: %1\n").arg(error));
                return;
            }
            appendHostInstallLog(QString::fromUtf8(
                "Vultr instance %1 destroyed.\n").arg(instanceId));
            // Only now does the saved row become meaningless. Re-resolve it by
            // name: the table may have been rebuilt while the call was in
            // flight, so the row index this started from can be stale.
            QSettings settings;
            const QJsonArray hosts = forkmesh::control::loadSavedHosts(
                settings, kHostsSetting, &m_hostSessionPasswords);
            for (int i = 0; i < hosts.size(); ++i) {
                if (hosts.at(i).toObject().value(QStringLiteral("name"))
                        .toString() == name) {
                    forgetHostAtRow(i);
                    break;
                }
            }
            setStatus(QString::fromUtf8(
                "Destroyed \"%1\" on Vultr and removed it from this list.")
                          .arg(name));
        });
}

QString MainWindow::savedHostIdentityFile(const QString &name, const QString &ip,
                                          const QString &user) const
{
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, nullptr);
    for (const QJsonValue &value : hosts) {
        const QJsonObject host = value.toObject();
        if (host.value(QStringLiteral("name")).toString() != name ||
            host.value(QStringLiteral("ip")).toString() != ip ||
            host.value(QStringLiteral("user")).toString() != user) {
            continue;
        }
        const QString identity =
            host.value(QStringLiteral("identityFile")).toString().trimmed();
        // Return the configured path even when the file has gone missing.
        // buildHostSshCommand() owns validation and will then fail closed with
        // "The managed SSH key for this host is missing." Treating the path as
        // absent here would silently fall back to a session password.
        return identity;
    }
    return {};
}

// --- One-click Vultr mirror provisioning (adhoc #315) -----------------------
//
// createVultrMirrorFromForm drives an async chain over the Vultr v2 API:
// managed keypair → SSH-key registration → cheapest US plan → newest Debian →
// instance create → boot poll → the normal runHostInstall handoff, which
// installs ForkMesh over SSH and auto-links the fresh node to this account so
// it starts mirroring and syncing on its own. Every step streams into the
// shared Live output pane. The API key is captured by value through the chain
// and lives only in these closures and the Authorization headers.

void MainWindow::finishVultrProvision(bool ok, const QString &message)
{
    m_vultrProvisionActive = false;
    if (m_vultrCreateButton)
        m_vultrCreateButton->setEnabled(true);
    if (m_vultrStatus)
        m_vultrStatus->setText(
            (ok ? QString::fromUtf8("\xE2\x9C\x94 ")
                : QString::fromUtf8("\xE2\x9C\x98 ")) + message);
    if (!message.isEmpty())
        appendHostInstallLog(
            (ok ? QString::fromUtf8("\n\xE2\x9C\x94 ")
                : QString::fromUtf8("\n\xE2\x9C\x98 ")) +
            message + QStringLiteral("\n"));
}

void MainWindow::waitForVultrMirrorPublication(
    const QString &node, const QString &successMessage, int attempt)
{
    constexpr int kMaxPublicationPolls = 30; // five minutes at 10 seconds
    if (!m_vultrProvisionActive)
        return;
    if (m_vultrStatus) {
        m_vultrStatus->setText(QString::fromUtf8(
            "ForkMesh is running on %1 \xE2\x80\x94 waiting for its signed "
            "Mirror nodes / World catalog record (%2/%3)\xE2\x80\xA6")
            .arg(node)
            .arg(attempt + 1)
            .arg(kMaxPublicationPolls));
    }

    QUrl url = catalogApiUrl();
    url.setPath(QStringLiteral("/api/repo/forkmesh/forkmesh/mirrors"));
    url.setQuery(QString());
    QNetworkRequest request(url);
    request.setRawHeader(QByteArrayLiteral("accept"),
                         QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, node, successMessage, attempt] {
        const QJsonObject payload =
            QJsonDocument::fromJson(reply->readAll()).object();
        const bool requestOk =
            reply->error() == QNetworkReply::NoError &&
            payload.value(QStringLiteral("ok")).toBool();
        reply->deleteLater();

        bool published = false;
        if (requestOk) {
            for (const QJsonValue &value :
                 payload.value(QStringLiteral("mirrors")).toArray()) {
                const QJsonObject mirror = value.toObject();
                QString candidate =
                    mirror.value(QStringLiteral("node")).toString().trimmed();
                if (candidate.isEmpty())
                    candidate =
                        mirror.value(QStringLiteral("owner")).toString().trimmed();
                if (candidate.compare(node, Qt::CaseInsensitive) == 0 &&
                    mirror.value(QStringLiteral("integrity"))
                            .toString()
                            .compare(QStringLiteral("ok"),
                                     Qt::CaseInsensitive) == 0 &&
                    mirror.value(QStringLiteral("lastSync")).toVariant()
                            .toLongLong() > 0) {
                    published = true;
                    break;
                }
            }
        }
        if (published) {
            appendHostInstallLog(QString::fromUtf8(
                "\n\xE2\x9C\x94 Verified %1 in the public Mirror nodes / World "
                "catalog.\n").arg(node));
            finishVultrProvision(true, successMessage);
            return;
        }
        if (attempt + 1 >= kMaxPublicationPolls) {
            finishVultrProvision(false, QString::fromUtf8(
                "ForkMesh is installed on %1, but its signed repository "
                "catalog did not appear within five minutes. The host remains "
                "saved and will keep retrying; check its Logs and account link "
                "before treating the mirror as ready.").arg(node));
            return;
        }
        QTimer::singleShot(
            10000, this,
            [this, node, successMessage, attempt] {
                waitForVultrMirrorPublication(
                    node, successMessage, attempt + 1);
            });
    });
}

void MainWindow::vultrApiCall(const QString &apiKey, const QString &path,
                              const QByteArray &method, const QJsonObject &body,
                              std::function<void(QJsonObject, QString)> onDone)
{
    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.vultr.com") + path));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("POST")) {
        reply = m_networkAccess->post(
            request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    } else if (method == QByteArrayLiteral("GET") || method.isEmpty()) {
        reply = m_networkAccess->get(request);
    } else {
        // DELETE and friends: Qt has no typed overload, and a successful
        // DELETE /v2/instances/{id} answers 204 with no body at all.
        reply = m_networkAccess->sendCustomRequest(
            request, method,
            body.isEmpty()
                ? QByteArray()
                : QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, apiKey, onDone] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                .toInt();
        const QJsonObject object =
            QJsonDocument::fromJson(reply->read(1024 * 1024)).object();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300) {
            QString detail =
                object.value(QStringLiteral("error")).toString();
            if (detail.isEmpty())
                detail = reply->errorString();
            onDone({}, QStringLiteral("Vultr API error (HTTP %1): %2")
                           .arg(status)
                           .arg(detail));
            return;
        }
        // Vultr just authenticated this key, so it is worth keeping: every
        // flow on this page (create, destroy) then resolves it from the store
        // instead of asking for it again. Later calls in the same run are a
        // no-op (adhoc #127).
        QString rememberError;
        const QStringList remembered =
            rememberVultrApiKey(apiKey, &rememberError);
        if (!remembered.isEmpty()) {
            appendHostInstallLog(
                QStringLiteral("Saved your Vultr API key to %1.\n")
                    .arg(remembered.join(QStringLiteral(" and "))));
            if (!rememberError.isEmpty())
                appendHostInstallLog(rememberError + QLatin1Char('\n'));
        }
        onDone(object, QString());
    });
}

void MainWindow::cloudflareApiCall(const QString &apiToken, const QString &path,
                                   const QByteArray &method,
                                   const QJsonObject &body,
                                   std::function<void(QJsonObject, QString)> onDone)
{
    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.cloudflare.com/client/v4") + path));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + apiToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    const QByteArray payload =
        QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply *reply =
        method == QByteArrayLiteral("GET")
            ? m_networkAccess->get(request)
            : m_networkAccess->sendCustomRequest(request, method, payload);
    connect(reply, &QNetworkReply::finished, this, [reply, onDone] {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                .toInt();
        const QJsonObject object =
            QJsonDocument::fromJson(reply->read(1024 * 1024)).object();
        const bool succeeded =
            object.value(QStringLiteral("success")).toBool();
        if (reply->error() != QNetworkReply::NoError || status < 200 ||
            status >= 300 || !succeeded) {
            QString detail = object.value(QStringLiteral("errors"))
                                 .toArray()
                                 .first()
                                 .toObject()
                                 .value(QStringLiteral("message"))
                                 .toString();
            if (detail.isEmpty())
                detail = reply->errorString();
            onDone({}, QStringLiteral("Cloudflare API error (HTTP %1): %2")
                           .arg(status)
                           .arg(detail));
            return;
        }
        onDone(object, QString());
    });
}

// A brand-new Vultr instance only has a raw address, so it never lines up with
// the hand-provisioned mirrors that answer under the mesh's own zone. Resolve
// the zone, then create or update one DNS-only A record for "<node>.<zone>"
// (adhoc #331). Every failure here is soft: the mirror is already booting and
// links to the account over the relay regardless, so the DNS record is a
// convenience that must never abort provisioning.
void MainWindow::ensureVultrMirrorDns(const QString &node, const QString &ip,
                                      std::function<void(QString)> onDone)
{
    auto skip = [this, onDone](const QString &reason) {
        appendHostInstallLog(
            QStringLiteral("Skipping the Cloudflare DNS record: %1\n")
                .arg(reason));
        onDone(QString());
    };
    if (!m_networkAccess) {
        skip(QStringLiteral("network access is unavailable"));
        return;
    }
    const QMap<QString, QString> variables = ActionStore::variables();
    const QString apiToken =
        forkmesh::control::cloudflareApiTokenFromVariables(variables);
    if (apiToken.isEmpty()) {
        skip(QStringLiteral(
            "store a CLOUDFLARE_API_TOKEN device variable with DNS edit "
            "permission to name mirrors automatically"));
        return;
    }
    QSettings settings;
    QString zone = settings.value(QStringLiteral("control/cloudflareZone"))
                       .toString()
                       .trimmed();
    if (zone.isEmpty())
        zone = forkmesh::control::cloudflareZoneNameFromVariables(variables);
    const QString hostname =
        forkmesh::control::vultrMirrorDnsHostname(node, zone);
    if (hostname.isEmpty()) {
        skip(zone.isEmpty()
                 ? QStringLiteral("set the Cloudflare zone on the Control "
                                  "node page first")
                 : QStringLiteral("\"%1\" and \"%2\" do not form a valid "
                                  "hostname")
                       .arg(node, zone));
        return;
    }
    const QJsonObject payload =
        forkmesh::control::vultrMirrorDnsRecordPayload(hostname, ip);
    if (payload.isEmpty()) {
        skip(QStringLiteral("the instance address %1 is not a usable IPv4 "
                            "answer").arg(ip));
        return;
    }
    appendHostInstallLog(
        QStringLiteral("Pointing %1 at %2 in Cloudflare\xE2\x80\xA6\n")
            .arg(hostname, ip));
    cloudflareApiCall(
        apiToken,
        QStringLiteral("/zones?name=%1")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(zone))),
        QByteArrayLiteral("GET"), {},
        [this, apiToken, zone, hostname, payload, skip, onDone](
            QJsonObject result, QString error) {
            if (!error.isEmpty()) {
                skip(error);
                return;
            }
            const QString zoneId = forkmesh::control::cloudflareZoneId(
                result.value(QStringLiteral("result")).toArray(), zone);
            if (zoneId.isEmpty()) {
                skip(QStringLiteral(
                         "this API token does not see exactly one \"%1\" zone")
                         .arg(zone));
                return;
            }
            cloudflareApiCall(
                apiToken,
                QStringLiteral("/zones/%1/dns_records?type=A&name=%2")
                    .arg(zoneId,
                         QString::fromLatin1(
                             QUrl::toPercentEncoding(hostname))),
                QByteArrayLiteral("GET"), {},
                [this, apiToken, zoneId, hostname, payload, skip, onDone](
                    QJsonObject existing, QString listError) {
                    if (!listError.isEmpty()) {
                        skip(listError);
                        return;
                    }
                    const QString recordId =
                        forkmesh::control::cloudflareDnsRecordId(
                            existing.value(QStringLiteral("result")).toArray(),
                            hostname, QStringLiteral("A"));
                    const QString path =
                        recordId.isEmpty()
                            ? QStringLiteral("/zones/%1/dns_records")
                                  .arg(zoneId)
                            : QStringLiteral("/zones/%1/dns_records/%2")
                                  .arg(zoneId, recordId);
                    cloudflareApiCall(
                        apiToken, path,
                        recordId.isEmpty() ? QByteArrayLiteral("POST")
                                           : QByteArrayLiteral("PUT"),
                        payload,
                        [this, hostname, recordId, skip, onDone](
                            QJsonObject, QString writeError) {
                            if (!writeError.isEmpty()) {
                                skip(writeError);
                                return;
                            }
                            appendHostInstallLog(
                                QStringLiteral("Cloudflare DNS record %1 "
                                               "for %2.\n")
                                    .arg(recordId.isEmpty()
                                             ? QStringLiteral("created")
                                             : QStringLiteral("updated"),
                                         hostname));
                            onDone(hostname);
                        });
                });
        });
}

void MainWindow::ensureVultrManagedKeypair(
    std::function<void(QString, QString, QString)> onDone)
{
    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString sshDir = QDir(appDataDir).filePath(QStringLiteral("ssh"));
    if (appDataDir.isEmpty() || !QDir().mkpath(sshDir)) {
        onDone({}, {}, QStringLiteral(
            "ForkMesh could not create its managed SSH key directory."));
        return;
    }
    QFile::setPermissions(sshDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    const QString keyPath =
        QDir(sshDir).filePath(QStringLiteral("vultr_mirror_ed25519"));
    const QString pubPath = keyPath + QStringLiteral(".pub");
    const auto readPublicKey = [pubPath]() {
        QFile pub(pubPath);
        if (!pub.open(QIODevice::ReadOnly))
            return QString();
        return QString::fromUtf8(pub.read(64 * 1024)).trimmed();
    };
    if (QFileInfo(keyPath).isFile()) {
        const QString publicKey = readPublicKey();
        if (!publicKey.isEmpty()) {
            onDone(keyPath, publicKey, {});
            return;
        }
    }
    appendHostInstallLog(QString::fromUtf8(
        "Generating the managed SSH key for Vultr mirrors\xE2\x80\xA6\n"));
    auto *keygen = new QProcess(this);
    keygen->setProcessChannelMode(QProcess::MergedChannels);
    connect(keygen, &QProcess::errorOccurred, this,
            [keygen, onDone](QProcess::ProcessError processError) {
                if (processError != QProcess::FailedToStart)
                    return;
                keygen->deleteLater();
                onDone({}, {}, QStringLiteral(
                    "Could not start ssh-keygen. Install OpenSSH and try "
                    "again."));
            });
    connect(keygen, &QProcess::finished, this,
            [keygen, keyPath, readPublicKey, onDone](
                int code, QProcess::ExitStatus exitStatus) {
                keygen->deleteLater();
                if (exitStatus != QProcess::NormalExit || code != 0 ||
                    !QFileInfo(keyPath).isFile()) {
                    onDone({}, {}, QStringLiteral(
                        "ssh-keygen could not create the managed key "
                        "(exit %1).").arg(code));
                    return;
                }
                QFile::setPermissions(
                    keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                const QString publicKey = readPublicKey();
                if (publicKey.isEmpty()) {
                    onDone({}, {}, QStringLiteral(
                        "The managed key was created but its public half "
                        "could not be read."));
                    return;
                }
                onDone(keyPath, publicKey, {});
            });
    keygen->start(QStringLiteral("ssh-keygen"),
                  {QStringLiteral("-q"), QStringLiteral("-t"),
                   QStringLiteral("ed25519"), QStringLiteral("-N"),
                   QString(), QStringLiteral("-C"),
                   QStringLiteral("forkmesh-vultr-mirror"),
                   QStringLiteral("-f"), keyPath});
}

void MainWindow::resolveVultrSshKeyId(
    const QString &apiKey, const QString &publicKey,
    std::function<void(QString, QString)> onDone)
{
    // Match on the key blob (type + base64) so the managed key is found even
    // if it was renamed in the Vultr panel; register it once otherwise.
    const QStringList parts =
        publicKey.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString blob = parts.size() >= 2
                             ? parts.at(0) + QLatin1Char(' ') + parts.at(1)
                             : publicKey;
    vultrApiCall(
        apiKey, QStringLiteral("/v2/ssh-keys?per_page=500"),
        QByteArrayLiteral("GET"), {},
        [this, apiKey, publicKey, blob, onDone](QJsonObject result,
                                                QString error) {
            if (!error.isEmpty()) {
                onDone({}, error);
                return;
            }
            for (const QJsonValue &value :
                 result.value(QStringLiteral("ssh_keys")).toArray()) {
                const QJsonObject key = value.toObject();
                const QString id =
                    key.value(QStringLiteral("id")).toString();
                if (!id.isEmpty() &&
                    key.value(QStringLiteral("ssh_key"))
                        .toString()
                        .trimmed()
                        .startsWith(blob)) {
                    onDone(id, {});
                    return;
                }
            }
            const QJsonObject body{
                {QStringLiteral("name"),
                 QStringLiteral("forkmesh-mirror-controller")},
                {QStringLiteral("ssh_key"), publicKey},
            };
            vultrApiCall(
                apiKey, QStringLiteral("/v2/ssh-keys"),
                QByteArrayLiteral("POST"), body,
                [onDone](QJsonObject created, QString postError) {
                    if (!postError.isEmpty()) {
                        onDone({}, postError);
                        return;
                    }
                    const QString id =
                        created.value(QStringLiteral("ssh_key"))
                            .toObject()
                            .value(QStringLiteral("id"))
                            .toString();
                    if (id.isEmpty()) {
                        onDone({}, QStringLiteral(
                            "Vultr did not return an SSH key id."));
                        return;
                    }
                    onDone(id, {});
                });
        });
}

void MainWindow::createVultrMirrorFromForm()
{
    if (m_vultrProvisionActive) {
        if (m_vultrStatus)
            m_vultrStatus->setText(
                QStringLiteral("A Vultr mirror is already being created."));
        return;
    }
    if ((m_hostInstallProcess &&
         m_hostInstallProcess->state() != QProcess::NotRunning) ||
        m_hostDeployRemaining > 0) {
        if (m_vultrStatus)
            m_vultrStatus->setText(QStringLiteral(
                "Wait for the running install to finish first."));
        return;
    }
    QString apiKey =
        m_vultrApiKeyEdit ? m_vultrApiKeyEdit->text().trimmed() : QString();
    bool storedKey = false;
    if (apiKey.isEmpty()) {
        // Same convenience as the Cloudflare tooling: reuse a credential this
        // node already stores as a device-local Actions variable.
        apiKey = forkmesh::control::vultrApiKeyFromVariables(
            ActionStore::variables());
        storedKey = !apiKey.isEmpty();
    }
    QString node =
        m_vultrNameEdit ? m_vultrNameEdit->text().trimmed() : QString();
    // Saved host node names already in use, so repeated one-click deploys
    // (and manually typed names) never collide with an existing mirror.
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    QSet<QString> used;
    QStringList knownNames;
    for (const QJsonValue &value : hosts) {
        const QString name = value.toObject()
                                 .value(QStringLiteral("name"))
                                 .toString()
                                 .toLower();
        used.insert(name);
        knownNames.append(name);
    }
    if (node.isEmpty()) {
        // Continue the fleet's mirrorN numbering rather than naming the node
        // after its hosting provider: the account's linked nodes carry the
        // mirrors this device never saved as hosts (adhoc #344).
        knownNames += m_profileLinkedNodes;
        node = forkmesh::control::nextMirrorNodeName(knownNames);
        if (node.isEmpty()) {
            if (m_vultrStatus)
                m_vultrStatus->setText(QStringLiteral(
                    "Could not pick a free mirror name — enter one."));
            return;
        }
    } else if (used.contains(node.toLower())) {
        if (m_vultrStatus)
            m_vultrStatus->setText(QStringLiteral(
                "A saved host named \"%1\" already exists — choose a "
                "different node name.").arg(node));
        return;
    }
    const QString invalid =
        forkmesh::control::validateVultrMirrorRequest(apiKey, node);
    if (!invalid.isEmpty()) {
        if (m_vultrStatus)
            m_vultrStatus->setText(invalid);
        return;
    }

    m_vultrProvisionActive = true;
    m_vultrPollCount = 0;
    m_vultrInstallAttempts = 0;
    // A brand-new instance is nobody's mirror yet, so the installer's default
    // relay-download path has no online node to clone from and dies with "No
    // online ForkMesh node is currently mirroring 'forkmesh'". Start straight
    // in direct-upload mode whenever this app's own binary can run on the
    // instance we are about to create (always x64 Debian) — that needs no
    // online mirror at all. The failure-driven switch below stays as the
    // fallback for the platforms an upload cannot serve. An unreadable own
    // binary would fail every attempt before SSH is even reached, so it also
    // keeps the download path.
    m_vultrInstallUseLocalBinary =
         forkmesh::control::localBinaryRunsOnVultrMirror(
             QSysInfo::kernelType(), QSysInfo::currentCpuArchitecture()) &&
         QFileInfo(QCoreApplication::applicationFilePath()).isReadable();
    m_vultrInstallAgentClis =
        m_vultrAgentClisCheck && m_vultrAgentClisCheck->isChecked();
    m_vultrInstallAttemptLog.clear();
    m_vultrDnsHostname.clear();
    m_vultrHostMetadata = QJsonObject{
        {QStringLiteral("provider"), QStringLiteral("Vultr")},
        {QStringLiteral("displayName"), node},
    };
    m_hostInstallAttemptBanner.clear();
    if (m_vultrCreateButton)
        m_vultrCreateButton->setEnabled(false);
    if (m_hostInstallLog) {
        m_hostInstallLog->clear();
        m_hostInstallLogCarry.clear();
        m_hostInstallLogFg = -1;
        m_hostInstallLogBold = false;
    }
    appendHostInstallLog(QString::fromUtf8(
        "Creating Vultr mirror \"%1\" \xE2\x80\x94 cheapest supported US plan, latest "
        "Debian, managed SSH key\xE2\x80\xA6\n").arg(node));
    if (storedKey)
        appendHostInstallLog(QStringLiteral(
            "Using the VULTR_API_KEY stored as a device variable.\n"));
    if (m_vultrInstallUseLocalBinary)
        appendHostInstallLog(QString::fromUtf8(
            "This app's own binary will be uploaded over SSH, so the new "
            "mirror needs no published release to install.\n"));
    if (m_vultrInstallAgentClis) {
        const QString agentAccess = forkmesh::control::describeAgentCliCredentials(
            localAgentCliCredentials());
        appendHostInstallLog(
            agentAccess.isEmpty()
                ? QString::fromUtf8(
                      "Claude Code and Codex will be installed, but this device "
                      "has no provider login to copy \xE2\x80\x94 the mirror "
                      "will need its own sign-in.\n")
                : QStringLiteral(
                      "Claude Code and Codex will be installed and signed in "
                      "with this device's access (%1).\n").arg(agentAccess));
    }
    if (m_vultrStatus)
        m_vultrStatus->setText(
            QString::fromUtf8("Preparing the managed SSH key\xE2\x80\xA6"));

    ensureVultrManagedKeypair([this, apiKey, node](
                                  QString keyPath, QString publicKey,
                                  QString keyError) {
        if (!keyError.isEmpty()) {
            finishVultrProvision(false, keyError);
            return;
        }
        appendHostInstallLog(
            QStringLiteral("Managed SSH key: %1\n").arg(keyPath));
        if (m_vultrStatus)
            m_vultrStatus->setText(QString::fromUtf8(
                "Registering the SSH key with Vultr\xE2\x80\xA6"));
        resolveVultrSshKeyId(apiKey, publicKey, [this, apiKey, node, keyPath](
                                                    QString sshKeyId,
                                                    QString sshError) {
            if (!sshError.isEmpty()) {
                finishVultrProvision(false, sshError);
                return;
            }
            if (m_vultrStatus)
                m_vultrStatus->setText(QString::fromUtf8(
                    "Choosing the cheapest supported US plan\xE2\x80\xA6"));
            vultrApiCall(
                apiKey, QStringLiteral("/v2/plans?per_page=500"),
                QByteArrayLiteral("GET"), {},
                [this, apiKey, node, keyPath, sshKeyId](
                    QJsonObject plansResult, QString planError) {
                    if (!planError.isEmpty()) {
                        finishVultrProvision(false, planError);
                        return;
                    }
                    const QJsonObject plan =
                        forkmesh::control::cheapestVultrPlan(
                            plansResult.value(QStringLiteral("plans"))
                                .toArray());
                    const QString region =
                        forkmesh::control::vultrPlanRegion(plan);
                    if (plan.isEmpty() || region.isEmpty()) {
                        finishVultrProvision(false, QStringLiteral(
                            "No deployable US plan is available on this Vultr "
                            "account (New Jersey, Atlanta, or another supported "
                            "US region)."));
                        return;
                    }
                    m_vultrHostMetadata.insert(
                        QStringLiteral("planType"),
                        plan.value(QStringLiteral("id")));
                    m_vultrHostMetadata.insert(
                        QStringLiteral("region"), region);
                    m_vultrHostMetadata.insert(
                        QStringLiteral("monthlyCost"),
                        plan.value(QStringLiteral("monthly_cost")));
                    m_vultrHostMetadata.insert(
                        QStringLiteral("ramMb"),
                        plan.value(QStringLiteral("ram")));
                    m_vultrHostMetadata.insert(
                        QStringLiteral("diskGb"),
                        plan.value(QStringLiteral("disk")));
                    appendHostInstallLog(
                        QStringLiteral(
                            "Cheapest supported plan: %1 ($%2/month, %3 MB RAM, %4 GB "
                            "disk) in region %5\n")
                            .arg(plan.value(QStringLiteral("id")).toString())
                            .arg(plan.value(QStringLiteral("monthly_cost"))
                                     .toDouble())
                            .arg(plan.value(QStringLiteral("ram")).toInt())
                            .arg(plan.value(QStringLiteral("disk")).toInt())
                            .arg(region));
                    vultrApiCall(
                        apiKey, QStringLiteral("/v2/os?per_page=500"),
                        QByteArrayLiteral("GET"), {},
                        [this, apiKey, node, keyPath, sshKeyId, plan, region](
                            QJsonObject osResult, QString osError) {
                            if (!osError.isEmpty()) {
                                finishVultrProvision(false, osError);
                                return;
                            }
                            const QJsonObject debian =
                                forkmesh::control::latestVultrDebianOs(
                                    osResult.value(QStringLiteral("os"))
                                        .toArray());
                            if (debian.isEmpty()) {
                                finishVultrProvision(false, QStringLiteral(
                                    "Vultr offers no Debian x64 image right "
                                    "now."));
                                return;
                            }
                            appendHostInstallLog(
                                QStringLiteral("Operating system: %1\n")
                                    .arg(debian.value(QStringLiteral("name"))
                                             .toString()));
                            if (m_vultrStatus)
                                m_vultrStatus->setText(QString::fromUtf8(
                                    "Creating the instance\xE2\x80\xA6"));
                            const QJsonObject payload =
                                forkmesh::control::vultrInstanceCreatePayload(
                                    node,
                                    plan.value(QStringLiteral("id"))
                                        .toString(),
                                    region,
                                    debian.value(QStringLiteral("id"))
                                        .toInt(),
                                    sshKeyId);
                            vultrApiCall(
                                apiKey, QStringLiteral("/v2/instances"),
                                QByteArrayLiteral("POST"), payload,
                                [this, apiKey, node, keyPath](
                                    QJsonObject createResult,
                                    QString createError) {
                                    if (!createError.isEmpty()) {
                                        finishVultrProvision(false,
                                                             createError);
                                        return;
                                    }
                                    const QString instanceId =
                                        createResult
                                            .value(QStringLiteral("instance"))
                                            .toObject()
                                            .value(QStringLiteral("id"))
                                            .toString();
                                    if (instanceId.isEmpty()) {
                                        finishVultrProvision(
                                            false,
                                            QStringLiteral(
                                                "Vultr did not return an "
                                                "instance id."));
                                        return;
                                    }
                                    m_vultrHostMetadata.insert(
                                        QStringLiteral("instanceId"),
                                        instanceId);
                                    appendHostInstallLog(QString::fromUtf8(
                                        "Instance %1 created \xE2\x80\x94 "
                                        "waiting for it to boot\xE2\x80\xA6\n")
                                        .arg(instanceId));
                                    if (m_vultrStatus)
                                        m_vultrStatus->setText(
                                            QString::fromUtf8(
                                                "Waiting for the instance to "
                                                "boot\xE2\x80\xA6"));
                                    pollVultrInstance(apiKey, instanceId,
                                                      node, keyPath);
                                });
                        });
                });
        });
    });
}

void MainWindow::pollVultrInstance(const QString &apiKey,
                                   const QString &instanceId,
                                   const QString &node,
                                   const QString &identityFile)
{
    constexpr int kMaxPolls = 60; // ~10 minutes at one poll every 10 s
    if (!m_vultrProvisionActive)
        return;
    vultrApiCall(
        apiKey, QStringLiteral("/v2/instances/") + instanceId,
        QByteArrayLiteral("GET"), {},
        [this, apiKey, instanceId, node, identityFile](QJsonObject result,
                                                       QString error) {
            if (!m_vultrProvisionActive)
                return;
            if (!error.isEmpty()) {
                finishVultrProvision(false, error);
                return;
            }
            const QJsonObject instance =
                result.value(QStringLiteral("instance")).toObject();
            const QString ip =
                forkmesh::control::vultrInstanceReadyIp(instance);
            if (ip.isEmpty()) {
                if (forkmesh::control::vultrInstanceIsIpv6Only(instance)) {
                    // Nothing in the mesh can reach a v6-only host, and waiting
                    // out the poll budget would never change that (adhoc #344).
                    finishVultrProvision(false, QStringLiteral(
                        "Vultr gave this instance an IPv6 address only, which "
                        "the mesh cannot reach. Destroy instance %1 in the "
                        "Vultr panel and retry — ForkMesh only deploys IPv4 "
                        "plans.").arg(instanceId));
                    return;
                }
                if (++m_vultrPollCount >= kMaxPolls) {
                    finishVultrProvision(false, QStringLiteral(
                        "The instance did not become ready in time. Check "
                        "it in the Vultr panel, then Add host + Install "
                        "ForkMesh manually once it is up."));
                    return;
                }
                if (m_vultrStatus)
                    m_vultrStatus->setText(
                        QString::fromUtf8(
                            "Waiting for the instance to boot "
                            "(status: %1)\xE2\x80\xA6")
                            .arg(instance.value(QStringLiteral("status"))
                                     .toString()));
                QTimer::singleShot(
                    10000, this,
                    [this, apiKey, instanceId, node, identityFile] {
                        pollVultrInstance(apiKey, instanceId, node,
                                          identityFile);
                    });
                return;
            }
            appendHostInstallLog(
                QStringLiteral("Instance is up at %1.\n").arg(ip));
            // Persist the host with its managed key path before the install
            // so every later SSH action (install, logs, uninstall, Actions)
            // authenticates with that key. Vultr Debian images boot as root.
            rememberHost(node, ip, QStringLiteral("root"), QString(),
                         QStringLiteral("vultr booting"), identityFile,
                         m_vultrHostMetadata);
            // Name the node in the operator's Cloudflare zone while SSH is
            // still coming up, so it joins the mesh the way the other mirrors
            // do rather than as a bare address (adhoc #331).
            if (m_vultrStatus)
                m_vultrStatus->setText(QString::fromUtf8(
                    "Adding the Cloudflare DNS record\xE2\x80\xA6"));
            ensureVultrMirrorDns(
                node, ip, [this, node, ip, identityFile](QString hostname) {
                    if (!m_vultrProvisionActive)
                        return;
                    m_vultrDnsHostname = hostname;
                    if (m_vultrStatus)
                        m_vultrStatus->setText(QString::fromUtf8(
                            "Giving SSH a moment to come up\xE2\x80\xA6"));
                    QTimer::singleShot(
                        15000, this, [this, node, ip, identityFile] {
                            startVultrHostInstall(node, ip, identityFile);
                        });
                });
        });
}

void MainWindow::appendVultrAttemptHistory()
{
    if (m_vultrInstallAttemptLog.isEmpty())
        return;
    QString block = QString::fromUtf8("\nInstall attempts this run:\n");
    for (const QString &entry : std::as_const(m_vultrInstallAttemptLog))
        block += QString::fromUtf8("  \xE2\x80\xA2 %1\n").arg(entry);
    appendHostInstallLog(block);
}

void MainWindow::startVultrHostInstall(const QString &node, const QString &ip,
                                       const QString &identityFile)
{
    constexpr int kMaxInstallAttempts = 6;
    if (!m_vultrProvisionActive)
        return;
    // Hand off to the shared install path through the form it reads; the
    // managed key is picked up from the saved host's identityFile.
    if (m_hostIpEdit)
        m_hostIpEdit->setText(ip);
    if (m_hostUserEdit)
        m_hostUserEdit->setText(QStringLiteral("root"));
    if (m_hostPassEdit)
        m_hostPassEdit->clear();
    if (m_hostNameEdit)
        m_hostNameEdit->setText(node);
    if (m_hostUploadBinaryCheck)
        m_hostUploadBinaryCheck->setChecked(false);
    ++m_vultrInstallAttempts;
    const QString attemptLabel =
        QStringLiteral("Attempt %1 of %2 at %3")
            .arg(QString::number(m_vultrInstallAttempts),
                 QString::number(kMaxInstallAttempts),
                 QDateTime::currentDateTime().toString(
                     QStringLiteral("hh:mm:ss")));
    // Keep every attempt's output in the window rather than clearing the log
    // on each retry (adhoc #342) — a run that fails six times is exactly when
    // the earlier transcripts matter.
    // The banner is set for the first attempt too, so the provisioning
    // preamble above it (instance id, address, DNS record) survives as well.
    m_hostInstallAttemptBanner = attemptLabel;
    if (m_vultrStatus)
        m_vultrStatus->setText(
            QString::fromUtf8(
                m_vultrInstallUseLocalBinary
                    ? "Installing ForkMesh (attempt %1 of %2) \xE2\x80\x94 "
                      "uploading this app's release directly\xE2\x80\xA6"
                    : "Installing ForkMesh (attempt %1 of %2)\xE2\x80\xA6")
                .arg(m_vultrInstallAttempts)
                .arg(kMaxInstallAttempts));
    // A fresh instance often refuses SSH for a short while after Vultr
    // reports it active, so an early attempt failing is expected, not a
    // real failure — only the last attempt should report "Install failed".
    const bool isFinalAttempt = m_vultrInstallAttempts >= kMaxInstallAttempts;
    runHostInstall(m_vultrInstallUseLocalBinary, [this, node, ip, identityFile,
                           attemptLabel, isFinalAttempt](bool ok) {
        if (!m_vultrProvisionActive)
            return;
        m_vultrInstallAttemptLog.append(
            QString::fromUtf8("%1 \xE2\x80\x94 %2")
                .arg(attemptLabel,
                     ok ? QStringLiteral("installed")
                        : (m_hostInstallLastFailure.isEmpty()
                               ? QStringLiteral("failed")
                               : m_hostInstallLastFailure)));
        if (ok) {
            appendVultrAttemptHistory();
            const QString address =
                m_vultrDnsHostname.isEmpty()
                    ? ip
                    : QStringLiteral("%1, %2").arg(m_vultrDnsHostname, ip);
            const QString done = QString::fromUtf8(
                "Vultr mirror \"%1\" (%2) is installed, linked, and published "
                "to Mirror nodes and the World.").arg(node, address);
            if (!m_vultrInstallAgentClis) {
                waitForVultrMirrorPublication(node, done);
                return;
            }
            // The node is up and authenticated to the mesh; give it this
            // device's agent access too so it can run sessions immediately
            // (adhoc #418). A failure here does not undo the mirror itself.
            // With no login to copy the CLIs are still installed, so the
            // mirror only needs its own sign-in rather than everything.
            const bool copyLogins =
                !forkmesh::control::agentCliCredentialsAreEmpty(
                    localAgentCliCredentials());
            if (m_vultrStatus)
                m_vultrStatus->setText(QString::fromUtf8(
                    "Installing Claude Code and Codex\xE2\x80\xA6"));
            runAgentCliInstall(
                node, ip, QStringLiteral("root"), QString(), identityFile,
                copyLogins,
                [this, node, done](bool agentOk, QString agentMessage) {
                    if (!m_vultrProvisionActive)
                        return;
                    waitForVultrMirrorPublication(
                        node,
                        agentOk
                            ? QStringLiteral("%1 %2").arg(done, agentMessage)
                            : QString::fromUtf8(
                                  "%1 The agent CLIs were not set up: %2")
                                  .arg(done, agentMessage));
                });
            return;
        }
        // An address outside the routable internet will never answer, however
        // long we wait — stop burning attempts and say what is actually wrong
        // (adhoc #342).
        const QString unroutable = forkmesh::control::nonRoutableAddressNote(ip);
        if (!unroutable.isEmpty()) {
            appendVultrAttemptHistory();
            finishVultrProvision(false, QString::fromUtf8(
                "%1 is in %2, so SSH from this machine can never reach it. "
                "Give the instance a public address (or run the install from "
                "the network that owns that range); it is saved under Hosts "
                "\xE2\x80\x94 fix the address there and click Update.")
                .arg(ip, unroutable));
            return;
        }
        // A brand-new instance has nobody mirroring it yet and may have no
        // published release for its platform, so a relay download/clone can
        // never succeed no matter how many times it is retried the same way.
        // Switch this and every later attempt this run to uploading this app's
        // own release binary directly over the SSH session instead — that needs
        // neither an online mirror nor a published release — and retry right
        // away rather than waiting out the "host not reachable yet" backoff
        // below, since SSH clearly worked.
        if (!m_vultrInstallUseLocalBinary && !isFinalAttempt &&
            forkmesh::control::vultrInstallNeedsLocalBinary(
                m_hostInstallRawTail)) {
            m_vultrInstallUseLocalBinary = true;
            if (m_vultrStatus)
                m_vultrStatus->setText(QString::fromUtf8(
                    "Nothing published to install from yet \xE2\x80\x94 "
                    "retrying with this app's own binary uploaded "
                    "directly\xE2\x80\xA6"));
            QTimer::singleShot(2000, this, [this, node, ip, identityFile] {
                startVultrHostInstall(node, ip, identityFile);
            });
            return;
        }
        if (m_vultrInstallAttempts >= kMaxInstallAttempts) {
            appendVultrAttemptHistory();
            finishVultrProvision(false, QString::fromUtf8(
                "Install did not succeed after %1 attempts. The instance is "
                "saved under Hosts \xE2\x80\x94 click Update there to retry.")
                .arg(kMaxInstallAttempts));
            return;
        }
        // A fresh instance often refuses SSH for a short while after it
        // reports active; back off and retry.
        if (m_vultrStatus)
            m_vultrStatus->setText(QString::fromUtf8(
                "Attempt %1 of %2 failed \xE2\x80\x94 host not reachable yet, "
                "retrying in 30 seconds\xE2\x80\xA6")
                .arg(m_vultrInstallAttempts)
                .arg(kMaxInstallAttempts));
        QTimer::singleShot(30000, this, [this, node, ip, identityFile] {
            startVultrHostInstall(node, ip, identityFile);
        });
    }, /*reinstall=*/false, /*fromSource=*/false,
    /*suppressFailureStatus=*/!isFinalAttempt);
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

// Render one chunk of installer output into `log`: the installer streams
// ANSI/VT escape sequences — SGR colour codes plus a box-drawing banner — so
// paint the SGR colours and drop every other control sequence, otherwise the
// raw codes show up as literal "[32m"/"[0m" noise (adhoc #6). A sequence can
// straddle two read chunks, so an unfinished tail is carried over in `carry`
// and the running SGR style lives in `fg` (0xRRGGBB, -1 = theme default) and
// `bold`. Factored out of appendHostInstallLog so the single live log and every
// per-host pane of a parallel fleet deploy share one renderer.
void appendAnsiLog(QPlainTextEdit *log, QString &carry, int &fg, bool &bold,
                   bool dark, const QString &text)
{
    if (!log || text.isEmpty())
        return;

    QString data = carry + text;
    carry.clear();

    QTextCursor cursor(log->document());
    cursor.movePosition(QTextCursor::End);

    auto currentFormat = [&fg, &bold]() {
        QTextCharFormat fmt;
        if (fg >= 0)
            fmt.setForeground(
                QColor((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF));
        if (bold)
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
    auto applySgr = [&fg, &bold, dark](const QString &paramStr) {
        const QStringList parts =
            paramStr.isEmpty() ? QStringList{QStringLiteral("0")}
                               : paramStr.split(QLatin1Char(';'));
        for (int k = 0; k < parts.size(); ++k) {
            bool ok = false;
            const int code = parts.at(k).toInt(&ok);
            if (!ok)
                continue;
            if (code == 0) {
                fg = -1;
                bold = false;
            } else if (code == 1) {
                bold = true;
            } else if (code == 22) {
                bold = false;
            } else if (code == 39) {
                fg = -1;
            } else if (code >= 30 && code <= 37) {
                fg = installLogAnsiFg(code - 30, dark);
            } else if (code >= 90 && code <= 97) {
                fg = installLogAnsiFg(8 + code - 90, dark);
            } else if (code == 38 && k + 2 < parts.size() &&
                       parts.at(k + 1).toInt() == 5) {
                const int idx = parts.at(k + 2).toInt();
                fg = idx < 16 ? installLogAnsiFg(idx, dark)
                              : installLogXterm256(idx);
                k += 2;
            } else if (code == 38 && k + 4 < parts.size() &&
                       parts.at(k + 1).toInt() == 2) {
                fg = ((parts.at(k + 2).toInt() & 0xFF) << 16) |
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
            carry = data.mid(i);
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
                carry = data.mid(i);
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
                carry = data.mid(i);
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
    if (carry.size() > 256) {
        cursor.insertText(carry, currentFormat());
        carry.clear();
    }

    log->moveCursor(QTextCursor::End);
    log->ensureCursorVisible();
}
} // namespace

void MainWindow::appendHostInstallLog(const QString &text)
{
    appendAnsiLog(m_hostInstallLog, m_hostInstallLogCarry, m_hostInstallLogFg,
                  m_hostInstallLogBold, currentThemeIsDark(), text);
}

void MainWindow::appendHostDeployLog(HostDeploySession *session,
                                     const QString &text)
{
    if (!session)
        return;
    appendAnsiLog(session->log, session->ansiCarry, session->ansiFg,
                  session->ansiBold, currentThemeIsDark(), text);
}

namespace {
// Sentinel line separating the (possibly sudo-consumed) password from the
// uploaded binary on the SSH session's stdin in direct-upload installs
// (adhoc #67). The remote side discards lines until it sees this marker, so
// the upload stays intact whether or not sudo actually read the password.
const QString kHostUploadMarker = QStringLiteral("__FORKMESH_UPLOAD__");

QString controllerReleaseManifestDigest(const QString &expectedBuildCommit,
                                        QString *errorOut)
{
    QString configured =
        qEnvironmentVariable("FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256")
            .trimmed()
            .toLower();
#ifdef FORKMESH_WINDOW_TESTS
    const QString testDigest =
        qEnvironmentVariable("FORKMESH_TEST_RELEASE_MANIFEST_SHA256")
            .trimmed()
            .toLower();
    if (!testDigest.isEmpty())
        configured = testDigest;
#endif
    static const QRegularExpression exactDigest(
        QStringLiteral("^[0-9a-f]{64}$"));
    if (!configured.isEmpty()) {
        if (exactDigest.match(configured).hasMatch())
            return configured;
        if (errorOut) {
            *errorOut = QStringLiteral(
                "FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256 must be an exact "
                "64-hex SHA-256 digest.");
        }
        return {};
    }

    // A source checkout can authenticate the separately committed release
    // metadata directly. Packaged controllers, where that checkout is absent,
    // must receive the digest over their authenticated control channel through
    // the environment above; they never trust a hash copied from the remote
    // install response.
    const QString manifestPath =
        QDir(QStringLiteral(FORKMESH_SOURCE_DIR))
            .filePath(QStringLiteral(
                ".forkmesh/releases/latest/release.json"));
    QFile manifestFile(manifestPath);
    if (manifestFile.open(QIODevice::ReadOnly) &&
        manifestFile.size() > 0 && manifestFile.size() <= 1024 * 1024) {
        const QByteArray manifestBytes = manifestFile.readAll();
        QJsonParseError parseError;
        const QJsonObject manifest =
            QJsonDocument::fromJson(manifestBytes, &parseError).object();
        const QString expectedVersion =
            QStringLiteral(FORKMESH_VERSION);
        const QString tag =
            manifest.value(QStringLiteral("tag")).toString();
        const QString checksumsDigest =
            manifest.value(QStringLiteral("checksums_sha256"))
                .toString()
                .trimmed()
                .toLower();
        if (parseError.error == QJsonParseError::NoError &&
            manifest.value(QStringLiteral("schema")).toString() ==
                QStringLiteral("forkmesh-release-v2") &&
            manifest.value(QStringLiteral("build_commit"))
                    .toString()
                    .trimmed()
                    .toLower() == expectedBuildCommit &&
            (tag == expectedVersion ||
             tag == QStringLiteral("v") + expectedVersion) &&
            exactDigest.match(checksumsDigest).hasMatch()) {
            return QString::fromLatin1(
                QCryptographicHash::hash(
                    manifestBytes, QCryptographicHash::Sha256)
                    .toHex());
        }
    }
    if (errorOut) {
        *errorOut = QStringLiteral(
            "No controller-trusted release manifest matches this build. "
            "Refresh the checked-in signed release metadata or set "
            "FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256 from an authenticated "
            "control channel before deploying a fleet binary.");
    }
    return {};
}
} // namespace

bool MainWindow::buildHostInstallCommand(const QString &ip, const QString &user,
                                         const QString &node, bool uploadBinary,
                                         bool reinstall, bool fromSource,
                                         bool requirePublishedBinary,
                                         QString *remoteCmd, QByteArray *uploadBytes,
                                         QString *errorOut)
{
    const QString installUrl = installScriptUrl();
    if (installUrl.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not resolve the installer URL.");
        return false;
    }

    // Direct-upload mode (adhoc #67): stream this app's own release binary to
    // the host over the SSH session's stdin instead of the host downloading it
    // from the relay's release endpoint. Read the bytes up front so a locked or
    // missing binary fails here, before anything touches the remote machine.
    // A source build compiles on the host itself, so there is no binary to
    // upload — fromSource forces the direct-upload path off.
    // A published-binary fleet deploy and a local-executable upload are
    // mutually exclusive contracts. The former is deliberately resolved on
    // each target from the release manifest so a development build can never be
    // mistaken for the release merely because both report the same version.
    const bool doUpload =
        uploadBinary && !fromSource && !requirePublishedBinary;
    QByteArray bytes;
    if (doUpload) {
        QFile self(QCoreApplication::applicationFilePath());
        if (!self.open(QIODevice::ReadOnly) ||
            (bytes = self.readAll()).isEmpty()) {
            if (errorOut)
                *errorOut = QString::fromUtf8(
                                "Could not read this app's binary (%1) to "
                                "upload it.")
                                .arg(QCoreApplication::applicationFilePath());
            return false;
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
    QString expectedBuildCommit;
    QString expectedReleaseManifestDigest;
    if (requirePublishedBinary) {
        expectedBuildCommit =
            QStringLiteral(FORKMESH_BUILD_COMMIT).trimmed().toLower();
#ifdef FORKMESH_WINDOW_TESTS
        const QString testBuildCommit =
            qEnvironmentVariable("FORKMESH_TEST_BUILD_COMMIT")
                .trimmed()
                .toLower();
        if (!testBuildCommit.isEmpty())
            expectedBuildCommit = testBuildCommit;
#endif
        static const QRegularExpression exactCommit(
            QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
        if (!exactCommit.match(expectedBuildCommit).hasMatch()) {
            if (errorOut) {
                *errorOut = QString::fromUtf8(
                    "This ForkMesh build has no exact source revision, so it "
                    "cannot prove that a published same-version binary is "
                    "current. Rebuild from a Git checkout (or configure a "
                    "release archive with FORKMESH_BUILD_COMMIT_OVERRIDE), "
                    "then retry.");
            }
            return false;
        }
        expectedReleaseManifestDigest =
            controllerReleaseManifestDigest(expectedBuildCommit, errorOut);
        if (expectedReleaseManifestDigest.isEmpty())
            return false;
    }
    // Pass the chosen name as FORKMESH_NODE_NAME (not FORKMESH_NODE): the
    // installer uses it to name the freshly-deployed node and leaves the
    // clone-source mirror to auto-resolve to a real online one. The headless
    // installer then starts the node as a background daemon under this name so it
    // actually joins the network and shows up in the Mirror nodes list.
    // FORKMESH_OWNER carries the user name the fresh node is being attached to
    // so the installer can echo it (adhoc #258), and
    // FORKMESH_REINSTALL=1 tells it to wipe any existing install + data first.
    QString envPrefix = QStringLiteral("FORKMESH_NODE_NAME=%1").arg(shq(node));
    const QString linkUser = hostLinkUserName();
    if (!linkUser.isEmpty())
        envPrefix += QStringLiteral(" FORKMESH_OWNER=%1").arg(shq(linkUser));
    if (reinstall)
        envPrefix += QStringLiteral(" FORKMESH_REINSTALL=1");
    // Update-from-source (adhoc): FORKMESH_FROM_SOURCE=1 skips the prebuilt
    // release download and clones/pulls + rebuilds from the latest source;
    // FORKMESH_RESTART=1 makes the installer stop the old daemon before it
    // relaunches so the freshly built binary cleanly takes over (the node's data
    // is left untouched, unlike a reinstall).
    if (fromSource)
        envPrefix += QStringLiteral(" FORKMESH_FROM_SOURCE=1 FORKMESH_RESTART=1");
    // Fleet binary installs must never silently turn into a source build. Pin
    // the current release channel, preserve all node data, stop the old daemon
    // only after the new artifact is installed, and have the target verify the
    // exact version before its pane can report success.
    if (requirePublishedBinary) {
        envPrefix += QStringLiteral(
            " FORKMESH_RELEASE=latest FORKMESH_NO_SOURCE_FALLBACK=1 "
            "FORKMESH_EXPECTED_BUILD_COMMIT=%1 "
            "FORKMESH_EXPECTED_RELEASE_VERSION=%2 "
            "FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256=%3")
                         .arg(shq(expectedBuildCommit),
                              shq(QStringLiteral(FORKMESH_VERSION)),
                              shq(expectedReleaseManifestDigest));
        if (!reinstall)
            envPrefix += QStringLiteral(" FORKMESH_RESTART=1");
    }

    QString pipeline;
    if (requirePublishedBinary) {
        const QString expected =
            QStringLiteral("ForkMesh " FORKMESH_VERSION);
        // Download the installer into a temporary file rather than piping it
        // directly to bash. Without pipefail, `curl | bash` can return success
        // when curl failed and bash merely received an empty script.
        pipeline =
            QStringLiteral(
                "installer=\"$(mktemp \"${TMPDIR:-/tmp}/"
                "forkmesh-installer.XXXXXX\")\" || exit 70; "
                "if ! command -v sha256sum >/dev/null 2>&1 && "
                "! command -v shasum >/dev/null 2>&1; then "
                "printf 'A SHA-256 tool is required for a verified ForkMesh "
                "binary install.\\n' >&2; rm -f \"$installer\"; exit 67; fi; "
                "if ! curl -fsSL -H 'Cache-Control: no-cache' %1 "
                "-o \"$installer\"; then "
                "printf 'ForkMesh installer download failed.\\n' >&2; "
                "rm -f \"$installer\"; exit 70; fi; "
                "%2 bash \"$installer\"; st=$?; rm -f \"$installer\"; "
                "if [ \"$st\" -ne 0 ]; then exit \"$st\"; fi; "
                "bin=\"$HOME/.local/bin/forkmesh\"; "
                "if [ ! -x \"$bin\" ]; then "
                "printf 'ForkMesh binary missing after install: %s\\n' "
                "\"$bin\" >&2; exit 66; fi; "
                "actual=\"$(\"$bin\" --version 2>&1)\"; expected=%3; "
                "if [ \"$actual\" != \"$expected\" ]; then "
                "printf 'ForkMesh version check failed: expected %s, "
                "got %s\\n' \"$expected\" \"$actual\" >&2; exit 65; fi; "
                "commit_output=\"$(mktemp \"${TMPDIR:-/tmp}/"
                "forkmesh-build-commit.XXXXXX\")\" || exit 63; "
                "\"$bin\" --build-commit >\"$commit_output\" 2>&1 & "
                "commit_pid=$!; "
                "probe_ticks=0; "
                "while kill -0 \"$commit_pid\" 2>/dev/null && "
                "[ \"$probe_ticks\" -lt 50 ]; do sleep 0.1; "
                "probe_ticks=$((probe_ticks + 1)); done; "
                "commit_timed_out=0; "
                "if kill -0 \"$commit_pid\" 2>/dev/null; then "
                "commit_timed_out=1; "
                "kill \"$commit_pid\" 2>/dev/null || true; "
                "sleep 0.2; "
                "kill -KILL \"$commit_pid\" 2>/dev/null || true; fi; "
                "commit_status=0; "
                "wait \"$commit_pid\" || commit_status=$?; "
                "actual_commit=\"$(head -c 128 \"$commit_output\" | "
                "tr -d '\\r\\n')\"; rm -f \"$commit_output\"; "
                "expected_commit=%4; "
                "if [ \"$commit_timed_out\" -ne 0 ] || "
                "[ \"$commit_status\" -ne 0 ] || "
                "[ \"$actual_commit\" != \"$expected_commit\" ]; then "
                "printf 'ForkMesh source-revision check failed: the installed "
                "artifact did not report expected commit %s.\\n' "
                "\"$expected_commit\" >&2; "
                "exit 64; fi; "
                "printf 'Verified %s from source commit %s using the "
                "published checksum-verified release.\\n' \"$actual\" "
                "\"$actual_commit\"")
                .arg(shq(installUrl), envPrefix, shq(expected),
                     shq(expectedBuildCommit));
    } else {
        pipeline =
            QStringLiteral("curl -fsSL %1 | %2 bash")
                .arg(shq(installUrl), envPrefix);
    }
    const bool needSudo = user != QStringLiteral("root");
    if (doUpload) {
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
        const QString localBinarySha256 =
            QString::fromLatin1(
                QCryptographicHash::hash(
                    bytes, QCryptographicHash::Sha256)
                    .toHex());
        pipeline =
            QStringLiteral(
                "up=\"$(mktemp \"${TMPDIR:-/tmp}/forkmesh-upload.XXXXXX\")\" && "
                "while IFS= read -r l; do [ \"$l\" = %1 ] && break; done && "
                "cat > \"$up\" && curl -fsSL %2 | %3 "
                "FORKMESH_LOCAL_BINARY=\"$up\" FORKMESH_LOCAL_OS=%4 "
                "FORKMESH_LOCAL_ARCH=%5 "
                "FORKMESH_LOCAL_BINARY_SHA256=%6 bash; "
                "st=$?; rm -f \"$up\"; exit $st")
                .arg(shq(kHostUploadMarker), shq(installUrl), envPrefix,
                     shq(os), shq(QSysInfo::currentCpuArchitecture()),
                     shq(localBinarySha256));
    }
    const QString cmd =
        needSudo
            ? QStringLiteral("sudo -S -p '' -- bash -c %1").arg(shq(pipeline))
            : pipeline;

    if (remoteCmd)
        *remoteCmd = cmd;
    if (uploadBytes)
        *uploadBytes = bytes;
    return true;
}

void MainWindow::runHostInstall(bool forceUploadBinary,
                                std::function<void(bool)> onFinished,
                                bool reinstall, bool fromSource,
                                bool suppressFailureStatus)
{
    // A retry loop hands us a banner so its earlier attempts stay in the
    // window (adhoc #342); a plain one-shot install starts from a clean log.
    // Consumed once per call so it can never leak into a later manual run.
    const QString attemptBanner = m_hostInstallAttemptBanner;
    m_hostInstallAttemptBanner.clear();
    if ((m_hostInstallProcess &&
         m_hostInstallProcess->state() != QProcess::NotRunning) ||
        m_hostDeployRemaining > 0) {
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
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "Enter the host IP, SSH username and a node name first. "
                "Leave the password blank to use your SSH agent/default key."));
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
    // the host over the SSH session instead of the host downloading it from the
    // relay. forceUploadBinary is set by the per-row / install-all "Install
    // (binary)" actions (adhoc #257), which always direct-upload regardless of
    // the form's checkbox. A source build compiles on the host, so fromSource
    // forces the direct-upload path off.
    const bool uploadBinary = !fromSource &&
        (forceUploadBinary ||
         (m_hostUploadBinaryCheck && m_hostUploadBinaryCheck->isChecked()));
    const bool needSudo = user != QStringLiteral("root");
    QString remoteCmd;
    QByteArray uploadBytes;
    QString buildErr;
    if (!buildHostInstallCommand(ip, user, node, uploadBinary, reinstall,
                                 fromSource, false, &remoteCmd,
                                 &uploadBytes, &buildErr)) {
        // Record it as this run's failure too, so a retry loop's attempt
        // history says why the command could not even be built.
        m_hostInstallLastFailure = buildErr;
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(buildErr);
        if (onFinished)
            onFinished(false);
        return;
    }
    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            ip, user, pass, remoteCmd, &sshError,
            savedHostIdentityFile(node, ip, user));
    if (ssh.program.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(sshError);
        if (onFinished)
            onFinished(false);
        return;
    }

    // Persist the server info before we start so it is saved even if the install
    // fails partway through; a successful run flips the status to "installed".
    rememberHost(node, ip, user, pass, QStringLiteral("installing"));

    if (attemptBanner.isEmpty())
        m_hostInstallLog->clear();
    m_hostInstallLogCarry.clear();
    m_hostInstallLogFg = -1;
    m_hostInstallLogBold = false;
    if (!attemptBanner.isEmpty())
        appendHostInstallLog(
            QString::fromUtf8("\n\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94"
                              "\x80 %1 \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"
                              "\xE2\x94\x80\n")
                .arg(attemptBanner));
    m_hostInstallRawTail.clear();
    m_hostInstallLastFailure.clear();
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
    proc->setProcessEnvironment(ssh.environment);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        appendHostInstallLog(chunk);
        // Bounded tail kept only to classify a connection-level failure below
        // (e.g. "Connection timed out"); it never needs the full transcript.
        m_hostInstallRawTail = (m_hostInstallRawTail + chunk).right(4000);
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
                QString linkUser;
                const QString signer = hostLinkSigningAccountName(&linkUser);
                if (!signer.isEmpty()) {
                    appendHostInstallLog(QString::fromUtf8(
                        "\nLinking node to your user account (%1)\xE2\x80\xA6\n")
                        .arg(linkUser));
                    submitHostLinkCode(code);
                } else {
                    appendHostInstallLog(QString::fromUtf8(
                        "\n[warning] Not auto-linking this host: this node is "
                        "not linked to a user account. Link this node to your "
                        "user account, then reinstall the host to attach it.\n"));
                    promptHostLinkCode(code);
                }
            }
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            appendHostInstallLog(QString::fromUtf8(
                "\n[error] Could not start SSH. Install OpenSSH (and sshpass "
                "only when using password login) and try again.\n"));
    });
    connect(proc, &QProcess::finished, this,
            [this, ip, user, node, pass, onFinished, reinstall,
             suppressFailureStatus](int code, QProcess::ExitStatus status) {
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
                    m_hostInstallLastFailure =
                        forkmesh::control::sshFailureSummary(
                            code, m_hostInstallRawTail);
                    const QString hint = forkmesh::control::sshConnectionFailureHint(
                        code, m_hostInstallRawTail, ip);
                    if (!hint.isEmpty())
                        appendHostInstallLog(
                            QStringLiteral("\n%1\n").arg(hint));
                    // A caller that still has retries left (the Vultr
                    // auto-provision flow) reports its own "retrying..."
                    // status and only wants the terminal "Install failed"
                    // wording once its last attempt is spent.
                    if (!suppressFailureStatus) {
                        if (m_hostInstallStatus)
                            m_hostInstallStatus->setText(QString::fromUtf8(
                                "\xE2\x9C\x98 Install failed \xE2\x80\x94 see "
                                "the output above."));
                        rememberHost(node, ip, user, pass,
                                     QStringLiteral("install failed"));
                    }
                }
                if (m_hostInstallProcess) {
                    m_hostInstallProcess->deleteLater();
                    m_hostInstallProcess = nullptr;
                }
                if (onFinished)
                    onFinished(ok);
            });

    proc->start(ssh.program, ssh.arguments);
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
    runHostDeployAllParallel(FleetDeployMode::InstallBinary);
}

MainWindow::FleetDeployOptions
MainWindow::fleetDeployOptions(FleetDeployMode mode)
{
    switch (mode) {
    case FleetDeployMode::InstallBinary:
        // Download the published artifact on each target. Never upload the
        // current process: it may be a developer build rather than the packaged
        // release even when its version string is identical.
        return {/*uploadBinary=*/false, /*reinstall=*/false,
                /*fromSource=*/false, /*requirePublishedBinary=*/true};
    case FleetDeployMode::Reinstall:
        return {/*uploadBinary=*/false, /*reinstall=*/true,
                /*fromSource=*/false, /*requirePublishedBinary=*/true};
    case FleetDeployMode::UpdateSource:
        return {/*uploadBinary=*/false, /*reinstall=*/false,
                /*fromSource=*/true, /*requirePublishedBinary=*/false};
    }
    return {};
}

#ifdef FORKMESH_WINDOW_TESTS
QString MainWindow::testFleetBinaryInstallRemoteCommand(
    bool reinstall, qsizetype *uploadByteCount, QString *errorOut)
{
    const FleetDeployOptions options = fleetDeployOptions(
        reinstall ? FleetDeployMode::Reinstall
                  : FleetDeployMode::InstallBinary);
    QString remoteCommand;
    QByteArray uploadBytes;
    QString error;
    const bool ok = buildHostInstallCommand(
        QStringLiteral("host.example"), QStringLiteral("root"),
        QStringLiteral("test-node"), options.uploadBinary, options.reinstall,
        options.fromSource, options.requirePublishedBinary, &remoteCommand,
        &uploadBytes, &error);
    if (uploadByteCount)
        *uploadByteCount = uploadBytes.size();
    if (errorOut)
        *errorOut = error;
    return ok ? remoteCommand : QString();
}

QString MainWindow::testDirectBinaryInstallRemoteCommand(
    qsizetype *uploadByteCount, QString *errorOut)
{
    QString remoteCommand;
    QByteArray uploadBytes;
    QString error;
    const bool ok = buildHostInstallCommand(
        QStringLiteral("host.example"), QStringLiteral("root"),
        QStringLiteral("test-node"), /*uploadBinary=*/true,
        /*reinstall=*/false, /*fromSource=*/false,
        /*requirePublishedBinary=*/false, &remoteCommand, &uploadBytes,
        &error);
    if (uploadByteCount)
        *uploadByteCount = uploadBytes.size();
    if (errorOut)
        *errorOut = error;
    uploadBytes.fill('\0');
    uploadBytes.clear();
    return ok ? remoteCommand : QString();
}
#endif

void MainWindow::runHostReinstallAllFromBinary()
{
    if (!m_hostsTable || m_hostsTable->rowCount() == 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral("No saved hosts to reinstall."));
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
            "install the published ForkMesh v" FORKMESH_VERSION
            " binary and re-link each one to your account.\n\nThis cannot be "
            "undone. Continue?")
            .arg(rowCount),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes)
        return;
    runHostDeployAllParallel(FleetDeployMode::Reinstall);
}

void MainWindow::runHostUpdateAllFromSource()
{
    if (!m_hostsTable || m_hostsTable->rowCount() == 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral("No saved hosts to update."));
        return;
    }
    // Non-destructive, but a source build is slow (clone/pull + compile on each
    // box) and restarts every node, so confirm the fleet-wide run up front.
    const int rowCount = m_hostsTable->rowCount();
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("Update all hosts from source"),
        QString::fromUtf8(
            "For each of your %1 saved host(s): SSH in, pull the latest ForkMesh "
            "source, rebuild the client from it and restart the node. Each host "
            "keeps its identity key and mirrored data.\n\nBuilding from source on "
            "the host runs on all hosts at once — each streams into its own pane "
            "below. Continue?")
            .arg(rowCount),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Yes)
        return;
    runHostDeployAllParallel(FleetDeployMode::UpdateSource);
}

void MainWindow::runHostDeployAllParallel(FleetDeployMode mode)
{
    if (!m_hostsTable || m_hostsTable->rowCount() == 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QStringLiteral("No saved hosts."));
        return;
    }
    // Refuse to start on top of a single-host install or another parallel run —
    // each would fight over the shared status line and the deploy panel.
    if ((m_hostInstallProcess &&
         m_hostInstallProcess->state() != QProcess::NotRunning) ||
        m_hostDeployRemaining > 0) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(
                QStringLiteral("A host session is already running."));
        return;
    }
    if (!m_hostDeployPanel || !m_hostDeployGrid)
        return;

    // Load non-sensitive host metadata. A password may exist only in this
    // process's cache; otherwise the session uses an SSH agent/default key.
    QSettings settings;
    const QJsonArray hosts = forkmesh::control::loadSavedHosts(
        settings, kHostsSetting, &m_hostSessionPasswords);
    struct Target { QString node, ip, user, pass; };
    QList<Target> targets;
    int skipped = 0;
    for (const QJsonValue &v : hosts) {
        const QJsonObject h = v.toObject();
        Target t{h.value("name").toString().trimmed(),
                 h.value("ip").toString().trimmed(),
                 h.value("user").toString().trimmed(),
                 QString()};
        t.pass = m_hostSessionPasswords.value(
            forkmesh::control::savedHostCredentialKey(
                t.node, t.ip, t.user));
        if (t.node.isEmpty() || t.ip.isEmpty() || t.user.isEmpty()) {
            ++skipped;
            continue;
        }
        targets.append(t);
    }
    if (targets.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "No saved host has complete node, address, and SSH username "
                "metadata."));
        return;
    }

    // Tear down any panes left from a previous parallel run.
    qDeleteAll(m_hostDeploySessions);
    m_hostDeploySessions.clear();
    while (QLayoutItem *item = m_hostDeployGrid->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const FleetDeployOptions options = fleetDeployOptions(mode);

    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    // Arrange the panes in a roughly square grid so many hosts stay readable.
    int cols = 1;
    while (cols * cols < targets.size())
        ++cols;

    for (int idx = 0; idx < targets.size(); ++idx) {
        const Target &t = targets.at(idx);
        auto *card = new QFrame;
        card->setObjectName("leaderboardCard");
        card->setFrameShape(QFrame::StyledPanel);
        auto *col = new QVBoxLayout(card);
        col->setContentsMargins(10, 8, 10, 8);
        col->setSpacing(6);

        auto *header = new QLabel(
            QString::fromUtf8("%1 \xE2\x80\x94 %2@%3 \xE2\x80\xA6")
                .arg(t.node, t.user, t.ip));
        QFont hf = header->font();
        hf.setBold(true);
        header->setFont(hf);
        header->setWordWrap(true);
        col->addWidget(header);

        auto *log = new QPlainTextEdit;
        log->setObjectName("actionLog");
        log->setReadOnly(true);
        log->setLineWrapMode(QPlainTextEdit::NoWrap);
        log->setMinimumHeight(200);
        log->setMinimumWidth(260);
        log->setFont(mono);
        col->addWidget(log, 1);

        m_hostDeployGrid->addWidget(card, idx / cols, idx % cols);

        auto *s = new HostDeploySession;
        s->node = t.node;
        s->ip = t.ip;
        s->user = t.user;
        s->pass = t.pass;
        s->log = log;
        s->header = header;
        m_hostDeploySessions.append(s);
    }

    m_hostDeployLabel->setText(
        QString::fromUtf8("Live output \xE2\x80\x94 %1 host(s) deploying in "
                          "parallel").arg(targets.size()));
    m_hostDeployLabel->setVisible(true);
    m_hostDeployPanel->setVisible(true);

    m_hostDeployRemaining = m_hostDeploySessions.size();
    m_hostDeployFailed = 0;
    if (m_hostInstallAllButton)
        m_hostInstallAllButton->setEnabled(false);
    if (m_hostReinstallAllButton)
        m_hostReinstallAllButton->setEnabled(false);
    if (m_hostUpdateAllSourceButton)
        m_hostUpdateAllSourceButton->setEnabled(false);
    if (m_hostInstallButton)
        m_hostInstallButton->setEnabled(false);
    if (m_hostInstallStatus)
        m_hostInstallStatus->setText(
            QString::fromUtf8("Deploying to %1 host(s) in parallel\xE2\x80\xA6%2")
                .arg(m_hostDeployRemaining)
                .arg(skipped ? QString::fromUtf8(" (%1 skipped \xE2\x80\x94 "
                                                 "incomplete metadata)").arg(skipped)
                             : QString()));

    // Snapshot the list first: startHostDeploySession may fail synchronously and
    // finish a session (mutating m_hostDeploySessions is not expected, but the
    // finished callback can fire re-entrantly), so iterate a stable copy.
    const QList<HostDeploySession *> toStart = m_hostDeploySessions;
    for (HostDeploySession *s : toStart)
        startHostDeploySession(s, options);
}

void MainWindow::startHostDeploySession(HostDeploySession *session,
                                        const FleetDeployOptions &options)
{
    if (!session)
        return;

    QString remoteCmd;
    QByteArray uploadBytes;
    QString buildErr;
    if (!buildHostInstallCommand(session->ip, session->user, session->node,
                                 options.uploadBinary, options.reinstall,
                                 options.fromSource,
                                 options.requirePublishedBinary, &remoteCmd,
                                 &uploadBytes, &buildErr)) {
        appendHostDeployLog(session,
                            QString::fromUtf8("\n\xE2\x9C\x98 %1\n").arg(buildErr));
        onHostDeploySessionFinished(session, false);
        return;
    }
    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            session->ip, session->user, session->pass, remoteCmd, &sshError,
            savedHostIdentityFile(session->node, session->ip, session->user));
    if (ssh.program.isEmpty()) {
        appendHostDeployLog(
            session, QString::fromUtf8("\n\xE2\x9C\x98 %1\n").arg(sshError));
        onHostDeploySessionFinished(session, false);
        return;
    }

    const bool needSudo = session->user != QStringLiteral("root");
    appendHostDeployLog(
        session, QString::fromUtf8("$ ssh %1@%2 \xE2\x80\xA6\nConnecting and "
                                   "running the installer\xE2\x80\xA6\n\n")
                     .arg(session->user, session->ip));
    if (options.requirePublishedBinary)
        appendHostDeployLog(
            session,
            QStringLiteral("Installing the published ForkMesh v"
                           FORKMESH_VERSION
                           " binary; the host will verify its release checksum, "
                           "reported version and exact source commit.\n"));
    else if (options.uploadBinary && !options.fromSource)
        appendHostDeployLog(
            session, QString::fromUtf8("Uploading this app's release binary "
                                       "(%1 MB) over the SSH session\xE2\x80\xA6\n")
                         .arg(QString::number(uploadBytes.size() /
                                                  (1024.0 * 1024.0),
                                              'f', 1)));

    auto *proc = new QProcess(this);
    session->proc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProcessEnvironment(ssh.environment);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, session, proc] {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        appendHostDeployLog(session, chunk);
        // Watch this host's stream for the installer's "FORKMESH LINK CODE:
        // NNNNNN" line (through a rolling tail so a code split across chunks
        // still matches) and auto-link the fresh node to this account, once.
        if (session->linkPrompted)
            return;
        session->linkTail = (session->linkTail + chunk).right(512);
        static const QRegularExpression linkRe(
            QStringLiteral("FORKMESH LINK CODE:\\s*([0-9]{6})"));
        const QRegularExpressionMatch m = linkRe.match(session->linkTail);
        if (!m.hasMatch())
            return;
        session->linkPrompted = true;
        QString linkUser;
        const QString signer = hostLinkSigningAccountName(&linkUser);
        if (!signer.isEmpty()) {
            appendHostDeployLog(
                session, QString::fromUtf8("\nLinking node to your user account "
                                           "(%1)\xE2\x80\xA6\n").arg(linkUser));
            submitHostLinkCode(m.captured(1));
        } else {
            appendHostDeployLog(
                session, QString::fromUtf8(
                             "\n[warning] Not auto-linking: this app is not "
                             "linked to a user account.\n"));
        }
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, session](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart)
                    return;
                // A failed start never emits finished(), so finalize the
                // session here or the fleet would wait on it forever.
                appendHostDeployLog(
                    session,
                    QString::fromUtf8(
                        "\n[error] Could not start SSH. Install OpenSSH (and "
                        "sshpass only when using password login).\n"));
                if (session->proc) {
                    session->proc->deleteLater();
                    session->proc = nullptr;
                }
                onHostDeploySessionFinished(session, false);
            });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, session](int code, QProcess::ExitStatus status) {
                const bool ok = status == QProcess::NormalExit && code == 0;
                if (ok)
                    appendHostDeployLog(
                        session, QString::fromUtf8(
                                     "\n\xE2\x9C\x94 Finished.\n"));
                else
                    appendHostDeployLog(
                        session, QString::fromUtf8(
                                     "\n\xE2\x9C\x98 Failed (exit %1).\n")
                                     .arg(code));
                if (session->proc) {
                    session->proc->deleteLater();
                    session->proc = nullptr;
                }
                onHostDeploySessionFinished(session, ok);
            });

    proc->start(ssh.program, ssh.arguments);
    if (needSudo)
        proc->write((session->pass + QStringLiteral("\n")).toUtf8());
    if (options.uploadBinary && !options.fromSource) {
        proc->write((kHostUploadMarker + QStringLiteral("\n")).toUtf8());
        proc->write(uploadBytes);
    }
    proc->closeWriteChannel();
}

void MainWindow::onHostDeploySessionFinished(HostDeploySession *session, bool ok)
{
    if (!session || session->finished)
        return;
    session->finished = true;
    if (!ok)
        ++m_hostDeployFailed;
    // Persist this host's outcome so the hosts table reflects it.
    rememberHost(session->node, session->ip, session->user, session->pass,
                 ok ? QStringLiteral("installed")
                    : QStringLiteral("install failed"));
    if (session->header) {
        const QString glyph = ok ? QString::fromUtf8("\xE2\x9C\x94")
                                 : QString::fromUtf8("\xE2\x9C\x98");
        session->header->setText(
            QString::fromUtf8("%1 %2 \xE2\x80\x94 %3@%4")
                .arg(glyph, session->node, session->user, session->ip));
    }
    if (m_hostDeployRemaining > 0)
        --m_hostDeployRemaining;
    if (m_hostDeployRemaining == 0) {
        if (m_hostInstallAllButton)
            m_hostInstallAllButton->setEnabled(true);
        if (m_hostReinstallAllButton)
            m_hostReinstallAllButton->setEnabled(true);
        if (m_hostUpdateAllSourceButton)
            m_hostUpdateAllSourceButton->setEnabled(true);
        if (m_hostInstallButton)
            m_hostInstallButton->setEnabled(true);
        if (m_hostInstallStatus) {
            const int total = m_hostDeploySessions.size();
            m_hostInstallStatus->setText(
                m_hostDeployFailed == 0
                    ? QString::fromUtf8("\xE2\x9C\x94 Finished deploying to all "
                                        "%1 host(s).").arg(total)
                    : QString::fromUtf8("Finished deploying to %1 host(s) "
                                        "\xE2\x80\x94 %2 failed (see panes "
                                        "above).").arg(total)
                          .arg(m_hostDeployFailed));
        }
    } else if (m_hostInstallStatus) {
        const int total = m_hostDeploySessions.size();
        m_hostInstallStatus->setText(
            QString::fromUtf8("Deploying\xE2\x80\xA6 %1/%2 done%3")
                .arg(total - m_hostDeployRemaining).arg(total)
                .arg(m_hostDeployFailed
                         ? QString::fromUtf8(", %1 failed").arg(m_hostDeployFailed)
                         : QString()));
    }
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
    if (ip.isEmpty() || user.isEmpty() || node.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(QString::fromUtf8(
                "Enter the host IP, SSH username and a node name first. "
                "Leave the password blank to use your SSH agent/default key."));
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

    QString sshError;
    const forkmesh::control::HostSshCommand ssh =
        forkmesh::control::buildHostSshCommand(
            ip, user, pass, remoteCmd, &sshError,
            savedHostIdentityFile(node, ip, user));
    if (ssh.program.isEmpty()) {
        if (m_hostInstallStatus)
            m_hostInstallStatus->setText(sshError);
        return;
    }

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
    proc->setProcessEnvironment(ssh.environment);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        appendHostInstallLog(QString::fromUtf8(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            appendHostInstallLog(QString::fromUtf8(
                "\n[error] Could not start SSH. Install OpenSSH (and sshpass "
                "only when using password login) and try again.\n"));
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

    proc->start(ssh.program, ssh.arguments);
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
    QString linkUser;
    if (hostLinkSigningAccountName(&linkUser).isEmpty())
        return;
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Link new node to your account"));
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(
        QStringLiteral("The machine being installed shows a link code. Confirm "
                       "it below to register the new node under your account "
                       "(<b>%1</b>).").arg(linkUser.toHtmlEscaped()));
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
    QString linkUser;
    const QString signer = hostLinkSigningAccountName(&linkUser);
    if (signer.isEmpty())
        return;
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical =
        ("forkmesh-link-v1\n" + signer + "\n" + code + "\n" + ts).toUtf8();
    const QJsonObject body{{"nodeName", signer},
                           {"code", code},
                           {"ts", ts},
                           {"sig", m_profileIdentity.signData(canonical)}};
    QNetworkRequest request(accountsApiUrl("link-node"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    QNetworkReply *reply = m_networkAccess->post(
        request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, linkUser]() {
        const QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        QString message;
        if (resp.value(QStringLiteral("linked")).toBool()) {
            message = QString::fromUtf8(
                "\n\xE2\x9C\x94 Node \"%1\" is now linked to user \"%2\".\n")
                .arg(resp.value(QStringLiteral("node")).toString(),
                     resp.value(QStringLiteral("user")).toString(linkUser));
        } else if (resp.value(QStringLiteral("pending")).toBool()) {
            message = QString::fromUtf8(
                "\n\xE2\x9C\x94 Link code accepted; the new node will be linked "
                "to user \"%1\" as soon as it registers.\n")
                .arg(resp.value(QStringLiteral("user")).toString(linkUser));
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
    // Building every repository tab creates several large tables, editors and
    // delegates. On a cold launch that work used to consume more than a second
    // before the first frame even when no repository was selected. Keep Home
    // lightweight and build the full detail tree on the first real repo open.
    auto *empty = new QLabel(
        QStringLiteral("Select a repository to explore its code and activity."));
    empty->setObjectName(QStringLiteral("emptyState"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_repoDetailSection = empty;
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
    m_nodeProfileSectionHost = page;
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
    // panel rendered ~450px wide and clipped the "THIS MACHINE" action row
    // (Logout), the Node ID key, the balance button and the verify-wallet
    // button on the right. Give both columns real room so everything shows —
    // wide enough that the nodes list can carry full per-node detail rows.
    scroll->setMinimumWidth(920);
    scroll->setMaximumWidth(1440);
    m_nodeProfilePanel = scroll;

    auto *content = new QWidget;
    content->setObjectName("nodeProfileContent");

    auto *closeButton = new QPushButton(QString());
    closeButton->setObjectName("ghostButton");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip("Close");
    setOcticon(closeButton, "x", 16);
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::hideNodeProfile);
    // Meaningless while the panel is a Settings tab (there's nothing to close
    // back out of), so hostNodeProfilePanel() hides it there.
    m_profileCloseButton = closeButton;
    auto *titleLabel = new QLabel("User profile");
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

    // --- Mirror reward settings: opt-in public payout configuration under the
    // username on your own profile. It does not create a wallet or promise that
    // an eligible node will be selected.
    m_profileGetPaidButton = new QPushButton(
        QString::fromUtf8("Mirror reward settings"));
    m_profileGetPaidButton->setObjectName("primaryButton");
    m_profileGetPaidButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileGetPaidButton, "credit-card", 16);
    m_profileGetPaidButton->setToolTip(
        "Configure a public self-custodial payout address. A healthy mirror may "
        "be eligible for voluntary community rewards; selection and payment are "
        "not guaranteed. Cloning, mirroring, issues and PRs work without it.");
    connect(m_profileGetPaidButton, &QPushButton::clicked, this,
            &MainWindow::enablePaidMirroring);

    // --- Node power switch: a plain on/off toggle sitting under the reward
    // settings, since that's exactly what it controls — whether this whole
    // node is online and publishing eligibility signals, or parked offline. Used to
    // be a small pill in the top-right nav cluster; moved here so it reads as
    // the node's power switch rather than a stray status badge.
    m_profileOnlineSection = new QWidget;
    auto *onlineSectionLabel = makeProfileSection("THIS MACHINE'S POWER SWITCH");
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
        QStringLiteral("How long this machine has been online this session"));
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
    // "THIS MACHINE", not "THIS NODE": on your own profile you're a user, and
    // users are not nodes — these actions just happen to act on the machine
    // the app is running on.
    auto *selfLabel = makeProfileSection("THIS MACHINE");
    m_profileRebuildButton = makeProfileActionButton(
        "sync", "Rebuild",
        "Rebuild from the local source checkout and relaunch (fast; no update)");
    connect(m_profileRebuildButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_profileRebuildButton); quickRebuildRestart(); });
    m_profileUpdateButton = makeProfileActionButton(
        "download", "Update", "Pull the latest source, then rebuild and relaunch");
    connect(m_profileUpdateButton, &QPushButton::clicked, this,
            [this] { startRestartSpin(m_profileUpdateButton); updateRebuildRestart(); });
    auto *selfAddRepoButton = makeProfileActionButton(
        "file-directory", "Add repo",
        "Choose a local Git repository on this computer, mirror it, and publish "
        "it as public or private");
    connect(selfAddRepoButton, &QPushButton::clicked, this,
            &MainWindow::promptAddRepository);
    auto *selfSettingsButton = makeProfileActionButton("gear", "Settings",
                                                        "Open settings");
    connect(selfSettingsButton, &QPushButton::clicked, this,
            [this] { showSection(1); });
    // Two buttons in the app said only "Logout"/"Log out" while doing very
    // different things. This one disconnects the mesh session and goes back to
    // the setup screen; the account stays signed in on this machine. Settings
    // holds the other one, which signs the account out. Name each for what it
    // actually does (adhoc #63).
    auto *selfLogoutButton = makeProfileActionButton(
        "sign-out", "Disconnect",
        "Disconnect this machine from the mesh and return to the setup "
        "screen. Your ForkMesh account stays signed in here \xE2\x80\x94 to "
        "sign the account out, use Settings \xE2\x80\xBA \"Log out of "
        "account\".");
    connect(selfLogoutButton, &QPushButton::clicked, this,
            [this] { leaveSession(); });
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(6);
    actionRow->addWidget(m_profileRebuildButton);
    actionRow->addWidget(m_profileUpdateButton);
    actionRow->addWidget(selfAddRepoButton);
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

    // --- User profile: shows this node's user account and every node linked to
    // it (adhoc #177). refreshProfileAccountStatus() populates m_profileUserNodesList
    // and drives the shared link-state side effects (m_profileLinkBrowserButton,
    // updateUserSwitcher()). Added to the right column below and shown on your own
    // profile only.
    m_profileAccountSection = new QWidget;
    m_profileAccountSection->setObjectName("profileUserCard");
    // Header flips between "NODES (n)" (linked: just the fleet, no chrome) and
    // "USER ACCOUNT" (unlinked: link-state text + login button). The old
    // always-on "USER PROFILE" label + status boxes ate half the card before
    // the first node row; renderProfileAccountStatus now hides both once the
    // account is linked so the card is just the nodes.
    m_profileAccountLabel = makeProfileSection("NODES");
    m_profileAccountStatus = new QLabel;
    m_profileAccountStatus->setObjectName("statusLine");
    m_profileAccountStatus->setWordWrap(true);
    m_profileAccountStatus->setTextFormat(Qt::RichText);
    m_profileUserNodesList = new QListWidget;
    m_profileUserNodesList->setObjectName("profileNodeList");
    m_profileUserNodesList->setIconSize(QSize(32, 32));
    m_profileUserNodesList->setSelectionMode(QAbstractItemView::NoSelection);
    m_profileUserNodesList->setFocusPolicy(Qt::NoFocus);
    m_profileUserNodesList->setFrameShape(QFrame::NoFrame);
    m_profileUserNodesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_profileUserNodesList->setCursor(Qt::PointingHandCursor);
    m_profileUserNodesList->setToolTip(
        QStringLiteral("Nodes owned by this user account. Click one to open "
                       "its node profile."));
    // Rows carry the real node name in UserRole (the display text may have a
    // "(this machine)" marker); clicking opens that node's own profile.
    connect(m_profileUserNodesList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const QString node = item->data(Qt::UserRole).toString();
                if (!node.isEmpty())
                    showNodeProfile(QString(), node);
            });
    m_profileLinkUserButton = new QPushButton("Log in as a user");
    m_profileLinkUserButton->setObjectName("ghostButton");
    m_profileLinkUserButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileLinkUserButton, "person", 14);
    m_profileLinkUserButton->setToolTip(
        "Link this machine's node to your user account by signing in. One user "
        "can own many nodes.");
    connect(m_profileLinkUserButton, &QPushButton::clicked, this,
            &MainWindow::promptLinkNodeToUser);
    auto *accountLayout = new QVBoxLayout(m_profileAccountSection);
    accountLayout->setContentsMargins(10, 10, 10, 10);
    accountLayout->setSpacing(6);
    accountLayout->addWidget(m_profileAccountLabel);
    accountLayout->addWidget(m_profileAccountStatus);
    accountLayout->addWidget(m_profileUserNodesList);
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
        new QPushButton("Link this machine to your account");
    m_profileLinkBrowserButton->setObjectName("ghostButton");
    m_profileLinkBrowserButton->setCursor(Qt::PointingHandCursor);
    setOcticon(m_profileLinkBrowserButton, "link", 14);
    m_profileLinkBrowserButton->setToolTip(
        "Open forkmesh.com in your browser and attach this machine's node to "
        "the user account you're logged in as there. One user can own many "
        "nodes.");
    connect(m_profileLinkBrowserButton, &QPushButton::clicked, this,
            &MainWindow::openLinkNodeInBrowser);

    // --- Solana section: address, QR, on-demand balance.
    m_profileSolanaSection = new QWidget;
    auto *solanaLabel = makeProfileSection("SOLANA");
    auto *custodyNotice = new QLabel(
        "Non-custodial payout address \xE2\x80\x94 ForkMesh reads the public "
        "address and on-chain balance only. Private keys stay in the external "
        "wallet you control.");
    custodyNotice->setObjectName("statusLine");
    custodyNotice->setWordWrap(true);
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
    solanaLayout->addWidget(custodyNotice);
    solanaLayout->addWidget(m_profileSolanaAddr);
    solanaLayout->addWidget(copyAddr, 0, Qt::AlignLeft);
    solanaLayout->addWidget(m_profileQr, 0, Qt::AlignCenter);
    auto *balLabel = makeProfileSection("BALANCE");
    solanaLayout->addWidget(balLabel);
    solanaLayout->addLayout(balanceRow);

    // Public reward configuration (self only). A balance or deposit is neither
    // requested nor treated as proof of wallet custody.
    m_profileEligibility = new QLabel;
    m_profileEligibility->setObjectName("statusLine");
    m_profileEligibility->setWordWrap(true);
    m_profileEligibility->setTextFormat(Qt::RichText);
    m_profileVerifyButton = new QPushButton("Check reward settings");
    m_profileVerifyButton->setToolTip(
        "Validate the public payout address and publish the existing signed "
        "heartbeat. This never requests a deposit or guarantees a reward.");
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
    rightColumn->addWidget(m_profileAccountSection);
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

void MainWindow::hostNodeProfilePanel(bool inSettings)
{
    // One panel, two homes: the centered full page (section 10, used for any
    // node's profile) and the Settings > Profile tab (your own node). Move it
    // rather than building a second copy — the panel owns all the m_profile*
    // widgets, so a second build would orphan the first one's state.
    if (!m_nodeProfilePanel)
        return;
    QWidget *host = inSettings ? m_settingsProfileHost : m_nodeProfileSectionHost;
    if (!host || m_nodeProfilePanel->parentWidget() == host)
        return;
    auto *layout = qobject_cast<QBoxLayout *>(host->layout());
    if (!layout)
        return;
    if (QWidget *old = m_nodeProfilePanel->parentWidget()) {
        if (old->layout())
            old->layout()->removeWidget(m_nodeProfilePanel);
    }
    if (inSettings) {
        // Fill the tab: the tab body is already narrower than the section page,
        // and the panel's own size hint collapses to ~450px without a stretch.
        m_nodeProfilePanel->setMinimumWidth(0);
        layout->addWidget(m_nodeProfilePanel, 1);
    } else {
        m_nodeProfilePanel->setMinimumWidth(920);
        layout->insertWidget(1, m_nodeProfilePanel, 0); // between the stretchers
    }
    if (m_profileCloseButton)
        m_profileCloseButton->setVisible(!inSettings);
    m_nodeProfilePanel->show();
}

void MainWindow::syncSettingsProfileTab()
{
    if (!m_settingsTabs || m_profileSettingsTabIndex < 0 ||
        m_settingsTabs->currentIndex() != m_profileSettingsTabIndex)
        return;
    ensureSectionBuilt(10); // the panel is built with the profile section
    hostNodeProfilePanel(true);
    showNodeProfile(m_profileIdentity.publicKey(), topBarUserName(),
                    /*navigate=*/false);
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
        src = nodeMachineFavicon(m_profileNodeName, 96);
    m_profileAvatar->setPixmap(roundedRectPixmap(src, side, 20));
}

void MainWindow::showNodeProfile(const QString &nodeId, const QString &nodeName,
                                 bool navigate)
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

    // Full-width avatar banner: real avatar if we have one, else a generated
    // machine tile for the node.
    QPixmap avatar = m_avatars.value(info.id);
    if (avatar.isNull())
        avatar = nodeMachineFavicon(info.id.isEmpty() ? info.name : info.id, 96);
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
        // Self only: the "USER PROFILE" card lists this node's user account and
        // every node linked to it (adhoc #177). refreshProfileAccountStatus()
        // populates the list and drives its side effects on other widgets.
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

    // Reward settings are self-only. Once an active, locally signable account
    // and public payout address are configured, the label reports configuration
    // only—not selection, a transfer, or guaranteed earnings.
    if (m_profileGetPaidButton) {
        const bool earning = hasOwnerSigningCapability(accountOwner()) &&
                             !solana.isEmpty();
        m_profileGetPaidButton->setVisible(info.self);
        m_profileGetPaidButton->setText(
            earning
                ? QString::fromUtf8(
                      "\xE2\x9C\x93 Reward eligibility configured")
                : QString::fromUtf8("Mirror reward settings"));
    }

    // Wallet verification + eligibility badge are shown only on your own profile.
    if (m_profileVerifyButton)
        m_profileVerifyButton->setVisible(info.self);
    if (m_profileEligibility) {
        m_profileEligibility->setVisible(info.self);
        if (info.self)
            m_profileEligibility->setText(
                m_accountSolanaVerified
                    ? QString::fromUtf8(
                          "<span style='color:#3fb950'>Reward eligibility "
                          "configured \xC2\xB7 may be eligible</span>")
                    : QString::fromUtf8(
                          "<span style='color:#d29922'>Not yet eligible "
                          "\xE2\x80\x94 configure and verify a public "
                          "self-custodial address.</span>"));
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

    // Show the profile as its own full page (section 10 in m_sectionStack).
    // navigate=false leaves it where it is — the Settings > Profile tab.
    if (navigate) {
        hostNodeProfilePanel(false);
        showSection(10);
    }
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
// marking the machine we're viewing from ("(this machine)") so the fleet is
// legible. Matched by machineNodeName() — the username is never a node.
QString MainWindow::linkedNodesHtml() const
{
    if (m_profileLinkedNodes.isEmpty())
        return QString();
    const QString machine = machineNodeName();
    QStringList parts;
    for (const QString &n : m_profileLinkedNodes) {
        QString label = QStringLiteral("<b>%1</b>").arg(n.toHtmlEscaped());
        if (n.compare(machine, Qt::CaseInsensitive) == 0)
            label += QString::fromUtf8(" <span style='color:#8b949e'>(this "
                                       "machine)</span>");
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
    QString userName = m_nodeOwnerUser.trimmed().toLower();
    if (userName.isEmpty() && (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty()))
        userName = accountOwner().trimmed().toLower();
    const bool hasUserProfile = !userName.isEmpty();
    int nodeCount = 0;
    if (m_profileUserNodesList) {
        QStringList nodes = m_profileLinkedNodes;
        // THIS machine is one of the user's nodes too — listed under its own
        // machine node name, never under the username (users are not nodes;
        // the old code prepended accountOwner() here, which is how "jett" got
        // labelled "(this node)").
        const QString machine = machineNodeName();
        if (hasUserProfile && !machine.isEmpty() &&
            !nodes.contains(machine, Qt::CaseInsensitive))
            nodes.prepend(machine);
        nodeCount = nodes.size();

        // Rich per-node rows: join each name against the same sources the Nodes
        // directory table uses — the chat roster (platform / version / mirrors,
        // via nodeListIdentityKey) and the relay's canonical online set — plus
        // the node dropdown's repo counts.
        auto rosterInfo = [this](const QString &name) -> MemberInfo {
            MemberInfo best;
            bool found = false;
            for (const MemberInfo &m : std::as_const(m_homeRoster)) {
                if (nodeListIdentityKey(m) != name)
                    continue;
                if (!found || (m.online && !best.online)) {
                    best = m;
                    found = true;
                    continue;
                }
                if (best.version.trimmed().isEmpty())
                    best.version = m.version;
                if (best.platform.trimmed().isEmpty())
                    best.platform = m.platform;
                if (best.mirrors.isEmpty())
                    best.mirrors = m.mirrors;
            }
            return best;
        };
        const QString dot = QString::fromUtf8(" \xC2\xB7 ");
        m_profileUserNodesList->clear();
        for (const QString &nodeName : std::as_const(nodes)) {
            const MemberInfo mi = rosterInfo(nodeName);
            const NodeMenuEntry *menu = nullptr;
            for (const NodeMenuEntry &e : std::as_const(m_nodeMenuEntries)) {
                if (e.name.compare(nodeName, Qt::CaseInsensitive) == 0) {
                    menu = &e;
                    break;
                }
            }
            const bool isThisMachine =
                nodeName.compare(machine, Qt::CaseInsensitive) == 0;
            // Same online precedence as refreshNodesTable: this machine trusts
            // the local backend; otherwise the relay set once fetched; before
            // that, either signal.
            const bool rosterLive = mi.online || (menu && menu->online);
            const bool relayLive =
                m_relayOnlineNodes.contains(nodeName.trimmed().toLower());
            const bool online = isThisMachine
                                    ? (m_backend != nullptr)
                                    : (m_relayOnlineNodesFetched
                                           ? relayLive
                                           : (rosterLive || relayLive));

            QStringList meta;
            meta << (online ? QStringLiteral("Online") : QStringLiteral("Offline"));
            const QString platform =
                mi.platform.trimmed().isEmpty() && menu ? menu->platform
                                                        : mi.platform.trimmed();
            if (!platform.isEmpty())
                meta << platform;
            if (!mi.version.trimmed().isEmpty())
                meta << QStringLiteral("v%1").arg(mi.version.trimmed());
            if (menu && menu->repoCount > 0)
                meta << QStringLiteral("%1 repo%2")
                            .arg(menu->repoCount)
                            .arg(menu->repoCount == 1 ? QString()
                                                      : QStringLiteral("s"));
            if (!mi.mirrors.isEmpty())
                meta << QStringLiteral("%1 mirror%2")
                            .arg(mi.mirrors.size())
                            .arg(mi.mirrors.size() == 1 ? QString()
                                                        : QStringLiteral("s"));

            auto *item = new QListWidgetItem;
            item->setIcon(QIcon(
                roundedRectPixmap(nodeMachineFavicon(nodeName, 32), 32, 8)));
            item->setText(
                nodeName +
                (isThisMachine ? QStringLiteral("  (this machine)") : QString()) +
                QLatin1Char('\n') + meta.join(dot));
            item->setData(Qt::UserRole, nodeName);
            item->setToolTip(QStringLiteral("%1%2\nOwned by %3 \xE2\x80\x94 "
                                            "click to open this node's profile")
                                 .arg(nodeName,
                                      isThisMachine
                                          ? QStringLiteral(" (this machine)")
                                          : QString(),
                                      userName.isEmpty()
                                          ? QStringLiteral("this account")
                                          : userName));
            m_profileUserNodesList->addItem(item);
        }
        const bool showNodes = !nodes.isEmpty();
        m_profileUserNodesList->setVisible(showNodes);
        if (showNodes)
            m_profileUserNodesList->setFixedHeight(
                qMin(324, qMax(60, nodes.size() * 52 + 8)));
    }
    if (!m_nodeOwnerUser.trimmed().isEmpty()) {
        // A child node attached to a separate user account: slim one-liner, the
        // list itself carries the fleet.
        if (m_profileAccountLabel)
            m_profileAccountLabel->setText(
                QStringLiteral("NODES (%1)").arg(nodeCount));
        m_profileAccountStatus->setText(
            QString::fromUtf8(
                "<span style='color:#3fb950'>\xE2\x9C\x94 Linked to user "
                "<b>%1</b></span>")
                .arg(m_nodeOwnerUser.toHtmlEscaped()));
        m_profileAccountStatus->setVisible(true);
        m_profileLinkUserButton->setText("Linked to a user");
        m_profileLinkUserButton->setVisible(false);
    } else if (m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty()) {
        // This account is itself a user: the card is just the node fleet under a
        // "NODES (n)" header. The old "✔ This is your user account" status box
        // (and the USER PROFILE header above it) used to push the actual nodes
        // half a card down; the fleet list needs no preamble.
        if (m_profileAccountLabel)
            m_profileAccountLabel->setText(
                QStringLiteral("NODES (%1)").arg(nodeCount));
        if (m_profileLinkedNodes.isEmpty()) {
            m_profileAccountStatus->setText(QString::fromUtf8(
                "No other nodes are linked yet \xE2\x80\x94 open "
                "another node's app and use \"Log in as a user\" or "
                "\"Link this node to your account\" there to attach it "
                "to this account."));
            m_profileAccountStatus->setVisible(true);
        } else {
            m_profileAccountStatus->clear();
            m_profileAccountStatus->setVisible(false);
        }
        m_profileLinkUserButton->setVisible(false);
    } else {
        // While a browser link grant is being watched (adhoc #120), say so
        // instead of "isn't linked yet" — the repaint on every poll would
        // otherwise clobber the context of what the user just started.
        if (m_profileAccountLabel)
            m_profileAccountLabel->setText(QStringLiteral("USER ACCOUNT"));
        m_profileAccountStatus->setText(
            m_linkGrantPollsLeft > 0
                ? QString::fromUtf8(
                      "Finishing in your browser \xE2\x80\xA6 this machine's "
                      "node will be attached to the user logged in on the "
                      "website.")
                : QString::fromUtf8(
                      "This machine's node isn't linked to a user account yet. "
                      "One user can own many nodes \xE2\x80\x94 log in to "
                      "attach it."));
        m_profileAccountStatus->setVisible(true);
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
    if (node.isEmpty() || !hasOwnerSigningCapability(node)) {
        m_nodeOwnerUser.clear();
        m_profileLinkedNodes.clear();
        m_profileIsUserAccount = false;
        updateUserSwitcher();
        if (m_profileAccountLabel)
            m_profileAccountLabel->setText(QStringLiteral("USER ACCOUNT"));
        if (m_profileUserNodesList) {
            m_profileUserNodesList->clear();
            m_profileUserNodesList->setVisible(false);
        }
        m_profileAccountStatus->setText(QString::fromUtf8(
            "Register this machine's node first (see Account settings) to "
            "link it to a user account."));
        m_profileAccountStatus->setVisible(true);
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
        cacheWebUserSolanaProfile(node, resp);
        m_profileLinkedNodes =
            profileNodesFromJson(resp.value(QStringLiteral("nodes")));
        updateUserSwitcher();
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
        if (resp.value(QStringLiteral("exists")).toBool()) {
            cacheWebUserSolanaProfile(
                resp.value(QStringLiteral("name")).toString(), resp);
            m_profileLinkedNodes =
                profileNodesFromJson(resp.value(QStringLiteral("nodes")));
        }
        renderProfileAccountStatus();
    });
}

void MainWindow::promptLinkNodeToUser()
{
    if (accountOwner().isEmpty() ||
        !hasOwnerSigningCapability(accountOwner())) {
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
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        !m_profileIdentity.isValid())
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
            updateUserSwitcher();
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
    if (node.isEmpty() || !hasOwnerSigningCapability(node) ||
        (!m_profileIdentity.isValid() && !m_profileIdentity.load())) {
        logSystem("Register this node first (see Account settings) to "
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
    updateFooterCommitInfo();
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
    if (m_coveExplorerTabs) {
        m_coveExplorerTabs->clear();
        m_openCoveExplorerTabs.clear();
    }
    if (m_coveExplorerTree)
        m_coveExplorerTree->clear();
    if (m_coveExplorerSelector)
        m_coveExplorerSelector->clear();
    m_coveExplorerCoves.clear();
    m_coveExplorerCurrentId.clear();
    if (m_commitBar)
        m_commitBar->setText(
            "<span style='color:#8b949e'>This node has no repositories.</span>");
    if (m_repoDetailStack)
        m_repoDetailStack->setCurrentIndex(0); // Code/overview
    if (m_filesStack)
        m_filesStack->setCurrentIndex(0);
    updateBreadcrumb();
}
