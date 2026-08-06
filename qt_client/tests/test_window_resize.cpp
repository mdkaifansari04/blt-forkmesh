#include "../src/MainWindow.h"
#include "../src/MainWindowInternal.h"
#include "../src/BackgroundActivity.h"
#include "../src/ClaudeTranscriptView.h"
#include "../src/LogTimelineChart.h"
#include "../src/PlatformLogFilter.h"
#include "ForkMeshVersion.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QCheckBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QDateTimeEdit>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QImage>
#include <QPixmap>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSemaphore>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QWidget>

#include <algorithm>
#include <atomic>

// Free helpers from MainWindowInternal.h (forkmesh::ui), linked into the
// window test via ${FORKMESH_APP_SOURCES}. Both are `inline`, so a forward
// declaration is enough to link against the definition already odr-used (and
// thus emitted) from MainWindowAgents.cpp/MainWindowShared.cpp.
namespace forkmesh {
namespace ui {
QString linkifyIssueRefs(const QString &escaped);
bool isTemporaryChatGuest(const MemberInfo &m);
QString agentModelLabel(const QString &model);
bool agentModelIsClaudeStyle(const QString &model);
bool agentModelMatchesProvider(const QString &provider, const QString &model);
MirrorBranchTip mirrorPrimaryBranchTip(const QString &mirrorPath,
                                       const QString &workTree);
QString gitTimeoutError(QProcess &process, int waitedMs);
bool isTransientGitError(const QString &err);
QString branchDiffErrorHtml(const QString &branch, const QString &err,
                            int attempts);
}
} // namespace forkmesh

namespace {

int failures = 0;

// Sink for the propagateSizeHints filter test: messages the platform log filter
// forwards (i.e. does not drop) land here so the test can assert what survived.
QStringList *g_capturedMessages = nullptr;
void captureMessages(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (g_capturedMessages)
        g_capturedMessages->append(message);
}

void check(bool condition, const QString &what)
{
    if (condition) {
        qInfo("PASS: %s", qPrintable(what));
    } else {
        qCritical("FAIL: %s", qPrintable(what));
        ++failures;
    }
}

bool tryAcquireWithEvents(QSemaphore &semaphore, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    do {
        if (semaphore.tryAcquire())
            return true;
        // Keep the real window responsive while waiting for the worker. A
        // blocking one-second QSemaphore wait falsely trips the application's
        // UI-stall watchdog and makes this functional test intermittently die
        // in its diagnostic signal handler.
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (timer.elapsed() < timeoutMs);
    return semaphore.tryAcquire();
}

bool iconContainsChromaKey(const QIcon &icon)
{
    const QImage image = icon.pixmap(QSize(32, 32)).toImage().convertToFormat(
        QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const bool magentaKey =
                pixel.red() > 235 && pixel.green() < 30 && pixel.blue() > 225;
            const bool greenKey =
                pixel.green() > 220 && pixel.red() < 45 && pixel.blue() < 45;
            if (pixel.alpha() > 32 && (magentaKey || greenKey))
                return true;
        }
    }
    return false;
}

void checkFooterOverlayGeometry(MainWindow &window)
{
    window.testShowHomeSection();
    window.testSetLogOverlayExpanded(false);
    QApplication::processEvents();

    auto *dock = window.findChild<QWidget *>(QStringLiteral("logDock"));
    auto *log = window.findChild<QWidget *>(QStringLiteral("footerLeftRegion"));
    auto *prompt = window.findChild<QWidget *>(QStringLiteral("promptOverlayHost"));
    auto *promptWrapper = window.findChild<QWidget *>(QStringLiteral("promptWrapper"));
    auto *avatar = window.findChild<QPushButton *>(QStringLiteral("serverFooterButton"));
    auto *lights = dynamic_cast<forkmesh::ui::LogActivityLights *>(
        window.findChild<QWidget *>(QStringLiteral("debugActivityLights")));
    auto *header = dynamic_cast<forkmesh::ui::LogActivityLights *>(
        window.findChild<QWidget *>(QStringLiteral("logActivityHeader")));
    auto *debugBar = window.findChild<QWidget *>(QStringLiteral("debugBar"));
    auto *version = window.findChild<QPushButton *>(
        QStringLiteral("statusVersionButton"));
    check(dock && log && prompt && lights && header && debugBar && version &&
              !log->isVisible() && !debugBar->isVisible() && lights->isDebug() &&
              lights->lightCount() == 31 &&
              prompt->geometry().center().x() > dock->rect().center().x() &&
              prompt->geometry().bottom() == dock->rect().bottom(),
          QStringLiteral("all 31 labeled log categories start collapsed in the "
                         "version-controlled debug bar"));
    if (version && debugBar) {
        version->click();
        QApplication::processEvents();
        check(debugBar->isVisible() && lights->isVisibleTo(debugBar) &&
                  window.findChild<QWidget *>(
                      QStringLiteral("debugResourceChart")) != nullptr,
              QStringLiteral("clicking the footer version reveals debug activity "
                             "and the enlarged four-resource chart"));

        const quint64 gitCountBefore = lights->countFor(QStringLiteral("GIT"));
        lights->pulse(QStringLiteral("GIT"));
        lights->pulse(QStringLiteral("GIT"));
        check(lights->countFor(QStringLiteral("GIT")) == gitCountBefore + 2,
              QStringLiteral("the debug row keeps per-category occurrence counts"));

        const qint64 minute = QDateTime::currentMSecsSinceEpoch() - 60000;
        const QJsonArray systems = {
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("website")},
                {QStringLiteral("label"), QStringLiteral("Website")},
                {QStringLiteral("minutes"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("minuteTs"), double(minute)},
                     {QStringLiteral("status"), QStringLiteral("operational")}}}}},
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("api")},
                {QStringLiteral("label"), QStringLiteral("API")},
                {QStringLiteral("minutes"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("minuteTs"), double(minute)},
                     {QStringLiteral("status"), QStringLiteral("down")}}}}},
        };
        check(window.testApplyFooterWebsiteStatusPayload(
                  QJsonObject{{QStringLiteral("ok"), true},
                              {QStringLiteral("now"), double(minute + 60000)},
                              {QStringLiteral("systems"), systems}}) &&
                  lights->websiteStatusCount() == 2 &&
                  lights->websiteStatusFor(QStringLiteral("website")) ==
                      QStringLiteral("operational") &&
                  lights->websiteStatusFor(QStringLiteral("api")) ==
                      QStringLiteral("down"),
              QStringLiteral("the debug row appends labeled green/red website "
                             "minute states"));

        // A Cloudflare-edge failure of /api/status itself (e.g. a 1101 worker
        // exception) must not leave the last-fetched green/red minutes on
        // screen forever as though nothing happened — the relay-reported rows
        // should go grey/unknown until a real payload is fetched again.
        window.testApplyFooterWebsiteStatusFailure(500);
        check(lights->websiteStatusCount() == 2 &&
                  lights->websiteStatusFor(QStringLiteral("website")) ==
                      QStringLiteral("unknown") &&
                  lights->websiteStatusFor(QStringLiteral("api")) ==
                      QStringLiteral("unknown"),
              QStringLiteral("an HTTP error from the status API itself marks "
                             "the relay-reported rows unknown instead of "
                             "keeping the stale cached minute"));

        // adhoc #1564: the relay grades itself from inside Cloudflare, so the
        // last two dots are checked here instead — the site and the /status
        // page loaded over the real public hostname. A Cloudflare edge failure
        // (520-527, a branded interstitial, a challenge) must land red/amber on
        // these rows even though every relay-reported row above is green.
        const QByteArray homepage =
            "<!doctype html><html><head><title>ForkMesh</title></head>";
        const QByteArray statusPage =
            "<!doctype html><html><head><title>Status \xc2\xb7 ForkMesh</title>"
            "</head><body><h1 id=\"status-title\">Is ForkMesh working?</h1>";
        const QByteArray cloudflareError =
            "<!doctype html><html><head><title>forkmesh.com | 521: Web server "
            "is down</title></head><body>Cloudflare Ray ID: abc123</body>";
        check(window.testApplyDesktopWebsiteProbe(
                  QStringLiteral("desktop_website"), 200, homepage) ==
                      QStringLiteral("operational") &&
                  window.testApplyDesktopWebsiteProbe(
                      QStringLiteral("desktop_status_page"), 206, statusPage) ==
                      QStringLiteral("operational") &&
                  lights->websiteStatusCount() == 4 &&
                  lights->websiteStatusFor(QStringLiteral("desktop_website")) ==
                      QStringLiteral("operational"),
              QStringLiteral("the desktop-side site and /status checks append "
                             "two more dots to the relay's own rows"));

        check(window.testApplyDesktopWebsiteProbe(
                  QStringLiteral("desktop_website"), 521, cloudflareError) ==
                      QStringLiteral("down") &&
                  window.testApplyDesktopWebsiteProbe(
                      QStringLiteral("desktop_website"), 200, cloudflareError) ==
                      QStringLiteral("down") &&
                  window.testApplyDesktopWebsiteProbe(
                      QStringLiteral("desktop_website"), 429, homepage) ==
                      QStringLiteral("degraded") &&
                  lights->websiteStatusCount() == 4,
              QStringLiteral("a Cloudflare edge status, a branded error document "
                             "served as HTTP 200, and a rate-limit each fail the "
                             "website check without adding a row"));

        // A host with no link of its own reports grey rather than a false
        // outage, so an offline test machine is allowed that verdict here.
        const QString unreachable = window.testApplyDesktopWebsiteProbe(
            QStringLiteral("desktop_status_page"), 0, QByteArray(),
            QStringLiteral("Host not found"));
        check(window.testApplyDesktopWebsiteProbe(
                  QStringLiteral("desktop_status_page"), 500, QByteArray()) ==
                      QStringLiteral("down") &&
                  window.testApplyDesktopWebsiteProbe(
                      QStringLiteral("desktop_status_page"), 200, homepage) ==
                      QStringLiteral("degraded") &&
                  (unreachable == QStringLiteral("down") ||
                   unreachable == QStringLiteral("unknown")),
              QStringLiteral("the /status check fails on a server error, on a "
                             "document that isn't the status page, and when the "
                             "page cannot be fetched at all"));

        // Rebuild+restart came down from the window-chrome line and Resize came
        // out of the navigation rail: both now sit in the debug bar's own tool
        // cluster at the right edge, outside the scrolling category row, each
        // still carrying its small caption under its icon.
        auto *tools = window.findChild<QWidget *>(QStringLiteral("debugBarTools"));
        QPushButton *restartTool = nullptr;
        QPushButton *resizeTool = nullptr;
        QPushButton *logTool = nullptr;
        if (tools) {
            for (QPushButton *tool : tools->findChildren<QPushButton *>()) {
                if (tool->accessibleName() == QLatin1String("Restart"))
                    restartTool = tool;
                else if (tool->accessibleName() == QLatin1String("Resize"))
                    resizeTool = tool;
                else if (tool->accessibleName() == QLatin1String("Log"))
                    logTool = tool;
            }
        }
        check(tools && restartTool && resizeTool && logTool &&
                  tools->parentWidget() == debugBar &&
                  tools->mapTo(&window, QPoint(0, 0)).x() >
                      lights->mapTo(&window, QPoint(0, 0)).x(),
              QStringLiteral("restart, resize and the log tail are captioned "
                             "tools at the debug bar's right edge"));

        // The third tool grows the window by a five-line live tail rather than
        // taking those five lines out of the workspace.
        auto *tail = window.findChild<QPlainTextEdit *>(
            QStringLiteral("debugLogTail"));
        if (logTool && tail) {
            const int heightBefore = window.height();
            logTool->setChecked(true);
            QApplication::processEvents();
            const bool grew = window.isMaximized() || window.isFullScreen() ||
                              window.height() >= heightBefore + tail->height();
            check(tail->isVisible() && grew &&
                      tail->document()->maximumBlockCount() == 5,
                  QStringLiteral("the log tool expands the window with a live "
                                 "five-line log tail"));
            window.testSetFooterUpdateLine(QStringLiteral("2026-01-01 00:00:00 "
                                                          "tail line"));
            QApplication::processEvents();
            check(tail->toPlainText().endsWith(QStringLiteral("tail line")) &&
                      tail->document()->blockCount() <= 5,
                  QStringLiteral("new log lines stream into the tail, capped at "
                                 "five"));
            logTool->setChecked(false);
            QApplication::processEvents();
            check(!tail->isVisible() && window.height() <= heightBefore,
                  QStringLiteral("switching the log tool off gives the window "
                                 "its height back"));
        }
    }

    // The prompt avatar is the lower-right launcher: clicking it collapses the
    // composer to the circular avatar, and hovering that avatar opens it again.
    check(promptWrapper && avatar && promptWrapper->isVisible() &&
              avatar->parentWidget() == prompt && prompt->width() > avatar->width(),
          QStringLiteral("prompt starts expanded with its avatar in the overlay"));
    if (avatar && promptWrapper && prompt && dock) {
        avatar->click();
        QApplication::processEvents();
        check(!promptWrapper->isVisible() && prompt->size() == QSize(34, 34) &&
                  prompt->geometry().right() >= dock->width() - 12,
              QStringLiteral("clicking the prompt avatar minimizes to the bottom-right circle"));

        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(avatar, &enter);
        QApplication::processEvents();
        check(promptWrapper->isVisible() && prompt->width() > avatar->width(),
              QStringLiteral("hovering the minimized avatar restores the prompt"));
    }

    // The composer is a movable panel (adhoc #1536): dragging its top strip
    // takes it off the footer anchor and onto the workspace, the corner grip
    // resizes it, the button pops it out into a window of its own, and
    // double-clicking the strip snaps everything back.
    auto *promptHandle =
        window.findChild<QWidget *>(QStringLiteral("promptDragHandle"));
    auto *promptGrip =
        window.findChild<QWidget *>(QStringLiteral("promptResizeGrip"));
    auto *promptDetach =
        window.findChild<QPushButton *>(QStringLiteral("promptDetachButton"));
    if (promptHandle && promptGrip && promptDetach && prompt && dock) {
        const auto sendMouse = [](QWidget *target, QEvent::Type type,
                                  const QPoint &global) {
            QMouseEvent event(type, target->mapFromGlobal(global),
                              QPointF(global), Qt::LeftButton,
                              type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                                 : Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        };
        // Global, not parent-relative: the drag reparents the panel out of the
        // dock, so the two geometries are not in the same coordinate space.
        const QPoint anchoredTopLeft = prompt->mapToGlobal(QPoint());
        const QPoint grabAt = promptHandle->mapToGlobal(
            QPoint(promptHandle->width() / 2, promptHandle->height() / 2));
        sendMouse(promptHandle, QEvent::MouseButtonPress, grabAt);
        sendMouse(promptHandle, QEvent::MouseMove, grabAt + QPoint(-120, -160));
        sendMouse(promptHandle, QEvent::MouseButtonRelease,
                  grabAt + QPoint(-120, -160));
        QApplication::processEvents();
        check(prompt->parentWidget() != dock &&
                  prompt->mapToGlobal(QPoint()).y() < anchoredTopLeft.y(),
              QStringLiteral("dragging the prompt's handle lifts it off the "
                             "footer anchor and onto the workspace"));

        const QSize dragged = prompt->size();
        const QPoint gripAt = promptGrip->mapToGlobal(
            QPoint(promptGrip->width() / 2, promptGrip->height() / 2));
        sendMouse(promptGrip, QEvent::MouseButtonPress, gripAt);
        sendMouse(promptGrip, QEvent::MouseMove, gripAt + QPoint(-60, -40));
        sendMouse(promptGrip, QEvent::MouseButtonRelease, gripAt + QPoint(-60, -40));
        QApplication::processEvents();
        check(prompt->width() > dragged.width() &&
                  prompt->height() > dragged.height(),
              QStringLiteral("the corner grip resizes the floating prompt"));

        promptDetach->click();
        QApplication::processEvents();
        auto *detachWindow =
            window.findChild<QWidget *>(QStringLiteral("promptDetachWindow"));
        check(detachWindow && detachWindow->isWindow() &&
                  prompt->window() == detachWindow &&
                  promptWrapper && promptWrapper->isVisible(),
              QStringLiteral("the detach button moves the prompt into a window "
                             "of its own, outside the app's own window"));

        promptDetach->click();
        QApplication::processEvents();
        check(prompt->window() == &window &&
                  (!detachWindow || !detachWindow->isVisible()),
              QStringLiteral("pressing detach again brings the prompt back "
                             "inside the app"));

        QMouseEvent snapBack(QEvent::MouseButtonDblClick,
                             QPointF(promptHandle->width() / 2.0,
                                     promptHandle->height() / 2.0),
                             QPointF(promptHandle->mapToGlobal(QPoint(0, 0))),
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(promptHandle, &snapBack);
        QApplication::processEvents();
        check(prompt->parentWidget() == dock &&
                  prompt->geometry().bottom() == dock->rect().bottom(),
              QStringLiteral("double-clicking the handle snaps the prompt back "
                             "to the footer anchor"));
    }

    window.testSetLogOverlayExpanded(true);
    QApplication::processEvents();
    check(log && lights && header && log->isVisible() && lights->isVisible() &&
              header->isVisible() && header->lightCount() == 31 &&
              log->geometry().center().x() < dock->rect().center().x() &&
              log->geometry().bottom() == dock->rect().bottom() &&
              header->geometry().top() >= log->rect().top(),
          QStringLiteral("the compact log opens at the lower-left with its "
                         "expanded 31-category header"));

    window.testShowLogSection();
    QApplication::processEvents();
    check(!log->isVisible() && lights->isVisible(),
          QStringLiteral("the full Log view leaves the debug category row visible"));
    window.testShowHomeSection();
    QApplication::processEvents();
}

// Give queued animations (the entry rise and the stack's shuffle up) time to
// land without blocking the window, which would trip the UI-stall watchdog.
void settleAnimations(int timeoutMs = 400)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

// An error reaching the log has to announce itself: until now a background
// failure was visible only if the Log section happened to be open. Every
// ERROR-badged line now pulses the window border and shows the failure as a
// card — once per failure, and without doubling up the cards callers raise
// themselves through flashMessage.
void checkLoggedErrorAlert(MainWindow &window)
{
    window.testDismissTopMessage();
    window.testResetNetworkLog();
    window.testResetLoggedErrorAlerts();
    QApplication::processEvents();

    window.testLogSystem(QStringLiteral("Mirror sync finished"));
    QApplication::processEvents();
    check(!window.testErrorBorderVisible(),
          QStringLiteral("an ordinary log line leaves the window alone"));

    window.testLogSystem(
        QStringLiteral("Push to mirror7 failed: connection refused"));
    QApplication::processEvents();
    check(window.testErrorBorderVisible() &&
              window.testTopMessageRaw().contains(
                  QStringLiteral("connection refused")),
          QStringLiteral("an error in the log flashes the window and shows the "
                         "failure"));

    const int queuedAfterFirst = window.testTopMessageQueueDepth();
    window.testLogSystem(
        QStringLiteral("Push to mirror7 failed: connection refused"));
    QApplication::processEvents();
    check(window.testTopMessageQueueDepth() == queuedAfterFirst,
          QStringLiteral("a retried failure repeating itself alerts once, not "
                         "once per attempt"));

    // A caller reporting its own failure still flashes the window, but the log
    // hook must not stack a second card behind the one it just showed.
    window.testDismissTopMessage();
    window.testResetLoggedErrorAlerts();
    QApplication::processEvents();
    window.testFlashMessage(QStringLiteral("Clone failed: no live host"), true);
    QApplication::processEvents();
    check(window.testErrorBorderVisible() &&
              window.testTopMessageQueueDepth() == 0 &&
              window.testTopMessageRaw().contains(QStringLiteral("Clone failed")),
          QStringLiteral("a reported failure flashes the window and is shown "
                         "exactly once"));

    window.testDismissTopMessage();
    window.testResetNetworkLog();
    window.testResetLoggedErrorAlerts();
    QApplication::processEvents();
}

// adhoc #1444: the alert stack has to hug its own content and read as one evenly
// spaced column that rises into place. The reference screenshot showed a prompt
// confirmation stretched down the entire window with empty bands between its
// sent marker, its agent line and the prompt itself.
void checkAlertStackLayout(MainWindow &window)
{
    window.testShowPromptBubble(
        QStringLiteral("please add the small icon of the user agent used for the "
                       "session to the left of the status, and move the file "
                       "count left of the diff colour bars"),
        QStringLiteral("Started a CC agent on your prompt (claude-opus-5, Auto)."));
    QApplication::processEvents();

    auto *container = window.findChild<QWidget *>(QStringLiteral("topMessage"));
    auto *header =
        window.findChild<QLabel *>(QStringLiteral("topMessagePromptHeader"));
    auto *status =
        window.findChild<QLabel *>(QStringLiteral("topMessagePromptStatus"));
    auto *text = window.findChild<QLabel *>(QStringLiteral("topMessageText"));
    auto *actions = window.findChild<QWidget *>(QStringLiteral("topMessageActions"));
    if (!container || !header || !status || !text || !actions) {
        check(false, QStringLiteral("the prompt confirmation stacks its lines "
                                    "without empty bands"));
        return;
    }

    // Every card enters by rising: it is placed below its anchor and glides up.
    const QRect anchored = window.testTopMessageRect();
    check(container->isVisible() && container->y() > anchored.y(),
          QStringLiteral("an arriving alert starts below its anchor and slides "
                         "up into place"));
    settleAnimations();
    check(container->geometry() == anchored,
          QStringLiteral("the alert lands exactly on its prompt-anchored slot"));

    const int markerGap = status->y() - (header->y() + header->height());
    const int textGap = text->y() - (status->y() + status->height());
    check(header->isVisible() && status->isVisible() && text->isVisible() &&
              markerGap == forkmesh::ui::kToastLineSpacing &&
              textGap == forkmesh::ui::kToastLineSpacing,
          QStringLiteral("the prompt confirmation stacks its lines without "
                         "empty bands"));

    // The bubble is its lines plus one caption row — not a slab sized to the
    // window. 4px of slack absorbs rounding in the wrapped-text measurement.
    const int lines = text->geometry().bottom() - header->geometry().top() + 1;
    const int chrome = actions->height() + forkmesh::ui::kToastRowSpacing +
                       forkmesh::ui::kToastPadTop +
                       forkmesh::ui::kToastPadBottom;
    check(anchored.height() <= lines + chrome + 4,
          QStringLiteral("the alert is only as tall as the lines it holds plus "
                         "its caption row"));

    // A second arrival queues below the active toast, and the column stays one
    // evenly spaced list anchored above the prompt. (The confirmation goes first:
    // anything arriving while it counts down would queue behind it too.)
    window.testDismissTopMessage();
    QApplication::processEvents();
    window.testFlashMessage(QStringLiteral("Mirror sync finished"));
    QApplication::processEvents();
    window.testFlashMessage(QStringLiteral("Push rejected: remote has newer "
                                           "commits on main"),
                            true);
    settleAnimations();
    auto *queue = window.findChild<QScrollArea *>(QStringLiteral("topMessageQueue"));
    const QRect stacked = window.testTopMessageRect();
    check(window.testTopMessageQueueDepth() == 1 && queue && queue->isVisible() &&
              queue->width() == stacked.width() &&
              queue->y() - stacked.bottom() == forkmesh::ui::kToastStackGap,
          QStringLiteral("a queued alert sits one gap under the active one in "
                         "the same column"));

    auto *card = queue ? queue->findChild<QWidget *>(
                             QStringLiteral("topMessageQueueCard"))
                       : nullptr;
    auto *cardActions =
        card ? card->findChild<QWidget *>(QStringLiteral("topMessageQueueActions"))
             : nullptr;
    auto *cardBadge =
        card ? card->findChild<QLabel *>(QStringLiteral("topMessageQueueTypeBadge"))
             : nullptr;
    auto *cardText =
        card ? card->findChild<QLabel *>(QStringLiteral("topMessageQueueText"))
             : nullptr;
    const int cardChrome = cardActions ? cardActions->height() +
                                             forkmesh::ui::kToastRowSpacing +
                                             forkmesh::ui::kToastPadTop +
                                             forkmesh::ui::kToastPadBottom
                                       : 0;
    check(card && cardActions && cardBadge && cardText &&
              cardBadge->parentWidget() == cardActions &&
              card->height() <= cardText->height() + cardChrome + 4,
          QStringLiteral("a queued card keeps its kind badge on the caption row "
                         "so it stays two lines tall"));

    if (!qEnvironmentVariableIsEmpty("FORKMESH_ALERT_SHOT")) {
        qInfo("ALERTSHOT bubble=%s queue=%s lines=%d chrome=%d card=%d",
              qPrintable(QString::number(anchored.height())),
              qPrintable(QString::number(queue ? queue->height() : -1)), lines,
              chrome, card ? card->height() : -1);
        const QRect stack = stacked.united(queue ? queue->geometry() : stacked)
                                .adjusted(-8, -8, 8, 8);
        window.grab(stack).save(
            qEnvironmentVariable("FORKMESH_ALERT_SHOT"));
    }

    window.testDismissTopMessage();
    QApplication::processEvents();
    check(container && !container->isVisible() && queue && !queue->isVisible(),
          QStringLiteral("dismissing clears the whole alert stack"));
}

QString widgetPath(QWidget *widget)
{
    QStringList parts;
    for (QWidget *current = widget; current; current = current->parentWidget()) {
        QString part = QString::fromLatin1(current->metaObject()->className());
        if (!current->objectName().isEmpty())
            part += QStringLiteral("#") + current->objectName();
        parts.prepend(part);
    }
    return parts.join(QStringLiteral(" > "));
}

void dumpTallMinimums(QWidget &root)
{
    struct Entry {
        int effectiveHeight = 0;
        int minimumHintHeight = 0;
        int explicitMinimumHeight = 0;
        QString path;
    };

    QList<Entry> entries;
    const QList<QWidget *> widgets = root.findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const int minimumHintHeight = widget->minimumSizeHint().height();
        const int explicitMinimumHeight = widget->minimumHeight();
        const int effectiveHeight =
            std::max(minimumHintHeight, explicitMinimumHeight);
        if (effectiveHeight < 120)
            continue;
        entries.append({effectiveHeight, minimumHintHeight, explicitMinimumHeight,
                        widgetPath(widget)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  return a.effectiveHeight > b.effectiveHeight;
              });

    const int limit = std::min<int>(entries.size(), 18);
    for (int i = 0; i < limit; ++i) {
        const Entry &entry = entries.at(i);
        qInfo().noquote()
            << QString("MIN %1px hint=%2 explicit=%3 %4")
                   .arg(entry.effectiveHeight)
                   .arg(entry.minimumHintHeight)
                   .arg(entry.explicitMinimumHeight)
                   .arg(entry.path);
    }
}

void dumpWideMinimums(QWidget &root)
{
    struct Entry {
        int effectiveWidth = 0;
        int minimumHintWidth = 0;
        int explicitMinimumWidth = 0;
        QString path;
    };

    QList<Entry> entries;
    const QList<QWidget *> widgets = root.findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        const int minimumHintWidth = widget->minimumSizeHint().width();
        const int explicitMinimumWidth = widget->minimumWidth();
        const int effectiveWidth =
            std::max(minimumHintWidth, explicitMinimumWidth);
        if (effectiveWidth < 180)
            continue;
        entries.append({effectiveWidth, minimumHintWidth, explicitMinimumWidth,
                        widgetPath(widget)});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) {
                  return a.effectiveWidth > b.effectiveWidth;
              });

    const int limit = std::min<int>(entries.size(), 24);
    for (int i = 0; i < limit; ++i) {
        const Entry &entry = entries.at(i);
        qInfo().noquote()
            << QString("MIN-W %1px hint=%2 explicit=%3 %4")
                   .arg(entry.effectiveWidth)
                   .arg(entry.minimumHintWidth)
                   .arg(entry.explicitMinimumWidth)
                   .arg(entry.path);
    }
}

QCheckBox *findCheckBox(QWidget &root, const QString &text)
{
    const QList<QCheckBox *> boxes = root.findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes) {
        if (box->text() == text)
            return box;
    }
    return nullptr;
}

QPushButton *findButtonStartingWith(QWidget &root, const QString &prefix)
{
    const QList<QPushButton *> buttons = root.findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text().startsWith(prefix))
            return button;
    }
    return nullptr;
}

QString quotedCommand(const QString &program, const QStringList &arguments)
{
    QStringList parts{program};
    parts += arguments;
    return parts.join(QLatin1Char(' '));
}

bool runProcessChecked(const QString &program, const QStringList &arguments,
                       const QString &workingDir)
{
    QProcess process;
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    process.start(program, arguments);
    if (!process.waitForFinished(10000)) {
        qCritical().noquote()
            << "FAIL: timed out:" << quotedCommand(program, arguments);
        ++failures;
        return false;
    }
    if (process.exitCode() != 0) {
        qCritical().noquote()
            << "FAIL:" << quotedCommand(program, arguments)
            << QString::fromUtf8(process.readAllStandardError()).trimmed();
        ++failures;
        return false;
    }
    return true;
}

bool runGitChecked(const QString &repoDir, const QStringList &arguments)
{
    return runProcessChecked(QStringLiteral("git"), arguments, repoDir);
}

QString runProcessOutput(const QString &program, const QStringList &arguments,
                         const QString &workingDir)
{
    QProcess process;
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    process.start(program, arguments);
    if (!process.waitForFinished(10000)) {
        qCritical().noquote()
            << "FAIL: timed out:" << quotedCommand(program, arguments);
        ++failures;
        return {};
    }
    if (process.exitCode() != 0) {
        qCritical().noquote()
            << "FAIL:" << quotedCommand(program, arguments)
            << QString::fromUtf8(process.readAllStandardError()).trimmed();
        ++failures;
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString gitOutput(const QString &repoDir, const QStringList &arguments)
{
    return runProcessOutput(QStringLiteral("git"), arguments, repoDir);
}

bool initGitRepo(QTemporaryDir &repo)
{
    if (!repo.isValid()) {
        qCritical("FAIL: could not create temporary git repository");
        ++failures;
        return false;
    }

    return runGitChecked(repo.path(), {"init"}) &&
           runGitChecked(repo.path(), {"config", "user.email", "tests@forkmesh.local"}) &&
           runGitChecked(repo.path(), {"config", "user.name", "ForkMesh Tests"}) &&
           runGitChecked(repo.path(), {"checkout", "-b", "main"}) &&
           runGitChecked(repo.path(), {"commit", "--allow-empty", "-m", "initial"});
}

MemberInfo testMember(const QString &id, const QString &name, bool self = false)
{
    MemberInfo member;
    member.id = id;
    member.name = name;
    member.self = self;
    member.online = true;
    return member;
}

void stopChildProcesses(QObject &root)
{
    QList<QPointer<QProcess>> processes;
    const QList<QProcess *> children = root.findChildren<QProcess *>();
    processes.reserve(children.size());
    for (QProcess *process : children)
        processes.append(QPointer<QProcess>(process));

    for (const QPointer<QProcess> &process : std::as_const(processes)) {
        // Waiting on one process can deliver deferred deletion for another.
        if (!process)
            continue;
        if (process->state() == QProcess::NotRunning)
            continue;
        process->kill();
        if (process)
            process->waitForFinished(3000);
    }
}

void runLogTimelineChecks(MainWindow &window)
{
    {
        LogTimelineChart retentionChart;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        retentionChart.setRange(now - 1000, now + 1000);
        retentionChart.setEntries({{now, QStringLiteral("NET")},
                                   {now, QStringLiteral("GIT")},
                                   {now, QStringLiteral("NET")}});
        retentionChart.removeEntry(now, QStringLiteral("NET"));
        check(retentionChart.visibleEntryCount() == 2,
              QStringLiteral("timeline retention removes only one matching "
                             "evicted log entry"));
        retentionChart.removeEntry(now, QStringLiteral("MISSING"));
        check(retentionChart.visibleEntryCount() == 2,
              QStringLiteral("timeline retention ignores an entry outside its "
                             "loaded slice"));
    }
    window.testResetNetworkLog();
    window.testLogSystem(QStringLiteral("Timeline session started"));
    window.testLogSystem(QStringLiteral("Pushed 2 commits to origin/main"));
    window.testLogSystem(QStringLiteral("Network request completed"));
    window.testShowLogSection();
    QApplication::processEvents();

    QWidget *chart = window.testLogTimelineChart();
    check(chart && chart->isVisible(),
          QStringLiteral("the Log page shows its activity chart"));
    check(chart && chart->maximumHeight() <= 72,
          QStringLiteral("the activity chart stays a thin top rail"));
    check(window.testNetworkLogView() && window.testNetworkLogView()->isVisible(),
          QStringLiteral("the paged network log remains visible below the rail"));
    check(window.findChild<QListWidget *>(QStringLiteral("logEventList")) ==
              nullptr,
          QStringLiteral("the duplicate Recent Pings feed is removed"));
    check(window.testLogTimelineVisibleCount() >= 3,
          QStringLiteral("the 24-hour timeline includes all freshly logged events"));

    QStringList ranges;
    QPushButton *customRangeButton = nullptr;
    for (QPushButton *button :
         window.findChildren<QPushButton *>(QStringLiteral("logRangeButton"))) {
        ranges << button->text();
        if (button->text() == QLatin1String("Custom..."))
            customRangeButton = button;
    }
    check(ranges.contains(QStringLiteral("24h")) &&
              ranges.contains(QStringLiteral("7 days")) &&
              ranges.contains(QStringLiteral("30 days")) &&
              ranges.contains(QStringLiteral("Custom...")),
          QStringLiteral("the timeline offers preset and custom timeframes"));

    bool customRangeApplied = false;
    if (customRangeButton) {
        QTimer::singleShot(0, &window, [&customRangeApplied] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            auto *from = dialog ? dialog->findChild<QDateTimeEdit *>(
                                      QStringLiteral("logCustomFrom"))
                                : nullptr;
            auto *to = dialog ? dialog->findChild<QDateTimeEdit *>(
                                    QStringLiteral("logCustomTo"))
                              : nullptr;
            auto *buttons = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr;
            if (!from || !to || !buttons ||
                !buttons->button(QDialogButtonBox::Ok)) {
                if (dialog)
                    dialog->reject();
                return;
            }
            const QDateTime now = QDateTime::currentDateTime();
            from->setDateTime(now.addSecs(-60 * 60));
            to->setDateTime(now.addSecs(60));
            customRangeApplied = true;
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        customRangeButton->click();
    }
    check(customRangeApplied && customRangeButton &&
              customRangeButton->isChecked() &&
              window.testLogTimelineVisibleCount() >= 3,
          QStringLiteral("a custom start and end time applies to the chart"));

    if (chart) {
        const QPointF start(chart->width() * 0.35, chart->height() * 0.5);
        const QPointF end(chart->width() * 0.70, chart->height() * 0.5);
        QMouseEvent press(QEvent::MouseButtonPress, start,
                          chart->mapToGlobal(start.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, end,
                            chart->mapToGlobal(end.toPoint()),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(chart, &press);
        QApplication::sendEvent(chart, &release);
        check(window.testLogTimelineSummary().contains(QStringLiteral("Zoomed")),
              QStringLiteral("dragging across the timeline zooms into that area"));
    }
    window.testSetLogTimelineHours(7 * 24);
    check(!window.testLogTimelineSummary().contains(QStringLiteral("Zoomed")),
          QStringLiteral("choosing a timeframe resets the chart zoom"));
    window.testResetNetworkLog();
}

} // namespace

int main(int argc, char *argv[])
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        qCritical("FAIL: could not create temporary settings directory");
        return 1;
    }
    QTemporaryDir runtimeDir;
    if (!runtimeDir.isValid()) {
        qCritical("FAIL: could not create temporary runtime directory");
        return 1;
    }
    qputenv("XDG_RUNTIME_DIR", runtimeDir.path().toUtf8());
    qputenv("GSETTINGS_BACKEND", QByteArrayLiteral("memory"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    // AgentStore persists issue #291's merge flag under AppDataLocation, so
    // concurrent or repeated suites must never share an application identity.
    // This temporary directory supplies a collision-resistant per-process token;
    // AppDataCleanup below removes the exact resolved path on normal exit.
    QTemporaryDir dataDir;
    if (!dataDir.isValid()) {
        qCritical("FAIL: could not create temporary data directory");
        return 1;
    }
    struct AppDataCleanup {
        QString path;
        ~AppDataCleanup()
        {
            if (!path.isEmpty())
                QDir(path).removeRecursively();
        }
    } appDataCleanup;
    QStandardPaths::setTestModeEnabled(true);

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("ForkMeshTests");
    const QString testApplicationName =
        QStringLiteral("WindowResize-") + QFileInfo(dataDir.path()).fileName();
    app.setApplicationName(testApplicationName);

    // Synchronous helper callers still receive a result immediately, but the
    // process and wait must be owned by a worker rather than the QApplication
    // thread. The activity bus exposes the actual lane used by waitForGit.
    std::atomic<int> gitWorkerStarts{0};
    std::atomic<int> gitUiStarts{0};
    forkmesh::BackgroundActivity::setListener(
        [&gitWorkerStarts, &gitUiStarts](
            quint64, const QString &kind, const QString &,
            forkmesh::ActionTelemetry::Execution execution, bool started) {
            if (!started || kind != QLatin1String("git"))
                return;
            if (execution == forkmesh::ActionTelemetry::Execution::Worker)
                ++gitWorkerStarts;
            if (execution == forkmesh::ActionTelemetry::Execution::UiBlocking)
                ++gitUiStarts;
        });
    QByteArray gitVersion;
    const bool gitVersionOk = forkmesh::ui::runGitCapture(
        QDir::tempPath(), {QStringLiteral("--version")}, &gitVersion, nullptr);
    forkmesh::BackgroundActivity::setListener(nullptr);
    check(gitVersionOk && gitVersion.startsWith("git version") &&
              gitWorkerStarts.load() == 1 && gitUiStarts.load() == 0,
          QStringLiteral("synchronous Git helpers execute and wait on a worker"));

    const QString darkTheme = QString::fromLatin1(Theme::styleSheetForDark(true));
    const QString lightTheme = QString::fromLatin1(Theme::styleSheetForDark(false));
    check(darkTheme.contains(QStringLiteral(
              "QToolTip {\n    background-color: #161b22; color: #e6edf3;")) &&
              lightTheme.contains(QStringLiteral(
              "QToolTip {\n    background-color: #ffffff; color: #1f2328;")) &&
              lightTheme.contains(QStringLiteral(
              "border: 1px solid #d0d7de; padding: 4px;")),
          QStringLiteral("tooltips use the active app theme's canvas, text, and border"));
    const bool fleetBinaryInstallOnly =
        app.arguments().contains(QStringLiteral("--fleet-binary-install-only"));
    const bool hostsLayoutOnly =
        app.arguments().contains(QStringLiteral("--hosts-layout-only"));
    const bool mirrorFleetOnly =
        app.arguments().contains(QStringLiteral("--mirror-fleet-only"));
    const bool issuesRedesignOnly =
        app.arguments().contains(QStringLiteral("--issues-redesign-only"));
    const bool logTimelineOnly =
        app.arguments().contains(QStringLiteral("--log-timeline-only"));
    const bool footerOverlayOnly =
        app.arguments().contains(QStringLiteral("--footer-overlay-only"));
    const bool errorAlertOnly =
        app.arguments().contains(QStringLiteral("--error-alert-only"));
    const bool updateIsolationOnly =
        app.arguments().contains(QStringLiteral("--update-isolation-only"));

    const QString appDataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        qCritical("FAIL: could not resolve temporary app data directory");
        return 1;
    }
    check(QFileInfo(appDataPath).fileName() == testApplicationName,
          QString("window test app data is isolated per process (%1)")
              .arg(appDataPath));
    appDataCleanup.path = appDataPath;
    QDir appDataDir(appDataPath);
    if (appDataDir.exists() && !appDataDir.removeRecursively()) {
        qCritical("FAIL: could not clear temporary app data directory");
        return 1;
    }

    // The chrome compresses the four resource histories into one chart while
    // preserving the distinct hover and click affordances of each quadrant.
    {
        using ResourceChart = forkmesh::ui::ResourceQuadrantSparkline;
        ResourceChart chart;
        chart.setResourceToolTip(ResourceChart::Cpu, QStringLiteral("CPU details"));
        chart.setResourceToolTip(ResourceChart::Memory, QStringLiteral("Memory details"));
        chart.setResourceToolTip(ResourceChart::Swap, QStringLiteral("Swap details"));
        chart.setResourceToolTip(ResourceChart::Disk, QStringLiteral("Disk details"));
        for (int resource = 0; resource < ResourceChart::ResourceCount; ++resource) {
            chart.addSample(static_cast<ResourceChart::Resource>(resource),
                            20.0 + resource * 20.0, 100.0);
            chart.addSample(static_cast<ResourceChart::Resource>(resource),
                            25.0 + resource * 20.0, 100.0);
        }

        int diagnosticsClicks = 0;
        int memoryClicks = 0;
        for (ResourceChart::Resource resource : {ResourceChart::Cpu, ResourceChart::Swap,
                                                 ResourceChart::Disk}) {
            chart.setClickHandler(resource, [&diagnosticsClicks] { ++diagnosticsClicks; });
        }
        chart.setClickHandler(ResourceChart::Memory, [&memoryClicks] { ++memoryClicks; });
        chart.show();
        QApplication::processEvents();

        const auto click = [&chart](const QPoint &position) {
            const QPointF global = chart.mapToGlobal(position);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(position), global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&chart, &press);
        };
        click(QPoint(8, 8));
        click(QPoint(25, 8));
        click(QPoint(8, 25));
        click(QPoint(25, 25));
        check(diagnosticsClicks == 3 && memoryClicks == 1,
              QStringLiteral("resource chart keeps the per-quadrant click actions"));

        const auto hover = [&chart](const QPoint &position, const QString &expected) {
            QHelpEvent event(QEvent::ToolTip, position, chart.mapToGlobal(position));
            QApplication::sendEvent(&chart, &event);
            return QToolTip::text() == expected;
        };
        check(hover(QPoint(8, 8), QStringLiteral("CPU details")) &&
                  hover(QPoint(25, 8), QStringLiteral("Memory details")) &&
                  hover(QPoint(8, 25), QStringLiteral("Swap details")) &&
                  hover(QPoint(25, 25), QStringLiteral("Disk details")),
              QStringLiteral("resource chart keeps the per-quadrant hover details"));
    }

    // issue #300: the headless node runs on the offscreen QPA plugin, whose
    // propagateSizeHints() base implementation logs "This plugin does not support
    // propagateSizeHints()" every time a window pushes its size constraints —
    // noise that otherwise lands in the interactive `forkmesh>` console. The
    // platform log filter must swallow exactly that message and forward everything
    // else to the previously installed handler. (This suite already runs under the
    // offscreen platform, the same one the warning fires on.)
    {
        QStringList captured;
        g_capturedMessages = &captured;
        QtMessageHandler previous = qInstallMessageHandler(captureMessages);
        forkmesh::installPlatformLogFilter(); // chains to captureMessages
        qWarning("This plugin does not support propagateSizeHints()");
        qWarning("forkmesh-300-control-line");
        qInstallMessageHandler(previous); // restore so PASS/FAIL output prints
        g_capturedMessages = nullptr;

        const QString joined = captured.join(QLatin1Char('\n'));
        check(!joined.contains(QStringLiteral("propagateSizeHints")),
              QStringLiteral("platform log filter drops the offscreen "
                             "propagateSizeHints warning (#300)"));
        check(joined.contains(QStringLiteral("forkmesh-300-control-line")),
              QStringLiteral("platform log filter forwards unrelated warnings to "
                             "the previous handler (#300)"));
        check(forkmesh::isPlatformSizeHintNoise(QStringLiteral(
                  "This plugin does not support propagateSizeHints()")) &&
                  !forkmesh::isPlatformSizeHintNoise(
                      QStringLiteral("forkmesh-300-control-line")),
              QStringLiteral("isPlatformSizeHintNoise matches only the "
                             "propagateSizeHints warning (#300)"));
    }

    // Seed the removed custody preference to verify startup performs a one-way,
    // fail-closed migration instead of silently re-enabling it.
    QSettings().setValue(QStringLiteral("bounty/autoPrEnabled"), true);
    QSettings().setValue(QStringLiteral("bounty/autoPrMode"),
                         QStringLiteral("wallet"));

    // Startup logging is intentionally always on: paired entries name each
    // expensive operation and include its duration so a slow launch can be
    // diagnosed from a user's log without reproducing it under a profiler.
    QStringList startupMessages;
    g_capturedMessages = &startupMessages;
    QtMessageHandler startupPrevious = qInstallMessageHandler(captureMessages);
    MainWindow window;
    qInstallMessageHandler(startupPrevious);
    g_capturedMessages = nullptr;

    // Prompt shortcut headers can dedicate a fresh run to one exact CLI model.
    // Those routing fields configure the launcher and must not leak into the
    // task text the model receives.
    {
        QTemporaryDir shortcutDir;
        const QString shortcutPath =
            shortcutDir.filePath(QStringLiteral("write-update.md"));
        QFile shortcut(shortcutPath);
        const bool wrote = shortcut.open(QIODevice::WriteOnly | QIODevice::Text) &&
                           shortcut.write(
                               "# name: Write update\n"
                               "# description: Publish the next update.\n"
                               "# agent: CODEX\n"
                               "# model: gpt-5.6-sol\n\n"
                               "Inspect changes since the last post.\n") > 0;
        shortcut.close();
        const QStringList metadata = window.testShortcutMetadata(shortcutPath);
        check(wrote && metadata.size() == 5 &&
                  metadata.at(0) == QStringLiteral("Write update") &&
                  metadata.at(1) == QStringLiteral("Publish the next update.") &&
                  metadata.at(2) == QStringLiteral("codex") &&
                  metadata.at(3) == QStringLiteral("gpt-5.6-sol") &&
                  metadata.at(4) ==
                      QStringLiteral("Inspect changes since the last post."),
              QStringLiteral("shortcut metadata pins a dedicated model and is "
                             "removed from its launch prompt"));
    }
    const QString startupLog = startupMessages.join(QLatin1Char('\n'));
    const int detailedStartupSteps =
        startupLog.count(QRegularExpression(QStringLiteral(
            "\\[startup \\+\\s*\\d+ms\\] BEGIN MainWindow:")));
    if (!issuesRedesignOnly && !logTimelineOnly && !footerOverlayOnly &&
        !mirrorFleetOnly && !errorAlertOnly)
        check(detailedStartupSteps >= 7 &&
              startupLog.contains(QStringLiteral(
                  "BEGIN MainWindow: read connection state and cached model list")) &&
              startupLog.contains(QStringLiteral(
                  "DONE  MainWindow: read connection state and cached model list (")) &&
              startupLog.contains(QStringLiteral(
                  "BEGIN MainWindow: populate Hosts navigation count")) &&
              startupLog.contains(QStringLiteral(
                  "DONE  MainWindow: populate Hosts navigation count (")) &&
              startupLog.contains(QStringLiteral(
                  "startup job scheduled: initial mirror synchronization in "
                  "1000ms")),
          QString("startup log names and times every material constructor phase "
                  "(detailed steps=%1)")
                  .arg(detailedStartupSteps));

    if (mirrorFleetOnly) {
        const QJsonArray managedHostFixture{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("mirror7")},
                {QStringLiteral("ip"), QStringLiteral("203.0.113.7")},
                {QStringLiteral("user"), QStringLiteral("root")},
                {QStringLiteral("status"), QStringLiteral("installed")},
                {QStringLiteral("provider"), QStringLiteral("Vultr")},
                {QStringLiteral("instanceId"),
                 QStringLiteral("1f2e3d4c-5b6a-4798-8899-aabbccddeeff")},
            },
        };
        QSettings settings;
        settings.remove(QStringLiteral("hosts/healthyMirrorFleet/enabled"));
        settings.remove(QStringLiteral("hosts/healthyMirrorFleet/desired"));
        settings.setValue(
            QStringLiteral("hosts/list"),
            QString::fromUtf8(
                QJsonDocument(managedHostFixture).toJson(
                    QJsonDocument::Compact)));
        window.testShowHostsSection();
        QApplication::processEvents();
        QCheckBox *enabled = window.findChild<QCheckBox *>(
            QStringLiteral("healthyMirrorFleetEnabled"));
        QSpinBox *desired = window.findChild<QSpinBox *>(
            QStringLiteral("desiredHealthyMirrorCount"));
        QLabel *status = window.findChild<QLabel *>(
            QStringLiteral("healthyMirrorFleetStatus"));
        check(enabled && desired && status && !enabled->isChecked() &&
                  desired->value() == 1 && desired->isEnabled() &&
                  status->text().contains(QStringLiteral("off")) &&
                  enabled->toolTip().contains(
                      QStringLiteral("permanently destroys")),
              QStringLiteral(
                  "healthy mirror fleet UI is opt-in and safely seeds its target from managed Vultr hosts"));
        if (desired)
            desired->setValue(2);
        QApplication::processEvents();
        check(settings.value(
                  QStringLiteral("hosts/healthyMirrorFleet/desired"))
                      .toInt() == 2,
              QStringLiteral(
                  "editing the desired healthy mirror count persists while automation is off"));
        settings.remove(QStringLiteral("hosts/list"));
        settings.remove(QStringLiteral("hosts/healthyMirrorFleet/enabled"));
        settings.remove(QStringLiteral("hosts/healthyMirrorFleet/desired"));
        stopChildProcesses(window);
        return failures == 0 ? 0 : 1;
    }

    if (logTimelineOnly) {
        window.show();
        QApplication::processEvents();
        runLogTimelineChecks(window);
        stopChildProcesses(window);
        return failures == 0 ? 0 : 1;
    }

    if (errorAlertOnly) {
        window.show();
        QApplication::processEvents();
        checkLoggedErrorAlert(window);
        stopChildProcesses(window);
        return failures == 0 ? 0 : 1;
    }

    if (footerOverlayOnly) {
        window.resize(1200, 720);
        window.show();
        QApplication::processEvents();
        checkFooterOverlayGeometry(window);
        return failures == 0 ? 0 : 1;
    }

    if (issuesRedesignOnly) {
        window.show();
        QApplication::processEvents();
        const bool shown = window.testShowRepoIssuesTab();
        QApplication::processEvents();

        QTableWidget *issueList =
            window.findChild<QTableWidget *>(QStringLiteral("issueList"));
        QLineEdit *topSearch =
            window.findChild<QLineEdit *>(QStringLiteral("globalSearch"));
        QLineEdit *legacySearch =
            window.findChild<QLineEdit *>(QStringLiteral("issueListSearchState"));
        QPushButton *prioritize = window.findChild<QPushButton *>(
            QStringLiteral("issuePrioritizeAction"));
        QPushButton *analyze = window.findChild<QPushButton *>(
            QStringLiteral("issueAnalyzeAction"));
        QPushButton *sync = window.findChild<QPushButton *>(
            QStringLiteral("issueHeaderAction"));
        QPlainTextEdit *composer =
            window.findChild<QPlainTextEdit *>(QStringLiteral("issueQuickAdd"));

        check(shown && issueList && issueList->horizontalHeader()->isHidden() &&
                  issueList->frameShape() == QFrame::NoFrame,
              QStringLiteral("issues render as a headerless, frameless summary list"));
        check(topSearch &&
                  topSearch->placeholderText().startsWith(
                      QStringLiteral("Search issues")) &&
                  topSearch->toolTip().contains(QStringLiteral("is:open")) &&
                  legacySearch && legacySearch->isHidden(),
              QStringLiteral("top search owns issue filtering and documents operators "
                             "(placeholder=%1 tooltip=%2 legacyHidden=%3)")
                  .arg(topSearch ? topSearch->placeholderText() : QStringLiteral("missing"),
                       topSearch ? topSearch->toolTip() : QStringLiteral("missing"))
                  .arg(legacySearch && legacySearch->isHidden()));
        check(window.testIssueDetailVisible(),
              QStringLiteral("issue detail remains visible beside the list"));
        check(prioritize && analyze && sync && prioritize->sizeHint().height() >= 40 &&
                  analyze->sizeHint().height() >= 40,
              QStringLiteral("issue actions use the rail-style icon tile row"));

        if (prioritize)
            prioritize->click();
        check(composer && composer->toPlainText().contains(
                              QStringLiteral("triaging a software project's open issue backlog")),
              QStringLiteral("Prioritize drafts an editable composer prompt"));
        if (analyze)
            analyze->click();
        check(composer && composer->toPlainText().contains(
                              QStringLiteral("ALREADY implemented in the codebase")),
              QStringLiteral("Analyze drafts an editable composer prompt"));
        return failures == 0 ? 0 : 1;
    }

    // Codex can explicitly mark either account-rate-limit window unavailable.
    // The two prompt gauges must show their independent live percentages first,
    // then clear rather than displaying stale figures from a previous plan.
    {
        const qint64 resetSeconds =
            QDateTime::currentMSecsSinceEpoch() / 1000 + 6 * 60 * 60;
        window.testApplyCodexRateLimits(
            QJsonObject{{QStringLiteral("primary"),
                         QJsonObject{{QStringLiteral("usedPercent"), 25},
                                     {QStringLiteral("resetsAt"), resetSeconds},
                                     {QStringLiteral("windowDurationMins"), 300}}},
                        {QStringLiteral("secondary"),
                         QJsonObject{{QStringLiteral("usedPercent"), 65},
                                     {QStringLiteral("resetsAt"),
                                      resetSeconds + 7 * 24 * 60 * 60},
                                     {QStringLiteral("windowDurationMins"),
                                      7 * 24 * 60}}}});
        const QString liveTip = window.testCodexUsageToolTip();
        check(liveTip.contains(QStringLiteral("5-hour remaining: 75%")) &&
                  liveTip.contains(QStringLiteral("Weekly remaining: 35%")) &&
                  liveTip.contains(QStringLiteral("resets in")),
              QStringLiteral("Codex 5-hour and weekly gauges show live usage"));

        window.testApplyCodexRateLimits(
            QJsonObject{{QStringLiteral("primary"), QJsonValue::Null},
                        {QStringLiteral("secondary"), QJsonValue::Null}});
        const QString unavailableTip = window.testCodexUsageToolTip();
        check(unavailableTip.contains(
                  QString::fromUtf8("5-hour remaining: \xE2\x80\x94")) &&
                  unavailableTip.contains(
                      QString::fromUtf8("Weekly remaining: \xE2\x80\x94")) &&
                  !unavailableTip.contains(QStringLiteral("resets in")),
              QStringLiteral("Codex gauges clear unavailable windows"));
    }

    // The prompt meters are click targets, ordered Codex then Claude Code, and
    // their menus combine per-account usage with account and terminal actions.
    {
        QWidget *codexMeter = window.findChild<QWidget *>(
            QStringLiteral("codexAccountUsageButton"));
        QWidget *claudeMeter = window.findChild<QWidget *>(
            QStringLiteral("claudeAccountUsageButton"));
        check(codexMeter && claudeMeter &&
                  codexMeter->parentWidget() == claudeMeter->parentWidget() &&
                  codexMeter->x() < claudeMeter->x() &&
                  codexMeter->cursor().shape() == Qt::PointingHandCursor &&
                  claudeMeter->cursor().shape() == Qt::PointingHandCursor,
              QStringLiteral("prompt usage buttons are clickable with Codex left "
                             "and Claude Code right"));

        const qint64 resetSeconds =
            QDateTime::currentMSecsSinceEpoch() / 1000 + 7 * 24 * 60 * 60;
        window.testApplyClaudeUsageResponse(QJsonObject{
            {QStringLiteral("five_hour"),
             QJsonObject{{QStringLiteral("utilization"), 0.12}}},
            {QStringLiteral("seven_day"),
             QJsonObject{{QStringLiteral("utilization"), 42.0}}},
            {QStringLiteral("seven_day_fable_5"),
             QJsonObject{{QStringLiteral("utilization"), QStringLiteral("0.37")},
                         {QStringLiteral("resetsAt"), resetSeconds}}}});
        const QString claudeTip = window.testClaudeUsageToolTip();
        check(claudeTip.contains(QStringLiteral("5-hour: 12%")) &&
                  claudeTip.contains(QStringLiteral("Weekly: 42%")) &&
                  claudeTip.contains(QStringLiteral("Fable: 37%")) &&
                  claudeTip.contains(QStringLiteral("resets in")),
              QStringLiteral("Claude meter parses versioned Fable limits and "
                             "fraction/string usage values"));

        window.testShowAgentAccountMenu(QStringLiteral("claude-code"));
        QApplication::processEvents();
        QMenu *accountMenu = window.findChild<QMenu *>(
            QStringLiteral("claudeAccountUsageMenu"));
        QStringList menuText;
        if (accountMenu)
            for (QAction *action : accountMenu->actions())
                if (!action->isSeparator())
                    menuText << action->text().trimmed();
        check(accountMenu &&
                  menuText.contains(QStringLiteral("Claude Code accounts and usage")) &&
                  std::any_of(menuText.cbegin(), menuText.cend(),
                              [](const QString &text) {
                                  return text.startsWith(
                                      QStringLiteral("Fable weekly: 37% used"));
                              }) &&
                  menuText.contains(QString::fromUtf8("Add account\xE2\x80\xA6")) &&
                  menuText.contains(QString::fromUtf8(
                      "Log out active account\xE2\x80\xA6")) &&
                  menuText.contains(QStringLiteral(
                      "Launch Claude Code in system terminal")),
              QStringLiteral("click menu shows account usage, add/logout and "
                             "system-terminal actions"));
        if (accountMenu)
            accountMenu->close();

        const QString profileId = QStringLiteral("test-secondary");
        const QString profileDir =
            QDir(appDataPath).filePath(QStringLiteral("agent-accounts/claude/") +
                                       profileId);
        QDir().mkpath(profileDir);
        {
            QSettings settings;
            settings.beginGroup(forkmesh::ui::agentAccountProfilesGroup(
                QStringLiteral("claude-code")));
            settings.beginGroup(profileId);
            settings.setValue(QStringLiteral("label"),
                              QStringLiteral("Secondary"));
            settings.setValue(QStringLiteral("configDir"), profileDir);
            settings.endGroup();
            settings.endGroup();
            settings.setValue(
                forkmesh::ui::agentAccountUsageSetting(
                    QStringLiteral("claude-code"), profileId,
                    QStringLiteral("claudeUsageFablePct")),
                58);
        }
        window.testSelectAgentAccount(QStringLiteral("claude-code"), profileId);
        check(forkmesh::ui::activeAgentAccount(
                  QStringLiteral("claude-code")).id == profileId &&
                  forkmesh::ui::activeAgentAccountEnv(
                      QStringLiteral("claude-code")) ==
                      QStringList{QStringLiteral("CLAUDE_CONFIG_DIR=") + profileDir} &&
                  window.testClaudeUsageToolTip().contains(
                      QStringLiteral("Fable: 58%")),
              QStringLiteral("selecting an account activates its provider config "
                             "root and cached limits"));

        window.testShowAgentAccountMenu(QStringLiteral("claude-code"));
        QApplication::processEvents();
        accountMenu = window.findChild<QMenu *>(
            QStringLiteral("claudeAccountUsageMenu"));
        bool hasEditAction = false;
        if (accountMenu) {
            for (QAction *action : accountMenu->actions()) {
                if (action->objectName() ==
                    QStringLiteral("agentAccountEdit_%1").arg(profileId))
                    hasEditAction = true;
            }
        }
        check(hasEditAction,
              QStringLiteral("account menu offers editing for saved accounts"));
        check(window.testRenameAgentAccount(QStringLiteral("claude-code"),
                                            profileId,
                                            QStringLiteral("Work Claude")) &&
                  forkmesh::ui::activeAgentAccount(
                      QStringLiteral("claude-code")).label ==
                      QStringLiteral("Work Claude"),
              QStringLiteral("editing an account persists its label"));
        if (accountMenu)
            accountMenu->close();
        window.testSelectAgentAccount(QStringLiteral("claude-code"),
                                      QStringLiteral("default"));
    }

    // adhoc #115: the first-run screen that asked for a username and a relay
    // host is retired — it only ever loaded straight into the app — so a freshly
    // constructed window is already on the app shell, before any session starts.
    check(window.testStackIndex() == 1,
          QStringLiteral("the app opens on the app shell, not a setup screen"));
    // Its replacement is the top-bar "Log in / Sign up" pill, which stays hidden
    // until the deferred startup has resolved whether a user account is attached
    // (otherwise every launch would flash it at an already-signed-in user).
    check(!window.testSignInButtonVisible(),
          QStringLiteral("the sign-in pill waits for silent auth to resolve"));

    // An agent's file list is the paths its patch-unique commits own, not the
    // reverse image of everything main added after the branch forked. This is
    // also the safe fallback when main was rewritten and equivalent base commits
    // no longer share object ids.
    {
        QTemporaryDir ownedDiffRepo;
        if (initGitRepo(ownedDiffRepo)) {
            const QString agentBranch = QStringLiteral("agent/owned-one-file");
            runGitChecked(ownedDiffRepo.path(), {"checkout", "-q", "-b",
                                                  agentBranch});
            QFile agentFile(ownedDiffRepo.path() + QStringLiteral("/agent-owned.txt"));
            if (agentFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                agentFile.write("belongs to the agent\n");
                agentFile.close();
            }
            runGitChecked(ownedDiffRepo.path(), {"add", "agent-owned.txt"});
            runGitChecked(ownedDiffRepo.path(), {"commit", "-m",
                                                  "agent owns one file"});
            runGitChecked(ownedDiffRepo.path(), {"checkout", "-q", "main"});
            QDir(ownedDiffRepo.path()).mkpath(QStringLiteral("main-only"));
            for (int i = 0; i < 23; ++i) {
                QFile unrelated(
                    ownedDiffRepo.path() +
                    QStringLiteral("/main-only/unrelated-%1.txt").arg(i));
                if (unrelated.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    unrelated.write("belongs to main\n");
                    unrelated.close();
                }
            }
            runGitChecked(ownedDiffRepo.path(), {"add", "main-only"});
            runGitChecked(ownedDiffRepo.path(), {"commit", "-m",
                                                  "main advances independently"});
            const QStringList owned = window.testAgentOwnedDiffPaths(
                ownedDiffRepo.path(), QStringLiteral("main"), agentBranch);
            check(owned == QStringList{QStringLiteral("agent-owned.txt")},
                  QString("agent diff excludes main's 23 unrelated files "
                          "(owned = %1)")
                      .arg(owned.join(QStringLiteral(", "))));
        }
    }

    // Opening Chat from its unread badge should land directly on the unread
    // conversation carrying the newest message, while preserving the already
    // open conversation when that conversation itself is unread.
    {
        check(window.testMostRecentUnreadConversation(
                  QStringLiteral("#general"),
                  {QStringLiteral("#general"), QStringLiteral("#engineering"),
                   QStringLiteral("#product")},
                  {QStringLiteral("#engineering"), QStringLiteral("#product")},
                  {{QStringLiteral("#engineering"), 100},
                   {QStringLiteral("#product"), 200}}) ==
                  QStringLiteral("#product"),
              QStringLiteral("chat badge selects the newest unread channel"));

        check(window.testMostRecentUnreadConversation(
                  QStringLiteral("#general"),
                  {QStringLiteral("#general"), QStringLiteral("#engineering")},
                  {QStringLiteral("#general"), QStringLiteral("#engineering")},
                  {{QStringLiteral("#general"), 300},
                   {QStringLiteral("#engineering"), 200}})
                  .isEmpty(),
              QStringLiteral("chat badge preserves an already-open unread channel"));

        check(window.testMostRecentUnreadConversation(
                  QStringLiteral("#general"),
                  {QStringLiteral("#general"), QStringLiteral("#engineering"),
                   QStringLiteral("#product")},
                  {QStringLiteral("#engineering"), QStringLiteral("#product")},
                  {}) ==
                  QStringLiteral("#engineering"),
              QStringLiteral("chat badge uses sidebar order when unread history "
                             "is not cached"));
    }

    // Bottom status bar (adhoc #2): a strip exactly one text line tall carrying
    // the branch switcher, the repo's git identity and the location of the
    // running executable. The first two used to live in the repo Code overview,
    // which builds lazily — the strip must be populated from the first frame.
    {
        QWidget *statusBar =
            window.findChild<QWidget *>(QStringLiteral("appStatusBar"));
        check(statusBar != nullptr,
              QStringLiteral("bottom status bar exists on the app shell"));
        if (statusBar) {
            check(statusBar->minimumHeight() == statusBar->maximumHeight() &&
                      statusBar->maximumHeight() <=
                          statusBar->fontMetrics().height() + 8,
                  QStringLiteral("status bar is pinned to a single text line"));
            QPushButton *branch =
                statusBar->findChild<QPushButton *>(QStringLiteral("ghostButton"));
            check(branch && branch->toolTip() == QStringLiteral("Switch branch"),
                  QStringLiteral("branch switcher sits in the status bar"));
            check(statusBar->findChild<QLabel *>(
                      QStringLiteral("footerGitIdentity")) != nullptr,
                  QStringLiteral("git identity sits in the status bar"));
            // adhoc #55: the strip also names the commit the branch is on.
            check(statusBar->findChild<QLabel *>(
                      QStringLiteral("footerCommitInfo")) != nullptr,
                  QStringLiteral("branch commit info sits in the status bar"));
            check(statusBar->findChild<QLabel *>(
                      QStringLiteral("footerWorktreeInfo")) != nullptr,
                  QStringLiteral("current worktree has a place in the status bar"));
            QLabel *appPath =
                statusBar->findChild<QLabel *>(QStringLiteral("statusAppPath"));
            check(appPath && !appPath->text().isEmpty() &&
                      appPath->toolTip().contains(
                          QCoreApplication::applicationFilePath()),
                  QStringLiteral("status bar shows the running app's location"));
            QPushButton *version = statusBar->findChild<QPushButton *>(
                QStringLiteral("statusVersionButton"));
            statusBar->layout()->activate();
            QApplication::processEvents();
            check(version && appPath && version->geometry().left() >=
                                               appPath->geometry().right(),
                  QStringLiteral("clickable version sits to the right of the app path"));

            // adhoc #1389: slow background work no longer reserves a permanent
            // footer column. Each kind appears as one 18px status icon, and the
            // segmented rotating ring is driven by the live process count.
            QWidget *backgroundHost = statusBar->findChild<QWidget *>(
                QStringLiteral("statusBackgroundTasks"));
            check(backgroundHost &&
                      window.findChild<QWidget *>(
                          QStringLiteral("backgroundTaskQueue")) == nullptr,
                  QStringLiteral("background work moved from the footer panel "
                                 "into the bottom status bar"));

            const quint64 cleanupOne = forkmesh::BackgroundActivity::begin(
                QStringLiteral("cleanup"), QStringLiteral("first test cleanup"));
            const quint64 cleanupTwo = forkmesh::BackgroundActivity::begin(
                QStringLiteral("cleanup"), QStringLiteral("second test cleanup"));
            QWidget *cleanupChip = nullptr;
            QElapsedTimer chipWait;
            chipWait.start();
            while (!cleanupChip && chipWait.elapsed() < 1000) {
                QApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(10);
                const auto chips = backgroundHost
                                       ? backgroundHost->findChildren<QWidget *>(
                                             QStringLiteral(
                                                 "statusBackgroundTaskChip"))
                                       : QList<QWidget *>();
                for (QWidget *chip : chips) {
                    if (chip->property("processKind").toString() ==
                        QStringLiteral("cleanup")) {
                        cleanupChip = chip;
                        break;
                    }
                }
            }
            check(cleanupChip && cleanupChip->size() == QSize(18, 18) &&
                      cleanupChip->property("processCount").toInt() == 2 &&
                      cleanupChip->toolTip().contains(
                          QString(QChar(0x00D7)) + QStringLiteral("2")) &&
                      cleanupChip->accessibleDescription().contains(
                          QStringLiteral("2 processes")),
                  QStringLiteral("one compact process icon carries the live "
                                 "background-work count"));
            forkmesh::BackgroundActivity::end(cleanupOne);
            forkmesh::BackgroundActivity::end(cleanupTwo);
        }
    }

    // Fleet "Install from binary" must install the published release, not
    // upload this test process (or any other locally-built executable). The
    // target verifies the release checksum in install.sh, refuses source
    // fallback, restarts in place, and checks both the reported version and
    // exact source revision before the per-host pane can turn green.
    {
        const QString expectedBuildCommit =
            QStringLiteral(FORKMESH_BUILD_COMMIT).trimmed().toLower();
        const QRegularExpression exactCommit(
            QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
        const bool buildIsCommitted =
            exactCommit.match(expectedBuildCommit).hasMatch();
        if (!buildIsCommitted) {
            qsizetype refusedBytes = -1;
            QString refusedError;
            const QString refusedCommand =
                window.testFleetBinaryInstallRemoteCommand(
                    false, &refusedBytes, &refusedError);
            check(refusedCommand.isEmpty() && refusedBytes == 0 &&
                      refusedError.contains(
                          QStringLiteral("no exact source revision")),
                  QStringLiteral("dirty or unknown-provenance builds refuse "
                                 "fleet binary deployment"));
        } else {
            check(true,
                  QStringLiteral("test binary embeds an exact Git source commit"));
        }

        // Exercise the successful command contract deterministically even when
        // this suite is intentionally running from a dirty developer tree. This
        // override exists only in the FORKMESH_WINDOW_TESTS target.
        const QByteArray testCommit(
            "0123456789abcdef0123456789abcdef01234567");
        const QByteArray testManifestDigest(64, 'b');
        qputenv("FORKMESH_TEST_BUILD_COMMIT", testCommit);
        qputenv("FORKMESH_TEST_RELEASE_MANIFEST_SHA256",
                QByteArray("not-a-digest"));
        qsizetype untrustedUploadBytes = -1;
        QString untrustedError;
        const QString untrustedCommand =
            window.testFleetBinaryInstallRemoteCommand(
                false, &untrustedUploadBytes, &untrustedError);
        check(untrustedCommand.isEmpty() && untrustedUploadBytes == 0 &&
                  untrustedError.contains(QStringLiteral(
                      "must be an exact 64-hex")),
              QStringLiteral(
                  "fleet binary deployment fails closed without a valid "
                  "controller release-manifest trust anchor"));
        qputenv("FORKMESH_TEST_RELEASE_MANIFEST_SHA256",
                testManifestDigest);
        const QString commandBuildCommit = QString::fromLatin1(testCommit);

        qsizetype uploadBytes = -1;
        QString commandError;
        const QString command = window.testFleetBinaryInstallRemoteCommand(
            false, &uploadBytes, &commandError);
        check(!command.isEmpty() && commandError.isEmpty(),
              QStringLiteral("fleet binary install command builds without an "
                             "installer error"));
        check(uploadBytes == 0 &&
                  !command.contains(QStringLiteral("FORKMESH_LOCAL_BINARY")) &&
                  !command.contains(QStringLiteral("__FORKMESH_UPLOAD__")),
              QStringLiteral("fleet binary install never uploads the locally "
                             "running source/test executable"));
        check(command.contains(
                  QStringLiteral("FORKMESH_RELEASE=latest")) &&
                  command.contains(
                      QStringLiteral("FORKMESH_NO_SOURCE_FALLBACK=1")) &&
                  command.contains(
                      QStringLiteral("FORKMESH_EXPECTED_BUILD_COMMIT=")) &&
                  command.contains(
                      QStringLiteral("FORKMESH_EXPECTED_RELEASE_VERSION=")) &&
                  command.contains(QStringLiteral(
                      "FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256=")) &&
                  command.contains(
                      QString::fromLatin1(testManifestDigest)) &&
                  command.contains(commandBuildCommit) &&
                  !command.contains(QStringLiteral("FORKMESH_FROM_SOURCE=1")),
              QStringLiteral("fleet binary install is pinned to the published "
                             "binary-only release at this source commit"));
        check(command.contains(QStringLiteral("FORKMESH_RESTART=1")) &&
                  !command.contains(QStringLiteral("FORKMESH_REINSTALL=1")),
              QStringLiteral("normal fleet binary install restarts in place "
                             "without deleting node keys or data"));
        check(command.contains(QStringLiteral("Cache-Control: no-cache")) &&
                  command.contains(QStringLiteral("command -v sha256sum")) &&
                  command.contains(
                      QStringLiteral("forkmesh-installer.XXXXXX")) &&
                  command.contains(QStringLiteral("--version")) &&
                  command.contains(QStringLiteral("--build-commit")) &&
                  command.contains(
                      QStringLiteral("source-revision check failed")) &&
                  command.contains(QStringLiteral("probe_ticks")) &&
                  command.contains(QStringLiteral("sleep 0.1")) &&
                  command.contains(QStringLiteral("sleep 0.2")) &&
                  command.contains(QStringLiteral("kill -KILL")) &&
                  command.contains(QStringLiteral("head -c 128")) &&
                  command.contains(
                      QStringLiteral("wait \"$commit_pid\" || "
                                     "commit_status=$?")) &&
                  command.contains(
                      QStringLiteral("ForkMesh " FORKMESH_VERSION)),
              QStringLiteral("fleet binary install fetches a fresh installer "
                             "and verifies the exact app version and source "
                             "revision with a bounded legacy-binary probe"));
        QProcess shellSyntax;
        shellSyntax.start(QStringLiteral("/bin/sh"),
                          {QStringLiteral("-n"), QStringLiteral("-c"), command});
        const bool syntaxFinished = shellSyntax.waitForFinished(5000);
        check(syntaxFinished && shellSyntax.exitStatus() == QProcess::NormalExit &&
                  shellSyntax.exitCode() == 0,
              QStringLiteral("fleet binary remote command is valid shell "
                             "syntax"));

        uploadBytes = -1;
        commandError.clear();
        const QString reinstallCommand =
            window.testFleetBinaryInstallRemoteCommand(
                true, &uploadBytes, &commandError);
        check(!reinstallCommand.isEmpty() && commandError.isEmpty() &&
                  uploadBytes == 0 &&
                  reinstallCommand.contains(
                      QStringLiteral("FORKMESH_REINSTALL=1")) &&
                  reinstallCommand.contains(
                      QStringLiteral("FORKMESH_NO_SOURCE_FALLBACK=1")) &&
                  reinstallCommand.contains(
                      QStringLiteral("FORKMESH_EXPECTED_BUILD_COMMIT=")) &&
                  reinstallCommand.contains(
                      QStringLiteral("--build-commit")),
              QStringLiteral("explicit destructive fleet reinstall also uses "
                             "the commit-matched published binary-only release"));
        qunsetenv("FORKMESH_TEST_BUILD_COMMIT");
        qunsetenv("FORKMESH_TEST_RELEASE_MANIFEST_SHA256");

        QFile runningBinary(QCoreApplication::applicationFilePath());
        QCryptographicHash runningBinaryHash(
            QCryptographicHash::Sha256);
        const bool hashOpened = runningBinary.open(QIODevice::ReadOnly);
        const bool hashRead =
            hashOpened && runningBinaryHash.addData(&runningBinary);
        const QString expectedUploadDigest =
            QString::fromLatin1(runningBinaryHash.result().toHex());
        qsizetype directUploadBytes = -1;
        QString directUploadError;
        const QString directUploadCommand =
            window.testDirectBinaryInstallRemoteCommand(
                &directUploadBytes, &directUploadError);
        check(hashRead && !directUploadCommand.isEmpty() &&
                  directUploadError.isEmpty() && directUploadBytes > 0 &&
                  expectedUploadDigest.size() == 64 &&
                  directUploadCommand.contains(
                      QStringLiteral("FORKMESH_LOCAL_BINARY_SHA256=")) &&
                  directUploadCommand.contains(expectedUploadDigest) &&
                  !directUploadCommand.contains(QStringLiteral(
                      "FORKMESH_EXPECTED_RELEASE_MANIFEST_SHA256=")),
              QStringLiteral(
                  "direct controller uploads pin the exact local binary SHA-256"));

        // The source-tree test must not depend on a developer having already
        // built the release companion.  Put a tiny non-empty executable in
        // PATH so this exercises the package framing and remote installer
        // deterministically; production still fails closed when the packaged
        // Go binary is absent.
        QTemporaryDir mirrorPackageDir;
        const QByteArray originalPath = qgetenv("PATH");
        QFile mirrorBinaryFixture(
            mirrorPackageDir.filePath(QStringLiteral("forkmesh-mirror-node")));
        const bool mirrorFixtureReady = mirrorPackageDir.isValid() &&
            mirrorBinaryFixture.open(QIODevice::WriteOnly) &&
            mirrorBinaryFixture.write("#!/bin/sh\nexit 0\n") > 0;
        mirrorBinaryFixture.close();
        if (mirrorFixtureReady) {
            mirrorBinaryFixture.setPermissions(
                QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                QFileDevice::ExeOther);
            qputenv("PATH", mirrorPackageDir.path().toUtf8() + ':' +
                                originalPath);
        }
        qsizetype mirrorUploadBytes = -1;
        QString mirrorUploadError;
        const QString mirrorCommand =
            window.testVultrGoMirrorInstallRemoteCommand(
                &mirrorUploadBytes, &mirrorUploadError);
        qputenv("PATH", originalPath);
        check(mirrorFixtureReady && !mirrorCommand.isEmpty() &&
                  mirrorUploadError.isEmpty() &&
                  mirrorUploadBytes > 0 &&
                  mirrorCommand.contains(QStringLiteral(
                      "systemctl enable --now forkmesh-mirror-node.service")) &&
                  mirrorCommand.contains(QStringLiteral(
                      "Go mirror-node installed and running (no Qt/GTK packages)")) &&
                  !mirrorCommand.contains(QStringLiteral("install.sh")),
              QStringLiteral(
                  "Vultr uses one native Go mirror package without the desktop installer"));
        QProcess mirrorSyntax;
        mirrorSyntax.start(QStringLiteral("bash"),
                           {QStringLiteral("-n"), QStringLiteral("-c"),
                            mirrorCommand});
        const bool mirrorSyntaxFinished = mirrorSyntax.waitForFinished(5000);
        check(mirrorSyntaxFinished &&
                  mirrorSyntax.exitStatus() == QProcess::NormalExit &&
                  mirrorSyntax.exitCode() == 0,
              QStringLiteral("Vultr Go mirror remote command is valid shell syntax"));
    }
    if (fleetBinaryInstallOnly)
        return failures == 0 ? 0 : 1;

    // Account credentials are never portable ForkMesh data.  The retired
    // claude-auth export/import command names must fail closed without reading
    // an input bundle, writing an output bundle, echoing a token, or touching
    // the clipboard.
    {
        QTemporaryDir transferDir;
        check(transferDir.isValid(),
              QStringLiteral("credential-transfer regression temp dir is valid"));
        const QString exportPath =
            transferDir.filePath(QStringLiteral("claude-account.json"));
        const QString importPath =
            transferDir.filePath(QStringLiteral("incoming.json"));
        const QString liveToken =
            QStringLiteral("sk-ant-live-regression-secret-1234567890");  // forkmesh-secret-scan:ignore-line
        {
            QFile input(importPath);
            check(input.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                      input.write(liveToken.toUtf8()) == liveToken.toUtf8().size(),
                  QStringLiteral("credential-transfer input fixture is written"));
        }
        QApplication::clipboard()->setText(QStringLiteral("clipboard-sentinel"));

        const QString exportReply =
            window.headlessClaudeAuth(
                      {QStringLiteral("export"), exportPath})
                .join(QLatin1Char('\n'));
        check(exportReply.contains(QStringLiteral("Refused:")) &&
                  !QFileInfo::exists(exportPath),
              QStringLiteral("retired Claude export refuses without writing a file"));
        check(!exportReply.contains(liveToken) &&
                  QApplication::clipboard()->text() ==
                      QStringLiteral("clipboard-sentinel"),
              QStringLiteral("retired Claude export cannot reveal or copy a token"));

        const QString importReply =
            window.headlessClaudeAuth(
                      {QStringLiteral("import"), importPath})
                .join(QLatin1Char('\n'));
        check(importReply.contains(QStringLiteral("Refused:")) &&
                  !importReply.contains(liveToken),
              QStringLiteral("retired Claude import refuses without reading or "
                             "echoing a live token"));
        check(QApplication::clipboard()->text() ==
                  QStringLiteral("clipboard-sentinel"),
              QStringLiteral("retired Claude import cannot modify the clipboard"));
    }

    // Plan §5.1: the desktop exposes a real local control-node surface. Navigate
    // to the deferred page exactly as a user does, then verify that its controls
    // exist before a session connects and that the Cloudflare credential input
    // remains a password field.
    check(window.testControlNodeSectionIndex() == 14,
          QStringLiteral("local control node has a stable top-level section"));
    window.testShowControlNode();
    QApplication::processEvents();
    check(window.findChild<QWidget *>(
              QStringLiteral("controlNodeSection")) != nullptr,
          QStringLiteral("local control-node page is constructed"));
    check(window.findChild<QPushButton *>(
              QStringLiteral("controlStartMirrorsButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlStopMirrorsButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlSyncMirrorsButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlHealthButton")) != nullptr,
          QStringLiteral("control node exposes mirror lifecycle, sync and health"));
    QLineEdit *cloudflareToken = window.findChild<QLineEdit *>(
        QStringLiteral("cloudflareApiToken"));
    check(cloudflareToken &&
              cloudflareToken->echoMode() == QLineEdit::Password,
          QStringLiteral("Cloudflare token control masks the session-only secret"));

    // adhoc #108: every control-node area is a tab at the top of the page, and
    // the API token tab states what the deploy path requires before any call is
    // made. The token input masks its value like every other credential field.
    QTabWidget *controlTabs =
        window.findChild<QTabWidget *>(QStringLiteral("controlNodeTabs"));
    QStringList controlTabNames;
    for (int index = 0; controlTabs && index < controlTabs->count(); ++index)
        controlTabNames << controlTabs->tabText(index);
    check(controlTabs && controlTabs->count() >= 8 &&
              controlTabNames.contains(QStringLiteral("Mirror services")) &&
              controlTabNames.contains(QStringLiteral("API token")) &&
              controlTabNames.contains(QStringLiteral("Site deployment")),
          QStringLiteral("control node groups each section under a top tab"));
    QLineEdit *controlTokenValue =
        window.findChild<QLineEdit *>(QStringLiteral("controlTokenValue"));
    QTableWidget *controlTokenTable = window.findChild<QTableWidget *>(
        QStringLiteral("controlTokenPermissionsTable"));
    check(controlTokenValue &&
              controlTokenValue->echoMode() == QLineEdit::Password &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlTokenTestButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlTokenGenerateButton")) != nullptr,
          QStringLiteral("API token tab exposes a masked token, a check and a "
                         "rotate button"));
    QStringList requiredPermissionLabels;
    for (int row = 0; controlTokenTable && row < controlTokenTable->rowCount();
         ++row) {
        const QTableWidgetItem *label = controlTokenTable->item(row, 0);
        const QTableWidgetItem *need = controlTokenTable->item(row, 1);
        if (label && need && need->text() == QStringLiteral("Required"))
            requiredPermissionLabels << label->text();
    }
    check(requiredPermissionLabels.contains(QStringLiteral("Workers Scripts: Edit")) &&
              requiredPermissionLabels.contains(QStringLiteral("D1: Edit")) &&
              requiredPermissionLabels.contains(QStringLiteral("DNS: Edit")),
          QStringLiteral("API token tab lists the required permissions up front"));
    window.testShowLogSection();
    QApplication::processEvents();
    check(window.findChild<QPushButton *>(
              QStringLiteral("cloudflareWorkerLogsButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("logPopoutButton")) != nullptr,
          QStringLiteral("the debug strip keeps the Cloudflare live-log viewer "
                         "and the Log page pops the whole log out"));
    check(window.findChild<QTableWidget *>(
              QStringLiteral("controlPermissionsTable")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("controlManageHostsButton")) != nullptr,
          QStringLiteral("control node exposes permissions and hosts"));
    check(window.findChild<QPushButton *>(
              QStringLiteral("controlOpenWorldButton")) == nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("relayOpenButton")) == nullptr &&
              findButtonStartingWith(window, QStringLiteral("World")) == nullptr &&
              findButtonStartingWith(
                  window, QStringLiteral("Open World")) == nullptr,
          QStringLiteral("Qt client does not expose World browser links"));
    QLineEdit *rewardRpc =
        window.findChild<QLineEdit *>(QStringLiteral("rewardPoolRpc"));
    QPushButton *rewardFetch = window.findChild<QPushButton *>(
        QStringLiteral("rewardPoolFetchButton"));
    check(window.findChild<QLabel *>(
              QStringLiteral("rewardPoolPublicAddress")) != nullptr &&
              window.findChild<QTableWidget *>(
                  QStringLiteral("rewardPoolIntentsTable")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("rewardPoolImportButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("rewardPoolSignButton")) != nullptr &&
              window.findChild<QPushButton *>(
                  QStringLiteral("rewardPoolReconcileButton")) != nullptr,
          QStringLiteral("control node exposes the local reward-pool signer workflow"));
    check(rewardRpc && rewardRpc->text().isEmpty() && rewardFetch &&
              !rewardFetch->isEnabled(),
          QStringLiteral("reward signer fails closed until public RPC configuration exists"));
    check(window.findChild<QLineEdit *>(
              QStringLiteral("rewardPoolPrivateKeyInput")) == nullptr,
          QStringLiteral("reward private-key input exists only inside the explicit import dialog"));

    const QJsonArray actionsHostFixture{
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("mirror2")},
            {QStringLiteral("ip"), QStringLiteral("mirror2.example.test")},
            {QStringLiteral("user"), QStringLiteral("forkmesh")},
            {QStringLiteral("pass"),
             QStringLiteral("test-password-never-rendered")},
            {QStringLiteral("status"), QStringLiteral("installed")},
        },
    };
    QSettings().setValue(
        QStringLiteral("hosts/list"),
        QString::fromUtf8(
            QJsonDocument(actionsHostFixture).toJson(
                QJsonDocument::Compact)));
    window.testShowHostsSection();
    QApplication::processEvents();
    QWidget *hostsPageBody =
        window.findChild<QWidget *>(QStringLiteral("hostsPageBody"));
    QLayout *hostsPageLayout = hostsPageBody ? hostsPageBody->layout() : nullptr;
    QLayout *hostsProvisioningRow = window.findChild<QLayout *>(
        QStringLiteral("hostsProvisioningRow"));
    QTableWidget *hostsPageTable =
        hostsPageBody
            ? hostsPageBody->findChild<QTableWidget *>(QStringLiteral("issueTable"))
            : nullptr;
    check(hostsPageLayout && hostsPageTable && hostsProvisioningRow &&
              hostsPageLayout->indexOf(hostsPageTable) == 0 &&
              hostsPageLayout->indexOf(hostsProvisioningRow) == 1,
          QStringLiteral(
              "Hosts puts the saved-host fleet first and provisioning cards next"));
    QCheckBox *healthyFleetEnabled = window.findChild<QCheckBox *>(
        QStringLiteral("healthyMirrorFleetEnabled"));
    QSpinBox *desiredHealthyMirrors = window.findChild<QSpinBox *>(
        QStringLiteral("desiredHealthyMirrorCount"));
    QLabel *healthyFleetStatus = window.findChild<QLabel *>(
        QStringLiteral("healthyMirrorFleetStatus"));
    check(healthyFleetEnabled && desiredHealthyMirrors && healthyFleetStatus &&
              !healthyFleetEnabled->isChecked() &&
              desiredHealthyMirrors->isEnabled() &&
              desiredHealthyMirrors->value() == 0 &&
              healthyFleetEnabled->toolTip().contains(
                  QStringLiteral("permanently destroys")),
          QStringLiteral(
              "Hosts exposes an explicit opt-in desired healthy mirror count with destructive scaling explained"));
    const QList<QPushButton *> inlineHelpButtons =
        window.findChildren<QPushButton *>(QStringLiteral("inlineHelpButton"));
    QSet<QString> inlineHelpNames;
    for (const QPushButton *button : inlineHelpButtons)
        inlineHelpNames.insert(button->accessibleName());
    const bool completeInlineHelp =
        inlineHelpNames.contains(QStringLiteral("About network diagnostics")) &&
        inlineHelpNames.contains(QStringLiteral("About saved hosts")) &&
        inlineHelpNames.contains(QStringLiteral("About adding a host")) &&
        inlineHelpNames.contains(QStringLiteral("About Vultr mirrors")) &&
        std::all_of(inlineHelpButtons.cbegin(), inlineHelpButtons.cend(),
                    [](const QPushButton *button) {
                        return !button->toolTip().isEmpty() &&
                               !button->accessibleName().isEmpty() &&
                               !button->accessibleDescription().isEmpty();
                    });
    check(completeInlineHelp,
          QStringLiteral(
              "Network and Hosts guidance is available from four accessible hover helpers"));
    check(window.findChild<QPushButton *>(
              QStringLiteral("hostActionsButton")) != nullptr,
          QStringLiteral(
              "saved mirror hosts expose the stdin-only Actions controller"));
    check(!QSettings()
               .value(QStringLiteral("hosts/list"))
               .toString()
               .contains(QStringLiteral("test-password-never-rendered")) &&
              !QSettings()
                   .value(QStringLiteral("hosts/list"))
                   .toString()
                   .contains(QStringLiteral("\"pass\"")),
          QStringLiteral(
              "opening Hosts migrates legacy SSH passwords out of persistent settings"));
    check(window.findChild<QWidget *>(
              QStringLiteral("hostActionsVariablesTable")) == nullptr,
          QStringLiteral(
              "Actions secret-entry widgets exist only inside the explicit dialog"));
    QSettings().remove(QStringLiteral("hosts/list"));
    if (hostsLayoutOnly) {
        stopChildProcesses(window);
        return failures == 0 ? 0 : 1;
    }

    // Settings is deferred independently from the Control Node. Navigate there
    // before checking its one-way legacy-custody migration and controls.
    window.testShowSettingsSection();
    QApplication::processEvents();
    // The profile page (avatar + node power switch) is reachable as a Settings
    // tab, not only from the avatar button (adhoc #274). The single panel is
    // moved into the tab, so check it actually lands there.
    QTabWidget *settingsTabs =
        window.findChild<QTabWidget *>(QStringLiteral("settingsTabs"));
    int profileTabIndex = -1;
    for (int i = 0; settingsTabs && i < settingsTabs->count(); ++i) {
        if (settingsTabs->tabText(i) == QLatin1String("Profile"))
            profileTabIndex = i;
    }
    QWidget *nodeProfilePanel =
        window.findChild<QWidget *>(QStringLiteral("nodeProfilePanel"));
    check(settingsTabs && profileTabIndex >= 0 && nodeProfilePanel &&
              nodeProfilePanel->parentWidget() ==
                  settingsTabs->widget(profileTabIndex),
          QStringLiteral(
              "Settings has a Profile tab hosting the node profile panel"));
    QCheckBox *legacyAutoBounty = window.findChild<QCheckBox *>(
        QStringLiteral("legacyAutoPrBountyDisabled"));
    QComboBox *bountyFundingMode = window.findChild<QComboBox *>(
        QStringLiteral("prBountyFundingMode"));
    check(legacyAutoBounty && !legacyAutoBounty->isEnabled() &&
              !legacyAutoBounty->isChecked() &&
              bountyFundingMode && !bountyFundingMode->isEnabled() &&
              bountyFundingMode->count() == 1 &&
              bountyFundingMode->currentData().toString() ==
                  QLatin1String("perPr") &&
              QSettings().value(QStringLiteral("bounty/autoPrEnabled")).toBool() ==
                  false &&
              QSettings().value(QStringLiteral("bounty/autoPrMode")).toString() ==
                  QLatin1String("perPr"),
          QStringLiteral("legacy Worker-held PR bounty preference migrates to a "
                         "disabled non-custodial placeholder"));
    window.show();
    QApplication::processEvents();

    // A rebuild/restart can continue in the background while the initiating
    // Settings control is no longer visible. Keep a pulsing amber edge around
    // the real window until that restart finishes or fails.
    window.testSetRestartCautionFlash(true);
    QApplication::processEvents();
    auto *restartCaution = window.findChild<QWidget *>(
        QStringLiteral("restartCautionBorderOverlay"));
    const QImage restartCautionImage =
        restartCaution ? restartCaution->grab().toImage() : QImage();
    const QColor restartCautionPixel = restartCautionImage.isNull()
        ? QColor()
        : restartCautionImage.pixelColor(2, 2);
    check(restartCaution && restartCaution->isVisible() &&
              restartCaution->geometry() == window.rect() &&
              restartCautionPixel.red() > restartCautionPixel.green() &&
              restartCautionPixel.green() > restartCautionPixel.blue() &&
              restartCautionPixel.red() > 150,
          QStringLiteral("a restart flashes an amber caution border around the app"));
    window.testSetRestartCautionFlash(false);
    QApplication::processEvents();
    check(restartCaution && !restartCaution->isVisible(),
          QStringLiteral("the restart caution border clears when the restart stops"));

    // The Log destination is a timeline rather than a second scrolling text
    // feed or a duplicate of the Pings page. Its timeframe presets and direct
    // area selection keep dense activity explorable without losing the category
    // filters.
    runLogTimelineChecks(window);

    checkLoggedErrorAlert(window);

    // adhoc #1389: notification actions are caption-height controls, not the
    // full-height buttons shown in the reference screenshot. The queued cards
    // share these same object names and theme rules.
    if (QWidget *toast = window.findChild<QWidget *>(
            QStringLiteral("topMessage"))) {
        QPushButton *toastAction = toast->findChild<QPushButton *>(
            QStringLiteral("topMessageAction"));
        const QList<QPushButton *> toastGhosts =
            toast->findChildren<QPushButton *>(QStringLiteral("ghostButton"));
        const bool ghostsAreThin =
            !toastGhosts.isEmpty() &&
            std::all_of(toastGhosts.cbegin(), toastGhosts.cend(),
                        [](const QPushButton *button) {
                            return button->maximumHeight() <= 18 &&
                                   button->sizeHint().height() <= 18 &&
                                   button->iconSize().height() <= 12;
                        });
        check(toastAction && toastAction->maximumHeight() <= 18 &&
                  toastAction->sizeHint().height() <= 18 &&
                  toastAction->iconSize().height() <= 11 && ghostsAreThin,
              QStringLiteral("alert action buttons use the thinner caption "
                             "treatment"));
    } else {
        check(false, QStringLiteral("alert action buttons use the thinner "
                                   "caption treatment"));
    }

    checkAlertStackLayout(window);

    window.testRunDeferredStartupNow();
    QApplication::processEvents();

    QTemporaryDir primaryBranchRepo;
    QTemporaryDir primaryMirrorParent;
    if (initGitRepo(primaryBranchRepo) && primaryMirrorParent.isValid()) {
        const QString mainCommit =
            gitOutput(primaryBranchRepo.path(), {"rev-parse", "main"});
        runGitChecked(primaryBranchRepo.path(), {"checkout", "-b", "feature/current"});
        runGitChecked(primaryBranchRepo.path(),
                      {"commit", "--allow-empty", "-m", "feature current"});
        const QString featureCommit =
            gitOutput(primaryBranchRepo.path(), {"rev-parse", "feature/current"});
        const QString mirrorPath =
            QDir(primaryMirrorParent.path()).filePath(QStringLiteral("repo.git"));
        runProcessChecked(QStringLiteral("git"),
                          {"clone", "--mirror", primaryBranchRepo.path(), mirrorPath},
                          QString());

        const forkmesh::ui::MirrorBranchTip tip =
            forkmesh::ui::mirrorPrimaryBranchTip(mirrorPath,
                                                 primaryBranchRepo.path());
        check(mainCommit.size() == 40 && featureCommit.size() == 40 &&
                  mainCommit != featureCommit,
              QStringLiteral("test repo has distinct main and checked-out feature commits"));
        check(tip.branch == QStringLiteral("main"),
              QStringLiteral("mirror primary tip reports main, not checked-out branch"));
        check(tip.commit == mainCommit,
              QStringLiteral("mirror primary tip uses the latest main commit"));
    }

    QTemporaryDir upstreamRepo;
    QTemporaryDir upstreamRemote;
    if (initGitRepo(upstreamRepo) && upstreamRemote.isValid()) {
        runProcessChecked(QStringLiteral("git"),
                          {"init", "--bare", upstreamRemote.path()}, QString());
        runGitChecked(upstreamRepo.path(),
                      {"remote", "add", "origin", upstreamRemote.path()});
        runGitChecked(upstreamRepo.path(), {"push", "-u", "origin", "main"});
        check(window.testQuickUpdatePullArguments(upstreamRepo.path()) ==
                  QStringList({"pull", "--ff-only"}),
              QStringLiteral("quick update keeps plain pull when upstream exists"));
    }

    QTemporaryDir noUpstreamRepo;
    if (initGitRepo(noUpstreamRepo)) {
        runGitChecked(noUpstreamRepo.path(), {"checkout", "-b", "feature/no-upstream"});
        check(window.testQuickUpdatePullArguments(noUpstreamRepo.path()) ==
                  QStringList({"pull", "--ff-only", "origin", "feature/no-upstream"}),
              QStringLiteral("quick update pulls origin/current-branch without upstream"));
    }

    QTemporaryDir detachedRepo;
    if (initGitRepo(detachedRepo)) {
        runGitChecked(detachedRepo.path(), {"checkout", "--detach", "HEAD"});
        check(window.testQuickUpdatePullArguments(detachedRepo.path()) ==
                  QStringList({"pull", "--ff-only", "origin", "HEAD"}),
              QStringLiteral("quick update pulls origin/HEAD in detached HEAD"));
    }

    // Update & restart owns a disposable source/binary pair under the app's
    // managed locations. In particular neither path may resolve into the
    // checkout baked into this developer build: the update fallback is allowed
    // to replace its own clone, while the user's working copy must remain byte
    // for byte untouched.
    {
        const QString working =
            QFileInfo(window.testWorkingClientDir()).absoluteFilePath();
        const QString running =
            QFileInfo(window.testRunningClientDir()).absoluteFilePath();
        const QString executable =
            QFileInfo(window.testRunningClientExecutable()).absoluteFilePath();
        const QString workingRoot =
            QFileInfo(QDir(working).filePath(QStringLiteral("..")))
                .absoluteFilePath() + QDir::separator();
        check(running != working && !running.startsWith(workingRoot),
              QStringLiteral("update source is separate from the working copy"));
        check(!executable.startsWith(workingRoot),
              QStringLiteral("updated executable is separate from the working copy"));
    }
    // adhoc #1563: a rebuild used to run the compiler with the default TMPDIR,
    // so a full RAM-backed /tmp killed it mid-compile ("No space left on
    // device") even with room on the build disk, and the status label showed a
    // fixed 300-character slice of the error tail that landed mid-word
    // ("Update failed: vice"). The scratch now lives inside the build tree and
    // the summary picks the real diagnostic.
    {
        check(forkmesh::ui::buildScratchDir(QStringLiteral("/opt/forkmesh/qt_client/build")) ==
                  QStringLiteral("/opt/forkmesh/qt_client/build/.forkmesh-tmp"),
              QStringLiteral("compiler scratch stays inside the build tree (#1563)"));

        // Free space is read from the nearest existing ancestor, so a build
        // directory CMake has not created yet still reports its future volume
        // instead of the "unknown" sentinel (which would skip the pre-flight).
        QTemporaryDir spaceProbe;
        if (spaceProbe.isValid()) {
            const QString buildDir = spaceProbe.filePath(QStringLiteral("build"));
            check(forkmesh::ui::freeBytesForPath(
                      buildDir + QStringLiteral("/nested/not-yet")) > 0,
                  QStringLiteral("free space walks up to an existing ancestor (#1563)"));

            // An unbuilt tree has to fit the whole ~2 GB of objects; one that
            // already has them overwrites in place, so demanding the full
            // figure again would block a viable incremental update.
            const qint64 fresh = forkmesh::ui::rebuildFreeBytesRequired(buildDir);
            check(QDir().mkpath(buildDir + QStringLiteral("/CMakeFiles/forkmesh.dir")),
                  QStringLiteral("test can stage an existing build tree (#1563)"));
            check(forkmesh::ui::rebuildFreeBytesRequired(buildDir) < fresh,
                  QStringLiteral("an existing build tree needs less free space (#1563)"));
        }

        const QString diskFull =
            QStringLiteral("[ 24%] Building CXX object MainWindowNotes.cpp.o\n"
                           "src/MainWindowNotes.cpp:894:1: fatal error: error "
                           "writing to /tmp/ccIQExpV.s: No space left on device\n"
                           "gmake: *** [Makefile:136: all] Error 2");
        check(forkmesh::ui::updateFailureSummary(diskFull).contains(QStringLiteral("disk filled up")),
              QStringLiteral("a full disk is reported as a full disk (#1563)"));
        check(!forkmesh::ui::updateFailureSummary(diskFull).contains(QStringLiteral("gmake")),
              QStringLiteral("a full disk is not reported as the make exit code (#1563)"));

        const QString compileError =
            QStringLiteral("[ 12%] Building CXX object MainWindow.cpp.o\n"
                           "src/MainWindow.cpp:12:3: error: no member named 'nope'\n"
                           "gmake[1]: *** [CMakeFiles/Makefile2:126] Error 2\n"
                           "gmake: *** [Makefile:136: all] Error 2");
        check(forkmesh::ui::updateFailureSummary(compileError) ==
                  QStringLiteral("src/MainWindow.cpp:12:3: error: no member named 'nope'"),
              QStringLiteral("the first diagnostic beats the make tail (#1563)"));

        // No recognisable diagnostic: the tail is cut on a line boundary rather
        // than mid-word, which is what produced "Update failed: vice".
        const QString noisy = QStringLiteral("%1\nthe last whole line")
                                  .arg(QString(400, QLatin1Char('x')));
        const QString summary = forkmesh::ui::updateFailureSummary(noisy);
        check(summary == QStringLiteral("the last whole line"),
              QStringLiteral("an unrecognised tail is cut on a line boundary (#1563)"));
    }

    if (updateIsolationOnly) {
        stopChildProcesses(window);
        return failures == 0 ? 0 : 1;
    }

    // Issue #214: "Build & preview" checks the PR head out into a throwaway
    // worktree, then CMake-configures and builds it. A fresh worktree is
    // registered with `git worktree add`; a reused one is moved with a forced
    // detached checkout. Either way the pipeline ends with the same configure +
    // build commands. The CMake configure line is platform-dependent (macOS adds
    // brew prefixes) so only assert the deterministic checkout-and-build steps here.
    {
        const QString gitDir = QStringLiteral("/repo/.git");
        const QString previewDir = QStringLiteral("/tmp/preview/acme-app-pr7");
        const QString clientDir = previewDir + QStringLiteral("/qt_client");
        const QString buildDir = clientDir + QStringLiteral("/build");
        const QString commit = QStringLiteral("deadbeef");

        const QStringList freshSteps = window.testBuildAndPreviewSteps(
            gitDir, previewDir, clientDir, buildDir, commit, /*haveWorktree=*/false);
        check(freshSteps.size() == 4,
              QStringLiteral("build & preview runs prune, checkout, configure, build (#214)"));
        check(freshSteps.value(0) ==
                  QStringLiteral("git -C /repo/.git worktree prune"),
              QStringLiteral("build & preview prunes stale worktrees first (#214)"));
        check(freshSteps.value(1) ==
                  QStringLiteral("git -C /repo/.git worktree add --detach "
                                 "/tmp/preview/acme-app-pr7 deadbeef"),
              QStringLiteral("build & preview adds a fresh worktree at the PR head (#214)"));
        check(freshSteps.value(3) ==
                  QStringLiteral("cmake --build /tmp/preview/acme-app-pr7/qt_client/build "
                                 "-j 4"),
              QStringLiteral("build & preview compiles the checked-out PR (#214)"));

        const QStringList reuseSteps = window.testBuildAndPreviewSteps(
            gitDir, previewDir, clientDir, buildDir, commit, /*haveWorktree=*/true);
        check(reuseSteps.value(1) ==
                  QStringLiteral("git -C /tmp/preview/acme-app-pr7 checkout --detach "
                                 "-f deadbeef"),
              QStringLiteral("build & preview reuses an existing worktree by checkout (#214)"));
        check(reuseSteps.value(3) == freshSteps.value(3),
              QStringLiteral("build & preview builds the same way whether or not the "
                             "worktree is reused (#214)"));
    }

    // Issue #263: data-table columns are user-resizable — every auto-sized column
    // (ResizeToContents and the Stretch flex column) flips to draggable Interactive
    // once rows arrive, keeping its width, while Fixed button columns are left as
    // configured.
    check(window.testColumnsBecomeResizable(),
          QStringLiteral("data-table content columns become drag-resizable"));

    // Issue #263: dragging a column divider behaves like a spreadsheet — only the
    // dragged column resizes and the columns to its right shift over, instead of a
    // neighbour or far-off Stretch column silently donating the width.
    check(window.testSpreadsheetResize(),
          QStringLiteral("column drag resizes only that column (spreadsheet)"));

    // Issue #33: resizing after a column move keeps spreadsheet semantics. The
    // real Agents table is verified after repository navigation constructs it.
    check(window.testSpreadsheetResizeAfterMove(),
          QStringLiteral("column drag leaves others untouched after a move"));

    // Deferred startup can begin while restoring the previous view, before the
    // silent account lookup runs. That intermediate state must not flash a
    // login prompt for an already-signed-in user.
    window.testMarkDeferredStartupRunning();
    window.testRefreshSignInButton();
    check(!window.testSignInButtonVisible(),
          QStringLiteral("the sign-in pill waits for silent auth after startup begins"));

    window.testEnableSessionStartBypass(true);

    // adhoc #115: startup has resolved now (the bypass marks it done) and this
    // node has no user account, so the pill appears — it is the only way in that
    // the retired setup screen left behind.
    window.testRefreshSignInButton();
    check(window.testSignInButtonVisible(),
          QStringLiteral("the sign-in pill offers a way in once no account is found"));
    check(!window.testUsersNavButtonVisible(),
          QStringLiteral("the Users rail destination is hidden from non-admins"));
    window.testShowUsersSection();
    check(window.testUsersColumns().isEmpty(),
          QStringLiteral("non-admin navigation cannot build the Users page"));

    // No wallet, no signup: starting a node needs only a valid name. The core
    // flow never invokes the (opt-in) account/signup flow, and a fresh node drops
    // straight into the app shell without an account or a verified wallet.
    window.testSetSetupInputs(QStringLiteral("Alice-Node"),
                              QStringLiteral(
                                  "So11111111111111111111111111111111111111112"));
    window.testSetAccountFlowResult(true); // would activate IF the flow ran
    window.testStartSession();
    check(window.testAccountFlowCalls() == 0,
          QStringLiteral("starting a node never runs the account flow"));
    check(window.testStackIndex() == 1,
          QStringLiteral("start enters the app with just a node name"));
    check(window.testUserName() == QStringLiteral("alice-node"),
          QStringLiteral("start uses the sanitized node name"));
    check(window.testAccountName() == QStringLiteral("alice-node"),
          QStringLiteral("start records the node owner name"));
    check(window.testSavedSolanaAddress() ==
              QStringLiteral("So11111111111111111111111111111111111111112"),
          QStringLiteral("start preserves the saved Solana address"));
    check(!window.testAccountAuthenticated(),
          QStringLiteral("start does not require or fake an account"));

    // The Nodes page expands the database-backed user directory's linked-node
    // lists, so offline fleet members do not vanish just because only one node
    // is currently in the live room roster.
    window.testSetDirectoryUserNodes(
        QStringLiteral("alice"),
        {QStringLiteral("node-a"), QStringLiteral("node-b")});
    window.testShowNodesSection();
    const QStringList directoryNodes = window.testNodeDirectoryNames();
    check(directoryNodes.contains(QStringLiteral("node-a")) &&
              directoryNodes.contains(QStringLiteral("node-b")),
          QStringLiteral("Nodes lists offline linked nodes from the relay directory"));
    check(window.findChild<QPushButton *>(
              QStringLiteral("nodesUpdateAllBinaryButton")) != nullptr,
          QStringLiteral("Nodes offers a fleet-wide binary update action"));

    // A registered account name is never a machine-node name.  In particular,
    // a stale online presence for "jett" must not re-add the user to any node
    // surface; the linked machine remains available instead.
    window.testSetDirectoryUserNodes(QStringLiteral("jett"),
                                     {QStringLiteral("jett-mirror")});
    window.testSetRoster(
        {testMember(QStringLiteral("jett-session"), QStringLiteral("jett"))});
    window.testShowNodesSection();
    const QStringList nodesAfterUserPresence = window.testNodeDirectoryNames();
    check(!nodesAfterUserPresence.contains(QStringLiteral("jett"),
                                           Qt::CaseInsensitive) &&
              nodesAfterUserPresence.contains(QStringLiteral("jett-mirror"),
                                              Qt::CaseInsensitive),
          QStringLiteral("a directory user is never classified as a node"));

    // The admin-only Users destination sits under Network and renders exactly
    // the privacy-filtered statistics used by World avatar chests. Every field
    // is a real sortable table column, with numeric activity sorting independent
    // of its human-readable duration.
    window.testSetAdmin(true);
    check(window.testUsersNavButtonVisible(),
          QStringLiteral("admins can see the Users rail destination"));
    window.testShowUsersSection();
    window.testApplyUsersDirectory(QJsonArray{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("zora")},
                    {QStringLiteral("kind"), QStringLiteral("user")},
                    {QStringLiteral("status"), QStringLiteral("active")},
                    {QStringLiteral("emailVerified"), false},
                    {QStringLiteral("createdAt"), 1700000000000.0},
                    {QStringLiteral("totalActiveMs"), 3600000.0},
                    {QStringLiteral("activityBucket"), QStringLiteral("5h")},
                    {QStringLiteral("lastEmailAt"), 0},
                    {QStringLiteral("lastEmailStatus"), QString()},
                    {QStringLiteral("countryCode"), QStringLiteral("CA")},
                    {QStringLiteral("browser"), QStringLiteral("firefox")},
                    {QStringLiteral("os"), QStringLiteral("linux")},
                    {QStringLiteral("nodes"), QJsonArray{}}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("alice")},
                    {QStringLiteral("kind"), QStringLiteral("user")},
                    {QStringLiteral("status"), QStringLiteral("active")},
                    {QStringLiteral("emailVerified"), true},
                    {QStringLiteral("solana"),
                     QStringLiteral("So11111111111111111111111111111111111111112")},
                    {QStringLiteral("createdAt"), 1600000000000.0},
                    {QStringLiteral("totalActiveMs"), 7380000.0},
                    {QStringLiteral("activityBucket"), QStringLiteral("hour")},
                    {QStringLiteral("lastEmailAt"), 1710000000000.0},
                    {QStringLiteral("lastEmailStatus"),
                     QStringLiteral("delivered")},
                    {QStringLiteral("countryCode"), QStringLiteral("US")},
                    {QStringLiteral("browser"), QStringLiteral("chrome")},
                    {QStringLiteral("os"), QStringLiteral("macos")},
                    {QStringLiteral("nodes"),
                     QJsonArray{QStringLiteral("node-a"),
                                QStringLiteral("node-b")}}},
    });
    const QStringList userColumns = window.testUsersColumns();
    const QStringList expectedUserColumns{
        QStringLiteral("User"),          QStringLiteral("Solana"),
        QStringLiteral("Email verified"),
        QStringLiteral("Status"),        QStringLiteral("Joined"),
        QStringLiteral("World activity"),
        QStringLiteral("Activity recency"),
        QStringLiteral("Last email"),    QStringLiteral("Email delivery"),
        QStringLiteral("Country"),       QStringLiteral("Browser"),
        QStringLiteral("OS"),            QStringLiteral("Nodes")};
    check(userColumns == expectedUserColumns,
          QStringLiteral("Users shows every World chest directory statistic"));
    const QStringList activityOrder = window.testSortUsersBy(
        QStringLiteral("World activity"), Qt::DescendingOrder);
    check(activityOrder == QStringList{QStringLiteral("alice"),
                                       QStringLiteral("zora")} &&
              window.testUsersCellText(0, QStringLiteral("World activity")) ==
                  QStringLiteral("2h 03m") &&
              window.testUsersCellText(0, QStringLiteral("Solana")) ==
                  QStringLiteral("So11111111111111111111111111111111111111112") &&
              window.testUsersCellText(1, QStringLiteral("Solana")) ==
                  QStringLiteral("Not set") &&
              window.testUsersCellText(0, QStringLiteral("Nodes")) ==
                  QStringLiteral("2 - node-a, node-b"),
          QStringLiteral("Users sorts formatted statistics by their numeric values"));
    window.testSetAdmin(false);
    check(!window.testUsersNavButtonVisible(),
          QStringLiteral("losing admin status immediately hides Users"));

    // adhoc #129: a public room (#general) is open to every registered account,
    // so its users popup lists the whole database directory — not just the
    // handful of accounts that happen to be online right now.
    window.testSetDirectoryUserNodes(QStringLiteral("zora"), {});
    const QStringList publicRoomUsers =
        window.testChatMemberNames(QStringLiteral("#general"));
    check(publicRoomUsers.contains(QStringLiteral("alice")) &&
              publicRoomUsers.contains(QStringLiteral("zora")),
          QStringLiteral("#general lists every database user, online or not"));

    // The Repos page groups machine publications by logical repository, prefers
    // a public organization alias, maps a standalone node back to its user, and
    // exposes an explicit Switch action.
    const QString rootA(40, QLatin1Char('a'));
    const QString rootB(40, QLatin1Char('b'));
    QJsonArray widgetActivity;
    for (int week = 0; week < 52; ++week)
        widgetActivity.append(0);
    widgetActivity[51] = 1;
    QJsonArray catalogFixture{
        QJsonObject{{QStringLiteral("owner"), QStringLiteral("node-a")},
                    {QStringLiteral("name"), QStringLiteral("widget")},
                    {QStringLiteral("rootCommit"), rootA},
                    {QStringLiteral("description"),
                     QStringLiteral("Never shown in the grid")},
                    {QStringLiteral("branch"), QStringLiteral("main")},
                    {QStringLiteral("issueCount"), QStringLiteral("7")},
                    {QStringLiteral("commitCount"), QStringLiteral("3")},
                    {QStringLiteral("activityWeeks"), widgetActivity},
                    {QStringLiteral("platform"), QStringLiteral("linux")},
                    {QStringLiteral("source"), QStringLiteral("local-node")}},
        QJsonObject{{QStringLiteral("owner"), QStringLiteral("node-b")},
                    {QStringLiteral("name"), QStringLiteral("widget")},
                    {QStringLiteral("rootCommit"), rootA},
                    {QStringLiteral("source"), QStringLiteral("remote-clone")}},
        // The relay's public alias is a copy of the record above, so it carries
        // the same published facts under the organization's name.
        QJsonObject{{QStringLiteral("owner"), QStringLiteral("acme")},
                    {QStringLiteral("name"), QStringLiteral("widget")},
                    {QStringLiteral("rootCommit"), rootA},
                    {QStringLiteral("description"),
                     QStringLiteral("Never shown in the grid")},
                    {QStringLiteral("branch"), QStringLiteral("main")},
                    {QStringLiteral("issueCount"), QStringLiteral("7")},
                    {QStringLiteral("commitCount"), QStringLiteral("1")},
                    {QStringLiteral("activityWeeks"), widgetActivity},
                    {QStringLiteral("platform"), QStringLiteral("linux")},
                    {QStringLiteral("source"),
                     QStringLiteral("organization-alias")},
                    {QStringLiteral("servingOwner"),
                     QStringLiteral("node-a")}},
        QJsonObject{{QStringLiteral("owner"), QStringLiteral("node-b")},
                    {QStringLiteral("name"), QStringLiteral("cli")},
                    {QStringLiteral("rootCommit"), rootB},
                    {QStringLiteral("source"), QStringLiteral("local-node")}},
    };
    window.testRenderNetworkRepos(catalogFixture);
    const QStringList renderedRepoNames = window.testNetworkRepoNames();
    const QSet<QString> repoNames(renderedRepoNames.cbegin(),
                                  renderedRepoNames.cend());
    check(repoNames == QSet<QString>{
                           QStringLiteral("acme/widget"),
                           QStringLiteral("alice/cli")},
          QStringLiteral("Repos groups mirrors under user and organization owners"));
    check(window.testNetworkRepoMirrorHeader() == QStringLiteral("Mirrors"),
          QStringLiteral("Repos uses a compact mirror-count column"));
    check(window.testNetworkRepoActionText(0) == QStringLiteral("Switch"),
          QStringLiteral("Repos provides an explicit Switch button"));
    check(window.testNetworkRepoHasCommitSparkline(0) &&
              window.testNetworkRepoCommitActivitySummary(0) ==
                  QStringLiteral("3 commits total; 1 commit in the past 52 weeks"),
          QStringLiteral("Repos sparkline uses the grouped catalog commit total"));

    // adhoc #118: the page shows the full catalog record per repository -- every
    // published field gets its own column, except the description.
    const QStringList repoColumns = window.testNetworkRepoColumns();
    const QStringList expectedRepoColumns{
        QStringLiteral("Visibility"), QStringLiteral("Live host"),
        QStringLiteral("Branch"),     QStringLiteral("Commits"),
        QStringLiteral("Platform"),   QStringLiteral("Size"),
        QStringLiteral("Clone URL"),  QStringLiteral("Maintainer")};
    bool allRepoColumnsPresent = true;
    for (const QString &column : expectedRepoColumns)
        allRepoColumnsPresent =
            allRepoColumnsPresent && repoColumns.contains(column);
    check(allRepoColumnsPresent,
          QStringLiteral("Repos gives every catalog field its own column"));
    check(!repoColumns.contains(QStringLiteral("Description")),
          QStringLiteral("Repos leaves the repository description out of the grid"));
    check(window.testNetworkRepoCellText(0, QStringLiteral("Branch")) ==
                  QStringLiteral("main") &&
              window.testNetworkRepoCellText(0, QStringLiteral("Issues")) ==
                  QStringLiteral("7") &&
              window.testNetworkRepoCellText(0, QStringLiteral("Platform")) ==
                  QStringLiteral("linux"),
          QStringLiteral("Repos rows carry the published catalog values"));
    check(!window.testNetworkRepoCellText(0, QStringLiteral("Repository"))
               .contains(QStringLiteral("Never shown")),
          QStringLiteral("Repos rows never print the repository description"));
    // The rail badge counts what this page lists (two grouped repositories),
    // not the machine's own copies.
    check(window.testReposNavBadgeCount() == 2,
          QStringLiteral("Repos rail badge counts the repositories on the network"));

    // Reward settings must never launch the former reserve/donation/finalize
    // account funnel. A mock account flow is installed specifically to prove it
    // remains untouched.
    window.testResetNetworkLog();
    window.testSetAccountFlowResult(true, false);
    window.testEnablePaidMirroring();
    check(window.testAccountFlowCalls() == 0,
          QStringLiteral("reward settings never run the account join flow"));
    check(!window.testHasOwnerSigningCapability(),
          QStringLiteral("reward settings do not invent owner signing capability"));

    // Even a mock that would report a desktop-capable account is not called:
    // account registration/sign-in stays an explicit, separate Account action.
    window.testSetAccountFlowResult(true, true);
    window.testEnablePaidMirroring();
    check(window.testAccountFlowCalls() == 0 &&
              !window.testAccountAuthenticated(),
          QStringLiteral("reward settings cannot reserve or activate an account"));

    QCheckBox *nodeConnectAlertCheck =
        findCheckBox(window, QStringLiteral("Show a system ping when a node connects"));
    check(nodeConnectAlertCheck != nullptr,
          QStringLiteral("node-connect system alert checkbox exists"));
    if (nodeConnectAlertCheck) {
        check(!nodeConnectAlertCheck->isChecked(),
              QStringLiteral("node-connect system alert checkbox is unchecked by default"));
    }

    QSettings().setValue(QStringLiteral("notifications/nodeConnect"), false);
    window.testResetNetworkLog();
    window.testResetRosterForAlerts();
    window.testSetNodeAlertGraceUntilMs(0);
    QList<MemberInfo> initialRoster;
    initialRoster.append(testMember(QStringLiteral("self-node"),
                                    QStringLiteral("Self Node"), true));
    initialRoster.append(testMember(QStringLiteral("existing-peer"),
                                    QStringLiteral("Existing Peer")));
    window.testSetRoster(initialRoster);
    check(!window.testNetworkLog().join(QLatin1Char('\n')).contains(
              QStringLiteral("Node connected")),
          QStringLiteral("initial roster fill does not log node connections"));

    QList<MemberInfo> updatedRoster = initialRoster;
    updatedRoster.append(testMember(QStringLiteral("new-peer"),
                                    QStringLiteral("New Peer")));
    window.testSetRoster(updatedRoster);
    const QString networkLog = window.testNetworkLog().join(QLatin1Char('\n'));
    check(networkLog.contains(QStringLiteral("Node connected")) &&
              networkLog.contains(QStringLiteral("New Peer")),
          QStringLiteral("newly online peer is logged when node-connect alerts are disabled"));

    // adhoc #121: a plain user account (accountKind "user" — e.g. ForkBot's
    // relayed chat identity, or a desktop signed in as a user rather than a
    // linked node) coming online must not be announced as "Node connected":
    // it isn't a node.
    window.testResetNetworkLog();
    QList<MemberInfo> withUserPeer = initialRoster;
    MemberInfo userPeer = testMember(QStringLiteral("user-peer"), QStringLiteral("jett"));
    userPeer.accountKind = QStringLiteral("user");
    withUserPeer.append(userPeer);
    window.testSetRoster(withUserPeer);
    check(!window.testNetworkLog().join(QLatin1Char('\n')).contains(QStringLiteral("jett")),
          QStringLiteral("a plain user account online is not logged as a node connecting"));

    // Adhoc #113: an anonymous chat guest coming online is a person passing
    // through, not a node. Only a guest that advertises a machine nodeName (a
    // first-run desktop) is announced — under the machine's name, never the
    // person's "Guest ####" alias.
    window.testResetNetworkLog();
    QList<MemberInfo> withGuestPeer = withUserPeer;
    MemberInfo guestPeer =
        testMember(QStringLiteral("guest-peer"), QStringLiteral("Guest 4242"));
    guestPeer.accountKind = QStringLiteral("guest");
    withGuestPeer.append(guestPeer);
    window.testSetRoster(withGuestPeer);
    check(!window.testNetworkLog().join(QLatin1Char('\n')).contains(
              QStringLiteral("Guest 4242")),
          QStringLiteral("an anonymous guest online is not logged as a node"));
    window.testResetNetworkLog();
    MemberInfo guestDesktopPeer =
        testMember(QStringLiteral("guest-desktop-peer"),
                   QStringLiteral("Guest 4242"));
    guestDesktopPeer.accountKind = QStringLiteral("guest");
    guestDesktopPeer.nodeName = QStringLiteral("magnetic-terminal-4242");
    withGuestPeer.append(guestDesktopPeer);
    window.testSetRoster(withGuestPeer);
    const QString guestLog = window.testNetworkLog().join(QLatin1Char('\n'));
    check(guestLog.contains(QStringLiteral("magnetic-terminal-4242")) &&
              !guestLog.contains(QStringLiteral("Guest 4242")),
          QStringLiteral(
              "a first-run desktop guest announces as its machine node name"));

    // adhoc #404: a browser guest / World visitor that stops sending presence is
    // forgotten after ten idle minutes, while a real node keeps its offline row
    // so it stays selectable in the Node dropdown.
    window.testResetRosterForAlerts();
    window.testSetNodeAlertGraceUntilMs(0);
    QList<MemberInfo> visitorRoster;
    visitorRoster.append(testMember(QStringLiteral("self-node"),
                                    QStringLiteral("Self Node"), true));
    visitorRoster.append(testMember(QStringLiteral("real-node"),
                                    QStringLiteral("Mirror One")));
    MemberInfo guest = testMember(QStringLiteral("guest-1"),
                                  QStringLiteral("Guest 1667"));
    guest.accountKind = QStringLiteral("guest");
    visitorRoster.append(guest);
    visitorRoster.append(testMember(
        QStringLiteral("visitor-1"),
        QString::fromUtf8("World visitor \xC2\xB7 tobiloba")));
    window.testSetRoster(visitorRoster);

    // Everyone drops off the live roster (only self remains).
    QList<MemberInfo> selfOnly;
    selfOnly.append(testMember(QStringLiteral("self-node"),
                               QStringLiteral("Self Node"), true));
    window.testSetRoster(selfOnly);
    const auto rosterNames = [&] {
        QStringList names;
        for (const MemberInfo &m : window.testHomeRoster())
            names << m.name;
        return names;
    };
    check(rosterNames().contains(QStringLiteral("Guest 1667")) &&
              rosterNames().contains(QStringLiteral("Mirror One")),
          QStringLiteral("a just-departed guest is still retained on the roster"));

    // Ten minutes of silence later, only the real node survives.
    window.testAgePeerSightings(11 * 60 * 1000);
    window.testSetRoster(selfOnly);
    const QStringList afterIdle = rosterNames();
    check(!afterIdle.contains(QStringLiteral("Guest 1667")) &&
              !afterIdle.contains(QString::fromUtf8("World visitor \xC2\xB7 tobiloba")),
          QStringLiteral("idle guests and World visitors leave the roster"));
    check(afterIdle.contains(QStringLiteral("Mirror One")),
          QStringLiteral("an offline node keeps its roster row"));
    window.testResetRosterForAlerts();

    std::atomic<int> historyDeleteCalls{0};
    QSemaphore historyDeleteStarted;
    QSemaphore finishHistoryDelete;
    QSemaphore historyDeleteFinished;
    window.testSetIssueHistoryDeleteRunner(
        [&](int, QString *) {
            historyDeleteCalls.fetch_add(1);
            historyDeleteStarted.release();
            finishHistoryDelete.acquire();
            historyDeleteFinished.release();
            return true;
        });
    window.testDeleteIssueWithHistory(141);
    check(tryAcquireWithEvents(historyDeleteStarted, 1000),
          QStringLiteral("history delete starts on a worker thread"));
    window.testDeleteIssueWithHistory(141);
    const bool secondDeleteStarted =
        tryAcquireWithEvents(historyDeleteStarted, 250);
    check(!secondDeleteStarted && historyDeleteCalls.load() == 1,
          QStringLiteral("second history delete request is ignored while one is running"));
    finishHistoryDelete.release(secondDeleteStarted ? 2 : 1);
    const int expectedFinishes = secondDeleteStarted ? 2 : 1;
    for (int i = 0; i < expectedFinishes; ++i) {
        check(tryAcquireWithEvents(historyDeleteFinished, 1000),
              QStringLiteral("history delete worker finishes"));
    }
    QElapsedTimer finishTimer;
    finishTimer.start();
    while (window.testIssueHistoryDeleteInProgress() && finishTimer.elapsed() < 1000)
        QApplication::processEvents();
    check(!window.testIssueHistoryDeleteInProgress(),
          QStringLiteral("history delete state clears after the worker finishes"));
    window.testSetIssueHistoryDeleteRunner({});

    constexpr int targetHeight = 520;
    window.resize(1060, targetHeight);
    QApplication::processEvents();

    const int minHintHeight = window.minimumSizeHint().height();
    const bool acceptsVerticalResize = window.height() <= targetHeight + 8;
    check(acceptsVerticalResize,
          QString("main window accepts runtime vertical resize to %1px "
                  "(actual %2px, minimum hint %3px)")
              .arg(targetHeight)
              .arg(window.height())
              .arg(minHintHeight));
    if (!acceptsVerticalResize)
        dumpTallMinimums(window);

    // Repro: opening a repo and toggling the publish/sync bar (as happens on a
    // push and when a mirror picks it up) must not grow the window on a small
    // screen.
    QTemporaryDir repoDir;
    // Name the initial branch explicitly (as initGitRepo does). On a host with
    // no init.defaultBranch the repo comes up on "master", and the switch back
    // to "main" below then pops a modal "Branch not found" that parks the whole
    // headless run.
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "init", "-q", "-b", "main"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "config", "user.email", "a@b.c"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "config", "user.name", "t"});
    {
        QFile f(repoDir.path() + QStringLiteral("/README.md"));
        if (f.open(QIODevice::WriteOnly)) { f.write("hi\n"); f.close(); }
    }
    QProcess::execute(QStringLiteral("git"), {"-C", repoDir.path(), "add", "-A"});
    QProcess::execute(QStringLiteral("git"),
                      {"-C", repoDir.path(), "commit", "-qm", "init"});
    const int repoIdx = window.testAddLocalRepository("me", "r", repoDir.path());
    window.testOpenRepository(repoIdx);
    QApplication::processEvents();

    // Historical signed PR heads can be pruned after repair/cleanup. The PR's
    // durable canonical branch remains the review source, so every PR surface
    // must resolve that instead of feeding a missing name to `git diff`.
    runGitChecked(repoDir.path(), {"branch", "pr/404", "HEAD"});
    PullRequest repairedPull;
    repairedPull.number = 404;
    repairedPull.head = QStringLiteral("api-pr/removed/historical-head");
    check(window.testResolvablePullHead(repairedPull) == QStringLiteral("pr/404"),
          QStringLiteral("a missing signed PR head falls back to pr/<number>"));

    // A remote submitter decorates its head as "<node>:<branch>".  The colon is
    // revision:path syntax to Git, so feeding the label to a range used to turn
    // `main...cache-socket-2632:fix/x` into an invalid lookup of only
    // `cache-socket-2632`.  Resolve and browse the exact local branch portion.
    runGitChecked(repoDir.path(),
                  {"branch", "fix/stall-filter-test-isolation", "HEAD"});
    PullRequest crossNodePull;
    crossNodePull.number = 405;
    crossNodePull.head =
        QStringLiteral("cache-socket-2632:fix/stall-filter-test-isolation");
    check(window.testResolvablePullHead(crossNodePull) ==
              QStringLiteral("fix/stall-filter-test-isolation"),
          QStringLiteral("a cross-node PR label resolves its synced branch without "
                         "passing node:branch to Git"));
    check(window.testSwitchToWorktreeGitBranch(crossNodePull.head) ==
              QStringLiteral("fix/stall-filter-test-isolation") &&
              window.testBranchDiffBranch() ==
                  QStringLiteral("fix/stall-filter-test-isolation"),
          QStringLiteral("opening a cross-node head diffs the branch portion, not "
                         "the cache-socket node label"));
    window.testSwitchToWorktreeGitBranch(QStringLiteral("main"));

    // adhoc #55: the status strip names the commit the open branch is on —
    // short SHA, date, subject and author. The read is detached (it must not
    // block the GUI thread), so pump the loop until it lands.
    {
        QLabel *commitInfo =
            window.findChild<QLabel *>(QStringLiteral("footerCommitInfo"));
        QElapsedTimer commitTimer;
        commitTimer.start();
        while (commitInfo && commitInfo->text().isEmpty() &&
               commitTimer.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        const QString commitText = commitInfo ? commitInfo->text() : QString();
        check(commitText.contains(QStringLiteral("init")) &&
                  commitText.contains(QStringLiteral("t")) &&
                  commitText.contains(QRegularExpression(
                      QStringLiteral("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}"))),
              QStringLiteral("status bar shows the open branch's commit "
                             "(date, message, author): ") +
                  commitText);

        // adhoc #65: the strip only fits one elided line, so hovering it pops
        // up the rest — full hash, author identity and the diffstat.
        const QString tip = commitInfo ? commitInfo->toolTip() : QString();
        check(tip.startsWith(QStringLiteral("<table")) &&
                  tip.contains(QStringLiteral("Commit: ")) &&
                  tip.contains(QStringLiteral("a@b.c")) &&
                  tip.contains(QStringLiteral("Changes: ")) &&
                  tip.contains(QStringLiteral("init")),
              QStringLiteral("hovering the status bar commit pops up its full "
                             "details (hash, author, changes): ") +
                  tip);
    }

    // Git workspace follow-up: dense edge margins, a framed commit draft,
    // shorter commit actions, no auto-view eye, a fully collapsible right pane,
    // and the worktree identity beside both history and the footer branch.
    {
        QWidget *scmPanel =
            window.findChild<QWidget *>(QStringLiteral("sourceControlPanel"));
        QMargins scmMargins;
        if (scmPanel && scmPanel->layout())
            scmMargins = scmPanel->layout()->contentsMargins();
        check(scmPanel && scmMargins.left() <= 6 && scmMargins.right() <= 6,
              QStringLiteral("source-control controls sit close to both pane edges"));
        check(window.testScmDiffUsesFullSurface(),
              QStringLiteral("working-tree diff has no outer or document padding"));

        QPlainTextEdit *draft = window.findChild<QPlainTextEdit *>(
            QStringLiteral("scmMessageInput"));
        check(draft && qApp->styleSheet().contains(
                           QStringLiteral("#scmMessageInput")),
              QStringLiteral("commit message draft owns a thin framed input style"));

        QWidget *controls =
            window.findChild<QWidget *>(QStringLiteral("scmControlsPanel"));
        bool compactActions = controls != nullptr;
        if (controls) {
            const QList<QPushButton *> buttons = controls->findChildren<QPushButton *>();
            compactActions = buttons.size() == 3;
            for (QPushButton *button : buttons)
                compactActions =
                    compactActions && button->property("buttonSize") == "xs";
        }
        check(compactActions,
              QStringLiteral("the three commit actions use the shorter compact size"));

        bool hasAutoViewEye = false;
        if (scmPanel) {
            for (QPushButton *button : scmPanel->findChildren<QPushButton *>()) {
                if (button->toolTip().contains(
                        QStringLiteral("Automatically mark files as viewed"))) {
                    hasAutoViewEye = true;
                    break;
                }
            }
        }
        check(!hasAutoViewEye,
              QStringLiteral("working-tree auto-view is enabled without an eye toggle"));

        QWidget *history =
            window.findChild<QWidget *>(QStringLiteral("gitHistoryListPage"));
        QMargins historyMargins;
        if (history && history->layout())
            historyMargins = history->layout()->contentsMargins();
        check(history && historyMargins.left() <= 6 &&
                  historyMargins.right() <= 6,
              QStringLiteral("commit graph sits close to both pane edges"));

        QSplitter *gitSplit =
            window.findChild<QSplitter *>(QStringLiteral("gitWorkspaceSplit"));
        check(gitSplit && !gitSplit->isCollapsible(0) &&
                  gitSplit->isCollapsible(1),
              QStringLiteral("Git right-hand detail pane can collapse all the way"));

        QLabel *worktree = window.findChild<QLabel *>(
            QStringLiteral("footerWorktreeInfo"));
        check(worktree && !worktree->isHidden() &&
                  worktree->text().contains(QFileInfo(repoDir.path()).fileName()) &&
                  worktree->toolTip().contains(repoDir.path()),
              QStringLiteral("status bar names the current worktree and its path"));
        bool historyNamesWorktree = false;
        for (QPushButton *button : window.findChildren<QPushButton *>()) {
            if (button->toolTip().contains(
                    QStringLiteral("Current worktree: ") + repoDir.path())) {
                historyNamesWorktree = true;
                break;
            }
        }
        check(historyNamesWorktree,
              QStringLiteral("commit history header identifies its current worktree"));
    }

    // Automatic Viewed state is a reversible scroll frontier: passing the end
    // marks files, returning to the top expands/unviews them again.
    check(window.testScmAutoViewedRoundTrip(),
          QStringLiteral("working-tree auto-view marks down and unmarks back up"));

    // Repository detail is intentionally built on first navigation. Verify the
    // real PR and Agents controls only after taking that user-visible path,
    // keeping the startup performance contract intact.
    // adhoc #7: the PR header's conflict action is a plain "Fix" that fills the
    // prompt box — no provider dropdown to pick a resolver from. adhoc #59: the
    // whole header row is icon-over-caption rail tiles ("repoActionStack"), the
    // same form as the activity rail, instead of a mix of pill shapes.
    window.testClickRepoDetailTab(4); // Pull requests
    QApplication::processEvents();
    // The pull-list actions float over the lower-right corner instead of
    // consuming a footer row.  Keep the geometry contract explicit: all five
    // repository actions form one horizontal cluster, anchored inside the
    // list pane and overlapping the table's layout area.
    {
        QWidget *bar = window.findChild<QWidget *>(
            QStringLiteral("pullListFloatingBar"));
        QWidget *pane = bar ? bar->parentWidget() : nullptr;
        QTableWidget *table =
            pane ? pane->findChild<QTableWidget *>(QStringLiteral("issueTable"))
                 : nullptr;
        check(bar && pane && table,
              QStringLiteral("pull actions are hosted by a floating list bar"));
        if (bar && pane && table) {
            check(pane->width() - bar->geometry().right() <= 12 &&
                      pane->height() - bar->geometry().bottom() <= 12 &&
                      bar->geometry().intersects(table->geometry()),
                  QStringLiteral("pull action bar floats at the list's lower-right "
                                 "corner"));
            const QList<QPushButton *> buttons =
                bar->findChildren<QPushButton *>(QString(),
                                                 Qt::FindDirectChildrenOnly);
            const QStringList expected = {QStringLiteral("New"),
                                          QStringLiteral("Directory"),
                                          QStringLiteral("Import"),
                                          QStringLiteral("Inbox"),
                                          QStringLiteral("Merged")};
            QStringList captions;
            for (QPushButton *button : buttons) {
                if (!button->text().isEmpty())
                    captions.append(button->text());
            }
            check(captions == expected,
                  QStringLiteral("all pull-list actions stay aligned in one "
                                 "floating row"));
        }
    }
    bool prFixButtonFound = false;
    bool prHeaderStyleUniform = true;
    for (QPushButton *b : window.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("Fix") && !b->menu())
            prFixButtonFound = true;
        if (b->text() == QStringLiteral("AI review") ||
            b->text() == QStringLiteral("Fix all") ||
            b->text() == QStringLiteral("Merge") ||
            b->text() == QStringLiteral("Agent fix"))
            prHeaderStyleUniform &=
                b->objectName() == QStringLiteral("repoActionStack");
    }
    check(prFixButtonFound,
          QStringLiteral("PR header offers a plain 'Fix' button with no provider "
                         "dropdown after repository navigation"));
    check(prHeaderStyleUniform,
          QStringLiteral("PR header actions all use the rail-style icon tile"));
    // The Agents tab is built when it's first opened, and a repo no longer opens
    // on it (adhoc #119) — so reach it the way a user does, from the nav strip,
    // before reading its list back. This also proves that route works for a repo
    // with no sessions yet.
    window.testOpenAgentsOverview();
    // The page is laid out on its first show, so its column widths only settle
    // once that reaches the event loop — pump until they do (bounded) rather than
    // reading a half-laid-out header below.
    {
        QElapsedTimer agentLayoutTimer;
        agentLayoutTimer.start();
        while (agentLayoutTimer.elapsed() < 5000) {
            QApplication::processEvents();
            const QStringList span =
                window.testAgentColumnLayout().section(QLatin1Char('|'), 1)
                    .split(QLatin1Char('/'));
            if (span.size() == 2 && span.at(1).toInt() > 0 &&
                span.at(0).toInt() == span.at(1).toInt())
                break;
        }
    }
    // The Agents prompt occupies the right half of the footer, matching the
    // transcript pane to the right of the page's middle splitter divider. Its
    // reserved page margin keeps the transcript and session list above it
    // instead of letting their last lines hide underneath.
    {
        QWidget *agentsPage = window.findChild<QWidget *>(
            QStringLiteral("agentsPage"));
        QWidget *footerDock = window.findChild<QWidget *>(
            QStringLiteral("logDock"));
        QWidget *promptHost = window.findChild<QWidget *>(
            QStringLiteral("promptOverlayHost"));
        const QMargins dockMargins =
            footerDock && footerDock->layout()
                ? footerDock->layout()->contentsMargins()
                : QMargins();
        const int dockContentWidth =
            footerDock ? footerDock->width() - dockMargins.left() -
                             dockMargins.right()
                       : 0;
        check(agentsPage && agentsPage->layout() && footerDock && promptHost &&
                  promptHost->isVisibleTo(&window) && dockContentWidth > 0 &&
                  promptHost->geometry().left() >=
                      dockMargins.left() + dockContentWidth / 2 - 2 &&
                  promptHost->geometry().left() <=
                      dockMargins.left() + dockContentWidth / 2 + 2 &&
                  promptHost->geometry().right() ==
                      footerDock->rect().right() - dockMargins.right() &&
                  promptHost->width() >= dockContentWidth / 2 - 2 &&
                  promptHost->width() <= dockContentWidth / 2 + 2 &&
                  agentsPage->layout()->contentsMargins().bottom() ==
                      footerDock->height(),
              QStringLiteral("Agents docks the prompt in the right half below "
                             "its transcript"));

        QPushButton *avatar = window.findChild<QPushButton *>(
            QStringLiteral("serverFooterButton"));
        QWidget *promptWrapper = window.findChild<QWidget *>(
            QStringLiteral("promptWrapper"));
        if (avatar && promptWrapper && agentsPage && agentsPage->layout() &&
            footerDock && promptHost) {
            avatar->click();
            QApplication::processEvents();
            check(!promptWrapper->isVisible() &&
                      promptHost->size() == QSize(34, 34) &&
                      promptHost->geometry().right() >= footerDock->width() - 12 &&
                      agentsPage->layout()->contentsMargins().bottom() == 0,
                  QStringLiteral("collapsing the Agents prompt leaves its circular "
                                 "avatar at the bottom-right"));

            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(avatar, &enter);
            QApplication::processEvents();
            check(promptWrapper->isVisible() &&
                      promptHost->geometry().left() >=
                          dockMargins.left() + dockContentWidth / 2 - 2 &&
                      promptHost->geometry().left() <=
                          dockMargins.left() + dockContentWidth / 2 + 2 &&
                      promptHost->geometry().right() ==
                          footerDock->rect().right() - dockMargins.right(),
                  QStringLiteral("hovering the Agents avatar restores the "
                                 "right-half prompt"));
        }
    }
    check(window.testAgentListChromeHidden(),
          QStringLiteral("agents list ships with no column header and no frame "
                         "border (adhoc #92)"));
    // The complete fleet toolbar floats over the session list's bottom-right
    // corner, preserving rows while keeping bulk actions, terminal launchers,
    // and queue controls together.
    {
        QLabel *queueStatus = window.findChild<QLabel *>(
            QStringLiteral("agentQueueStatusLabel"));
        QPushButton *decrease = window.findChild<QPushButton *>(
            QStringLiteral("agentQueueLimitDecreaseButton"));
        QPushButton *increase = window.findChild<QPushButton *>(
            QStringLiteral("agentQueueLimitIncreaseButton"));
        QPushButton *startAll = window.findChild<QPushButton *>(
            QStringLiteral("agentStartAllButton"));
        QPushButton *stopAll = window.findChild<QPushButton *>(
            QStringLiteral("agentStopAllButton"));
        QPushButton *deleteMerged = window.findChild<QPushButton *>(
            QStringLiteral("agentDeleteMergedButton"));
        QPushButton *hideDetail = window.findChild<QPushButton *>(
            QStringLiteral("issueIconButton"));
        QPushButton *claudeTerminal = window.findChild<QPushButton *>(
            QStringLiteral("agentClaudeTerminalButton"));
        QPushButton *codexTerminal = window.findChild<QPushButton *>(
            QStringLiteral("agentCodexTerminalButton"));
        QLineEdit *settingsLimit = window.findChild<QLineEdit *>(
            QStringLiteral("maxRunningAgentsEdit"));
        QWidget *queueOverlay = window.findChild<QWidget *>(
            QStringLiteral("agentQueueOverlay"));
        check(queueStatus && decrease && increase && startAll && stopAll &&
                  deleteMerged && hideDetail && claudeTerminal && codexTerminal &&
                  settingsLimit &&
                  queueOverlay &&
                  queueOverlay->parentWidget() &&
                  queueOverlay->parentWidget()->objectName() ==
                      QStringLiteral("agentsListPane") &&
                  startAll->parentWidget() == queueOverlay &&
                  stopAll->parentWidget() == queueOverlay &&
                  deleteMerged->parentWidget() == queueOverlay &&
                  hideDetail->parentWidget() == queueOverlay &&
                  claudeTerminal->parentWidget() == queueOverlay &&
                  codexTerminal->parentWidget() == queueOverlay &&
                  queueOverlay->isVisible() &&
                  queueOverlay->x() + queueOverlay->width() + 12 ==
                      queueOverlay->parentWidget()->width() &&
                  queueOverlay->y() + queueOverlay->height() + 12 ==
                      queueOverlay->parentWidget()->height() &&
                  queueStatus->text() == QStringLiteral("Queue: 0 / 5"),
              QStringLiteral("Agents fleet controls float together at the list "
                             "bottom with terminal launchers and queue controls"));
        if (queueStatus && decrease && increase && settingsLimit) {
            AgentSession runningOne;
            runningOne.id = 133890;
            runningOne.owner = QStringLiteral("me");
            runningOne.name = QStringLiteral("r");
            runningOne.prompt = QStringLiteral("Queue running-count fixture");
            runningOne.status = AgentStatus::Running;
            AgentSession runningTwo = runningOne;
            runningTwo.id = 133891;
            window.testAddAgentSession(runningOne);
            window.testAddAgentSession(runningTwo);
            QSettings().setValue(QStringLiteral("agents/maxRunning"), 11);
            window.testRefreshAgentQueueControls();
            check(queueStatus->text() == QStringLiteral("Queue: 2 / 11") &&
                      queueStatus->toolTip().contains(QStringLiteral("2 agents running")),
                  QStringLiteral("queue readout shows running agents against the "
                                 "concurrent-agent limit"));
            window.testRemoveAgentSession(runningOne.id);
            window.testRemoveAgentSession(runningTwo.id);
            QSettings().setValue(QStringLiteral("agents/maxRunning"), 5);
            window.testRefreshAgentQueueControls();
            increase->click();
            check(QSettings().value(QStringLiteral("agents/maxRunning")).toInt() == 6 &&
                      queueStatus->text() == QStringLiteral("Queue: 0 / 6") &&
                      settingsLimit->text() == QStringLiteral("6"),
                  QStringLiteral("one click raises the queue's concurrent-agent "
                                 "limit and syncs Settings"));
            decrease->click();
            check(QSettings().value(QStringLiteral("agents/maxRunning")).toInt() == 5 &&
                      queueStatus->text() == QStringLiteral("Queue: 0 / 5") &&
                      settingsLimit->text() == QStringLiteral("5"),
                  QStringLiteral("one click lowers the queue's concurrent-agent "
                                 "limit and syncs Settings"));
        }
    }
    // A restored Codex session can retain its persisted Running status after its
    // app-server transport has gone away. Its Continue action must requeue it;
    // otherwise a follow-up prompt is accepted by the UI but has no process to
    // receive it.
    {
        AgentSession detachedCodex;
        detachedCodex.id = 133892;
        detachedCodex.owner = QStringLiteral("me");
        detachedCodex.name = QStringLiteral("r");
        detachedCodex.provider = QStringLiteral("codex");
        detachedCodex.prompt = QStringLiteral("Detached Codex transport fixture");
        detachedCodex.status = AgentStatus::Running;
        window.testAddAgentSession(detachedCodex);
        check(window.testQueueDetachedRunningAgentSession(detachedCodex.id),
              QStringLiteral("a detached Running Codex session can be requeued "
                             "for a follow-up prompt"));
        window.testRemoveAgentSession(detachedCodex.id);
    }
    // adhoc #35 / #84 / #92: the list is down to "#" (the run glyph, branch chip
    // with its conflict alert, the churn bar and the age that used to have its
    // own "Updated" column) and the title, which is the column that flexes — so
    // the two always span the full list width with the title running to the
    // list's right edge rather than a fixed 320px slice.
    const QString agentColumns = window.testAgentColumnLayout();
    const QStringList agentColumnParts =
        agentColumns.split(QLatin1Char('|'));
    const QStringList agentSpan =
        agentColumnParts.size() == 2 ? agentColumnParts.at(1).split(QLatin1Char('/'))
                                     : QStringList();
    check(agentColumnParts.value(0) == QStringLiteral("#,Issue") &&
              agentSpan.size() == 2 &&
              agentSpan.at(0).toInt() == agentSpan.at(1).toInt() &&
              agentSpan.at(1).toInt() > 0,
          QStringLiteral("agents list is #/Issue with the title column absorbing "
                         "the spare width (adhoc #35/#84/#92, layout = %1)")
              .arg(agentColumns));

    // adhoc #1339: the chrome matrix is a live-session indicator, not a second
    // history list. A completed, failed, stopped, or cleared run must immediately
    // relinquish its square; work that is running, queued, or waiting for input
    // remains visible until it reaches a terminal state.
    {
        const int dotsBefore = window.testAgentDotCount();
        const auto addAgent = [&window](int id, const QString &status) {
            AgentSession session;
            session.id = id;
            session.owner = QStringLiteral("me");
            session.name = QStringLiteral("r");
            session.prompt = QStringLiteral("Dot-matrix status fixture");
            session.status = status;
            window.testAddAgentSession(session);
        };
        addAgent(133901, AgentStatus::Running);
        addAgent(133902, AgentStatus::Queued);
        addAgent(133903, AgentStatus::Waiting);
        addAgent(133904, AgentStatus::Success);
        addAgent(133905, AgentStatus::Failed);
        addAgent(133906, AgentStatus::Stopped);
        addAgent(133907, AgentStatus::Cleared);
        window.testRefreshAgentDotMatrix();
        check(window.testAgentDotCount() == dotsBefore + 7,
              QStringLiteral("agent matrix shows sessions in every lifecycle "
                             "state, including terminal history"));

        // The chrome matrix is deliberately unbounded: every agent must remain
        // visible and clickable, including sessions beyond its former
        // 66-dot limit.
        for (int id = 133908; id < 133978; ++id)
            addAgent(id, AgentStatus::Running);
        window.testRefreshAgentDotMatrix();
        check(window.testAgentDotCount() == dotsBefore + 77,
              QStringLiteral("agent matrix keeps every session visible "
                             "beyond 66 dots"));

        window.testSetAgentSessionStatus(133901, AgentStatus::Success);
        window.testSetAgentSessionStatus(133902, AgentStatus::Success);
        window.testSetAgentSessionStatus(133903, AgentStatus::Success);
        for (int id = 133908; id < 133978; ++id)
            window.testSetAgentSessionStatus(id, AgentStatus::Success);
        check(window.testAgentDotCount() == dotsBefore + 77,
              QStringLiteral("agent matrix retains each dot when its session "
                             "reaches a terminal state"));

        for (int id = 133901; id < 133978; ++id)
            window.testRemoveAgentSession(id);
    }

    // The source-of-truth inbox count is one response shared by all three
    // collaboration tabs. Each action must surface its own value before the
    // user opens the review list.
    window.testSetPendingInboxCounts(3, 2, 4);
    check(window.testIssueInboxBadgeCount() == 3 &&
              window.testPullInboxBadgeCount() == 2 &&
              window.testDiscussionInboxButtonText().contains(
                  QStringLiteral("4")),
          QStringLiteral("issue, PR, and discussion inbox actions show their "
                         "pending submission counts"));

    // adhoc #1541: the Inbox tile only exists while the Pulls toolbar is on
    // screen, so a pull request waiting on the nodes also rides the Pulls tab as
    // a red count next to its blue open-PR total.
    check(window.testPullsTabAlertBadgeCount() == 2,
          QStringLiteral("pull requests waiting in the inbox show as a red count "
                         "on the Pulls tab"));
    window.testSetPendingInboxCounts(0, 0, 0);
    check(window.testPullsTabAlertBadgeCount() == 0,
          QStringLiteral("the Pulls tab drops its red count once the inbox is "
                         "drained"));
    window.testSetPendingInboxCounts(3, 2, 4);

    QPushButton *legacyIssueBounty = window.findChild<QPushButton *>(
        QStringLiteral("legacyIssueBountyDisabled"));
    check(window.findChild<QLabel *>(
              QStringLiteral("legacyBountyWalletDisabled")) != nullptr &&
              legacyIssueBounty && !legacyIssueBounty->isEnabled(),
          QStringLiteral("legacy bounty funding controls are visibly disabled "
                         "after their pages are visited"));

    // Issue #286: the "Prioritize from README" button must actually be on the
    // open issues view (not hidden, not pushed off the right edge of the panel).
    {
        window.resize(1100, 800);
        QApplication::processEvents();
        const bool shown = window.testShowRepoIssuesTab();
        QApplication::processEvents();
        QPushButton *pb =
            findButtonStartingWith(window, "Prioritize from README");
        const bool realized = pb && pb->isVisibleTo(&window);
        bool onScreen = false;
        if (realized) {
            const QPoint tl = pb->mapTo(&window, QPoint(0, 0));
            onScreen = tl.x() >= 0 && tl.x() + pb->width() <= window.width();
        }
        check(shown && realized && onScreen,
              QString("issue #286 prioritize button is visible on the issues "
                      "view (shown=%1 realized=%2 onScreen=%3)")
                  .arg(shown)
                  .arg(realized)
                  .arg(onScreen));

        // Issue #369: on laptop-width screens, the Issues view plus footer
        // prompt must not advertise a desktop-only horizontal minimum. The
        // offscreen test window may still resize, but macOS honors this hint
        // when deciding how far the user can drag the window narrower.
        const int createdNumber =
            window.testQuickAddIssueNoAgent(QStringLiteral("Narrow window issue"));
        for (int i = 0; i < 36; ++i) {
            AgentSession session;
            session.id = 5000 + i;
            session.owner = QStringLiteral("me");
            session.name = QStringLiteral("r");
            session.prompt = QStringLiteral("Narrow status strip session %1").arg(i);
            session.status = (i % 5 == 0) ? AgentStatus::Failed
                                          : AgentStatus::Running;
            window.testAddAgentSession(session);
        }
        window.testRefreshAgentDotMatrix();
        window.resize(900, 650);
        QApplication::processEvents();
        const int minHintWidth = window.minimumSizeHint().width();
        check(createdNumber > 0 && minHintWidth <= 900,
              QString("issues view and footer prompt fit within a 900px laptop "
                      "window (issue %1, minimum hint %2px)")
                  .arg(createdNumber)
                  .arg(minHintWidth));
        if (createdNumber <= 0 || minHintWidth > 900)
            dumpWideMinimums(window);

        window.resize(1500, 650);
        QApplication::processEvents();
        window.testShowRepoIssuesTab();
        QApplication::processEvents();
        const int tabGap = window.testRepoTabGapAroundIssues();
        const int looperGap = window.testIssueLooperGapFromNewIssueButton();
        const bool looperAligned = window.testIssueLooperRowAligned();
        const int navTrailingGap = window.testTopNavTrailingGap();
        check(tabGap >= 8 && looperGap >= -1 && looperAligned &&
                  navTrailingGap >= 0 && navTrailingGap <= 28,
              QString("responsive top rows keep breathing room and place the "
                      "issue looper beside New issue (tabGap=%1 looperGap=%2 "
                      "looperAligned=%3 navTrailingGap=%4)")
                  .arg(tabGap)
                  .arg(looperGap)
                  .arg(looperAligned)
                  .arg(navTrailingGap));
        // Adhoc #421: the left rail's first icon sits on the tab row's icon
        // line, so the two rows of glyphs read as one horizontal band.
        const int railSkew = window.testRailTabIconLineSkew();
        check(qAbs(railSkew) <= 1,
              QString("the activity rail's icons line up with the repo tab "
                      "row's (skew=%1px)")
                  .arg(railSkew));

    // Issue #207: the commit detail page must expose a restore/revert action
    // beside the destructive delete-history action.
    {
        window.resize(1100, 800);
        window.testClickRepoDetailTab(1); // Commits
        QApplication::processEvents();
        const bool commitOpened = window.testOpenMostRecentCommit();
        QApplication::processEvents();
        QPushButton *deleteCommit = findButtonStartingWith(window, "Delete commit");
        QPushButton *restoreCommit = findButtonStartingWith(window, "Restore commit");
        bool adjacent = false;
        bool restoreOnScreen = false;
        if (deleteCommit && restoreCommit) {
            const QPoint deleteTopLeft = deleteCommit->mapTo(&window, QPoint(0, 0));
            const QPoint restoreTopLeft = restoreCommit->mapTo(&window, QPoint(0, 0));
            adjacent = restoreTopLeft.y() == deleteTopLeft.y() &&
                       restoreTopLeft.x() >= deleteTopLeft.x() + deleteCommit->width();
            restoreOnScreen = restoreTopLeft.x() >= 0 &&
                              restoreTopLeft.x() + restoreCommit->width() <=
                                  window.width();
        }
        check(commitOpened && deleteCommit && restoreCommit &&
                  restoreCommit->isVisibleTo(&window) && adjacent && restoreOnScreen,
              QString("commit restore button is visible beside delete "
                      "(delete=%1 restore=%2 visible=%3 adjacent=%4 onScreen=%5 "
                      "windowW=%6 restoreX=%7 restoreW=%8 opened=%9)")
                  .arg(deleteCommit != nullptr)
                  .arg(restoreCommit != nullptr)
                  .arg(restoreCommit && restoreCommit->isVisibleTo(&window))
                  .arg(adjacent)
                  .arg(restoreOnScreen)
                  .arg(window.width())
                  .arg(restoreCommit ? restoreCommit->mapTo(&window, QPoint(0, 0)).x() : -1)
                  .arg(restoreCommit ? restoreCommit->width() : -1)
                  .arg(commitOpened));
    }
    }

    window.resize(480, 420);
    QApplication::processEvents();
    // adhoc #374 removed the floating publish/sync pill that used to hover in the
    // band above the Code tab (and with it the "toggling it must not reflow the
    // page" repro): nothing may float there any more.
    check(!findButtonStartingWith(window, "Sync (") &&
              !findButtonStartingWith(window, "Syncing"),
          QStringLiteral("no floating Sync button above the Code tab"));

    // Outgoing commits belong inside Source Control, with one safe Sync action.
    // Give the earlier upstream fixture a real merge so this also exercises the
    // graph's branch-out/loop-in topology and local/remote ref badge metadata.
    if (upstreamRepo.isValid()) {
        const bool outgoingHistory =
            runGitChecked(upstreamRepo.path(),
                          {"checkout", "-b", "feature/graph-loop"}) &&
            runGitChecked(upstreamRepo.path(),
                          {"commit", "--allow-empty", "-m", "feature lane"}) &&
            runGitChecked(upstreamRepo.path(), {"checkout", "main"}) &&
            runGitChecked(upstreamRepo.path(),
                          {"commit", "--allow-empty", "-m", "main lane"}) &&
            runGitChecked(upstreamRepo.path(),
                          {"merge", "--no-ff", "feature/graph-loop", "-m",
                           "merge graph loop"});
        const int outgoingRepoIndex = window.testAddLocalRepository(
            QStringLiteral("me"), QStringLiteral("outgoing-repo"),
            upstreamRepo.path());
        window.testOpenRepository(outgoingRepoIndex);
        QElapsedTimer railSyncTimer;
        railSyncTimer.start();
        while (window.testGitPendingSyncCount() != 3 &&
               railSyncTimer.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        const bool railMarkedBeforeOpen =
            window.testGitPendingSyncCount() == 3;
        window.testClickRailGitButton();

        QWidget *outgoingPanel = window.findChild<QWidget *>(
            QStringLiteral("scmOutgoingPanel"));
        QPushButton *syncChanges = window.findChild<QPushButton *>(
            QStringLiteral("scmSyncButton"));
        QLabel *outgoingLabel = window.findChild<QLabel *>(
            QStringLiteral("scmOutgoingLabel"));
        QElapsedTimer outgoingTimer;
        outgoingTimer.start();
        while (outgoingPanel && !outgoingPanel->isVisibleTo(&window) &&
               outgoingTimer.elapsed() < 5000)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        check(outgoingHistory && outgoingPanel && syncChanges && outgoingLabel &&
                  !outgoingPanel->isVisibleTo(&window) &&
                  syncChanges->isVisibleTo(&window) &&
                  syncChanges->text().contains(QStringLiteral("3↑")) &&
                  syncChanges->isEnabled() &&
                  outgoingLabel->text().contains(QStringLiteral("main")) &&
                  railMarkedBeforeOpen && window.testGitPendingSyncCount() == 3,
              QString("Source Control promotes the enabled Sync Changes action "
                      "while outgoing commits are pending (history=%1 cardHidden=%2 "
                      "buttonVisible=%3 button=%4 label=%5)")
                  .arg(outgoingHistory)
                  .arg(outgoingPanel && !outgoingPanel->isVisibleTo(&window))
                  .arg(syncChanges && syncChanges->isVisibleTo(&window))
                  .arg(syncChanges ? syncChanges->text()
                                   : QStringLiteral("<missing>"))
                  .arg(outgoingLabel ? outgoingLabel->text()
                                     : QStringLiteral("<missing>")));

        QTableWidget *graph = window.findChild<QTableWidget *>(
            QStringLiteral("commitsList"));
        bool localRef = false;
        bool remoteRef = false;
        bool mergeLoop = false;
        bool outgoingTopRow = false;
        if (graph) {
            if (graph->rowCount() > 0) {
                QTableWidgetItem *topSummary = graph->item(0, 6);
                QTableWidgetItem *topLane = graph->item(0, 8);
                outgoingTopRow = topSummary && topLane &&
                                 topSummary->data(Qt::UserRole + 35).toBool() &&
                                 topLane->data(Qt::UserRole + 20).toList().isEmpty() &&
                                 topLane->data(Qt::UserRole + 22).toList() ==
                                     QVariantList{0};
            }
            for (int row = 0; row < graph->rowCount(); ++row) {
                if (QTableWidgetItem *summary = graph->item(row, 6)) {
                    const QStringList kinds =
                        summary->data(Qt::UserRole + 34).toStringList();
                    localRef = localRef || kinds.contains(QStringLiteral("local"));
                    remoteRef = remoteRef || kinds.contains(QStringLiteral("remote"));
                }
                if (QTableWidgetItem *lane = graph->item(row, 8)) {
                    const QVariantList top =
                        lane->data(Qt::UserRole + 20).toList();
                    const QVariantList bottom =
                        lane->data(Qt::UserRole + 22).toList();
                    mergeLoop = mergeLoop ||
                                (lane->data(Qt::UserRole + 32).toBool() &&
                                 top != bottom);
                }
            }
        }
        check(outgoingTopRow && localRef && remoteRef && mergeLoop,
              QString("commit graph links an outgoing dotted top row to local "
                      "target refs, remote cloud refs, and a merge loop "
                      "(outgoing=%1 local=%2 remote=%3 loop=%4)")
                  .arg(outgoingTopRow)
                  .arg(localRef)
                  .arg(remoteRef)
                  .arg(mergeLoop));

        // adhoc #66: those same outgoing commits used to swap the whole commit
        // row out for Sync Changes, so a staged change with a typed message had
        // no button left to record it. Stage one now and the commit actions must
        // come back — alongside Sync, not instead of it.
        {
            QFile waiting(upstreamRepo.path() +
                          QStringLiteral("/commit-waiting.txt"));
            waiting.open(QIODevice::WriteOnly);
            waiting.write("staged and waiting on a commit\n");
            waiting.close();
        }
        const bool stagedWaiting =
            runGitChecked(upstreamRepo.path(), {"add", "commit-waiting.txt"});
        window.testRefreshSourceControl();
        QElapsedTimer waitingTimer;
        waitingTimer.start();
        while (waitingTimer.elapsed() < 5000 &&
               !window.testSourceControlPaths().contains(
                   QStringLiteral("commit-waiting.txt")))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        const QString controls = window.testScmCommitControlsState();
        check(stagedWaiting &&
                  window.testSourceControlPaths().contains(
                      QStringLiteral("commit-waiting.txt")) &&
                  controls.contains(QStringLiteral("commit=enabled")) &&
                  controls.contains(QStringLiteral("commitPush=enabled")) &&
                  controls.contains(QStringLiteral("stagePush=enabled")) &&
                  !controls.contains(QStringLiteral("sync=hidden")),
              QString("a staged change waiting to be committed keeps the commit "
                      "buttons on screen while outgoing commits are pending "
                      "(staged=%1 files=%2 controls=%3)")
                  .arg(stagedWaiting)
                  .arg(window.testSourceControlPaths().join(QStringLiteral(", ")),
                       controls));
        check(window.testClickSourceControlPath(QStringLiteral("commit-waiting.txt")) &&
                  window.testScmDiffFilePinnedToTop(
                      QStringLiteral("commit-waiting.txt")),
              QStringLiteral("clicking a working-tree file pins its sticky filename "
                             "at the top of the diff"));

        // …and once that change is committed the row hands itself back to Sync,
        // which is the behaviour the swap was there for in the first place.
        const bool committedWaiting =
            runGitChecked(upstreamRepo.path(),
                          {"commit", "-m", "commit the waiting change"});
        window.testRefreshSourceControl();
        QElapsedTimer cleanTimer;
        cleanTimer.start();
        while (cleanTimer.elapsed() < 5000 &&
               !window.testScmCommitControlsState().contains(
                   QStringLiteral("commit=hidden")))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        const QString cleanControls = window.testScmCommitControlsState();
        check(committedWaiting &&
                  cleanControls.contains(QStringLiteral("commit=hidden")) &&
                  cleanControls.contains(QStringLiteral("commitPush=hidden")) &&
                  cleanControls.contains(QStringLiteral("stagePush=hidden")) &&
                  !cleanControls.contains(QStringLiteral("sync=hidden")),
              QString("a clean working tree still gives the row to Sync Changes "
                      "(committed=%1 controls=%2)")
                  .arg(committedWaiting)
                  .arg(cleanControls));
    }

    // issue #272: clicking "Update from main" rebuilds the worktrees panel. The
    // rebuild must keep the same worktree selected so its diff/detail pane stays
    // on screen instead of going blank.
    QTemporaryDir wtRepo;
    if (initGitRepo(wtRepo)) {
        // A base-tracked file that the feature worktree deletes exercises the
        // complete-snapshot diff path. Deleted files must come from Git's
        // temporary index without trying to stat a path that no longer exists.
        {
            QFile baseFile(wtRepo.path() + QStringLiteral("/base-delete.txt"));
            baseFile.open(QIODevice::WriteOnly);
            baseFile.write("tracked on main\n");
            baseFile.close();

            // Both main and the linked agent worktree will later edit this same
            // tracked file in separate hunks. A plain merge refuses before it
            // even considers whether the text edits overlap; Pull main must
            // autostash, update, and restore the agent edit instead.
            QFile shared(wtRepo.path() +
                         QStringLiteral("/auto-stash-overlap.txt"));
            shared.open(QIODevice::WriteOnly);
            for (int i = 1; i <= 12; ++i)
                shared.write(QStringLiteral("line %1\n").arg(i).toUtf8());
            shared.close();
        }
        runGitChecked(wtRepo.path(),
                      {"add", "base-delete.txt", "auto-stash-overlap.txt"});
        runGitChecked(wtRepo.path(), {"commit", "-m", "base file for deletion"});
        runGitChecked(wtRepo.path(), {"branch", "feature/keep-selected"});
        // A second branch so the compare base has somewhere else to point
        // (adhoc #16 — the base end of "<branch> -> <base>" is switchable).
        runGitChecked(wtRepo.path(), {"branch", "feature/other-base"});
        const QString wtPath = wtRepo.path() + QStringLiteral("/wt-keep");
        runGitChecked(wtRepo.path(),
                      {"worktree", "add", wtPath, "feature/keep-selected"});
        // Put the worktree's branch one commit ahead of main so the ahead/behind
        // column and compact file/churn badges have something non-trivial to
        // report.
        {
            QFile changed(wtPath + QStringLiteral("/branch-change.txt"));
            changed.open(QIODevice::WriteOnly);
            changed.write("one added line\n");
            changed.close();
        }
        runGitChecked(wtPath, {"add", "branch-change.txt"});
        runGitChecked(wtPath, {"commit", "-m", "ahead by one"});
        const int wtIdx =
            window.testAddLocalRepository("me", "wtrepo", wtRepo.path());
        window.testOpenRepository(wtIdx);
        QApplication::processEvents();
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        check(window.testSelectedWorktreeBranch() ==
                  QStringLiteral("feature/keep-selected"),
              QStringLiteral("selecting a worktree records it as the selection"));
        check(window.testWorktreeBranchLabel().contains(
                  QStringLiteral("feature/keep-selected")),
              QStringLiteral("the worktree detail shows which branch it's on"));
        check(window.testWorktreeBranchLabel().contains(QStringLiteral("wt-keep")),
              QStringLiteral("the worktree detail shows the worktree's location"));
        window.testReloadWorktreesPanel(); // what "Update from main" does after merging
        check(window.testSelectedWorktreeBranch() ==
                  QStringLiteral("feature/keep-selected"),
              QStringLiteral("reloading the worktrees panel keeps the selected "
                             "worktree instead of going blank (#272)"));
        // The ahead/behind column is filled by an async `git rev-list`; pump the
        // event loop until it lands, then check it reports "1 ahead" (↑1).
        QString abText;
        QElapsedTimer abTimer;
        abTimer.start();
        while (abTimer.elapsed() < 5000) {
            QApplication::processEvents();
            abText = window.testWorktreeAheadBehindText(
                QStringLiteral("feature/keep-selected"));
            if (!abText.isEmpty() && !abText.contains(QStringLiteral("checking")))
                break;
        }
        check(abText == QString::fromUtf8("\xE2\x86\x91""1"),
              QString("worktrees list shows the branch one commit ahead of main "
                      "(ahead/behind cell = %1)").arg(abText));
        const QStringList visibleWorktreeBranches = window.testWorktreeBranches();
        check(!visibleWorktreeBranches.contains(QStringLiteral("forkmesh/pulls")),
              QString("the private pull-metadata worktree stays out of the "
                      "Worktrees tab (%1)")
                  .arg(visibleWorktreeBranches.join(QStringLiteral(", "))));

        // Opening the Worktrees tab the way a user does (clicking its nav button)
        // should hand keyboard focus to the table, so arrow keys work right away
        // without first clicking a row.
        window.testClickRepoDetailTab(window.testWorktreesTabIndex());
        QApplication::processEvents();
        check(window.testWorktreesTableHasKeyboardFocus(),
              QStringLiteral("opening the Worktrees tab focuses the table so the "
                             "arrow keys can move through its rows"));

        // With feature/keep-selected (the bottom row) selected, an Up arrow on the
        // table should move the selection to the *other* worktree row; Down stays
        // put because it's already the last row.
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        const QString afterUp = window.testArrowOnWorktrees(false);
        window.testSwitchToWorktree(QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        const QString afterDown = window.testArrowOnWorktrees(true);
        qInfo("arrow nav: up->%s down->%s",
              qPrintable(afterUp), qPrintable(afterDown));
        check(!afterUp.isEmpty() &&
                  afterUp != QStringLiteral("feature/keep-selected"),
              QStringLiteral("arrow up moves the worktree selection to the row above"));
        check(afterDown == QStringLiteral("feature/keep-selected"),
              QStringLiteral("arrow down on the last worktree row stays put"));

        // adhoc #183 (the "and more" tables): the Releases and Mirror-nodes tabs
        // are single-list tables too, so opening either should also hand keyboard
        // focus to its table for immediate arrow-key navigation.
        window.testClickRepoDetailTab(window.testReleasesTabIndex());
        QApplication::processEvents();
        check(window.testReleasesTableHasKeyboardFocus(),
              QStringLiteral("opening the Releases tab focuses its table for "
                             "arrow-key navigation"));
        window.testClickRepoDetailTab(window.testMirrorNodesTabIndex());
        QApplication::processEvents();
        check(window.testMirrorNodesTableHasKeyboardFocus(),
              QStringLiteral("opening the Mirror-nodes tab focuses its table for "
                             "arrow-key navigation"));

        // adhoc #46: a node that re-registers (reinstall → new key) transiently
        // sits in the roster under two identities with the same name while the
        // old key's session still heartbeats. The Mirror nodes list must show
        // such a node once, and the newer identity wins the row.
        {
            QList<MemberInfo> dupRoster;
            dupRoster.append(testMember(QStringLiteral("self-node"),
                                        QStringLiteral("Self Node"), true));
            MirrorAdvert advert;
            advert.ownerName = QStringLiteral("mirror1/wtrepo");
            advert.source = QStringLiteral("me/wtrepo");
            advert.branch = QStringLiteral("main");
            advert.commit =
                QStringLiteral("1111111111111111111111111111111111111111");
            advert.updatedMs = 1000;
            MemberInfo oldIdentity = testMember(QStringLiteral("mirror1-old-key"),
                                                QStringLiteral("alice"));
            oldIdentity.nodeName = QStringLiteral("mirror1");
            oldIdentity.version = QStringLiteral("0.5.9");
            oldIdentity.mirrorDetails.append(advert);
            MemberInfo newIdentity = testMember(QStringLiteral("mirror1-new-key"),
                                                QStringLiteral("alice"));
            newIdentity.nodeName = QStringLiteral("mirror1");
            newIdentity.version = QStringLiteral("0.5.30");
            newIdentity.platform = QStringLiteral("linux");
            newIdentity.ownerUser = QStringLiteral("alice");
            newIdentity.diskUsedBytes = 40 * 1024 * 1024;
            newIdentity.diskTotalBytes = 100 * 1024 * 1024;
            newIdentity.diagnostics = {
                NodeDiagnostics::Finding{QStringLiteral("relay-flap"),
                                         NodeDiagnostics::Warning,
                                         QStringLiteral("Relay link dropped 6 times in the last 30m")}};
            newIdentity.diagnosticsMs = QDateTime::currentMSecsSinceEpoch();
            advert.worktreeCount = 3;
            newIdentity.mirrorDetails.append(advert);
            MemberInfo offlineNode = testMember(QStringLiteral("offline-key"),
                                                QStringLiteral("offline-node"));
            offlineNode.online = false;
            MirrorAdvert offlineAdvert = advert;
            offlineAdvert.ownerName = QStringLiteral("offline-node/wtrepo");
            offlineNode.mirrorDetails.append(offlineAdvert);
            dupRoster.append(oldIdentity);
            dupRoster.append(newIdentity);
            dupRoster.append(offlineNode);
            // Set the roster directly and rebuild just the Mirror nodes panel:
            // routing this through the full setRoster (which also rebuilds the
            // repo/node switcher) isn't needed to exercise loadMirrorNodesPanel's
            // row-building, and the switcher rebuild would close the open repo
            // detail since the repo-owning node ("me") isn't in this synthetic
            // roster snapshot.
            window.testSetHomeRosterAndReloadMirrorPanel(dupRoster);
            check(window.testMirrorNodesOnlineOnlyChecked(),
                  QStringLiteral("Mirror nodes defaults to showing online nodes only"));
            const QStringList mirrorRows = window.testMirrorNodeRows();
            int mirror1Rows = 0;
            QString mirror1Id;
            bool sawOffline = false;
            for (const QString &row : mirrorRows) {
                if (row.startsWith(QStringLiteral("mirror1"))) {
                    ++mirror1Rows;
                    mirror1Id = row.section(QLatin1Char('|'), 1);
                }
                if (row.startsWith(QStringLiteral("offline-node")))
                    sawOffline = true;
            }
            check(mirror1Rows == 1,
                  QString("a node re-registered under a new key shows one Mirror "
                          "nodes row, not one per identity (rows: %1)")
                      .arg(mirrorRows.join(QStringLiteral(" ; "))));
            check(mirror1Id == QStringLiteral("mirror1-new-key"),
                  QString("the newer identity wins the deduped Mirror nodes row "
                          "(got id %1)").arg(mirror1Id));
            check(window.testMirrorNodeCardsAreCompact(),
                  QStringLiteral("Mirror nodes render as three-row cards with "
                                 "resource gauges and live node, sync, commit, "
                                 "health, and reachability controls"));
            check(!sawOffline,
                  QStringLiteral("offline mirror nodes are hidden while Online only is checked"));
            check(window.testMirrorNodeCellText(QStringLiteral("mirror1"), 2) ==
                      QStringLiteral("alice"),
                  QStringLiteral("Mirror nodes Owner column shows the node owner"));
            // Hidden data-model columns remain stable behind the card: Node,
            // Sync, Owner, commit identity, sync facts, repo counts, pending
            // inbox counts, Health, then CPU/RAM/Disk/Platform.
            check(window.testMirrorNodeCellToolTip(QStringLiteral("mirror1"), 20)
                      .startsWith(QStringLiteral("Disk:")),
                  QStringLiteral("Mirror nodes Disk column contains disk usage, not platform text"));
            check(window.testMirrorNodeCellText(QStringLiteral("mirror1"), 21) ==
                      QStringLiteral("linux"),
                  QStringLiteral("Mirror nodes Platform column stays aligned after Disk"));
            const QString healthTip = window.testMirrorNodeCellToolTip(
                QStringLiteral("mirror1"), 13);
            check(healthTip.contains(QStringLiteral("Warning [relay-flap]")) &&
                      healthTip.contains(QStringLiteral("Correlate disconnect times")) &&
                      healthTip.contains(QStringLiteral("draft a troubleshooting prompt")),
                  QStringLiteral("Mirror nodes Health explains the finding, next checks, "
                                 "and its prompt action"));
            check(window.testDraftMirrorNodeDiagnostics(QStringLiteral("mirror1")),
                  QStringLiteral("activating Mirror nodes Health drafts its diagnostic prompt"));
            const QString diagnosticPrompt = window.testQuickAddText();
            check(diagnosticPrompt.contains(QStringLiteral("Node: mirror1")) &&
                      diagnosticPrompt.contains(QStringLiteral("Node id: mirror1-new-key")) &&
                      diagnosticPrompt.contains(QStringLiteral("Platform: linux")) &&
                      diagnosticPrompt.contains(QStringLiteral("Repository mirrored:")) &&
                      diagnosticPrompt.contains(QStringLiteral("Warning [relay-flap]")) &&
                      diagnosticPrompt.contains(QStringLiteral("Find the root cause")),
                  QStringLiteral("the mirror diagnostic prompt carries actionable node, repo, "
                                 "and finding context"));
            window.testSetMirrorNodesOnlineOnly(false);
            QApplication::processEvents();
            const QStringList unfilteredRows = window.testMirrorNodeRows();
            check(unfilteredRows.join(QStringLiteral("\n"))
                      .contains(QStringLiteral("offline-node")),
                  QStringLiteral("unchecking Online only shows offline mirror nodes"));

            // adhoc #375: every git read the panel makes (our own advert's
            // head/counts, the `git show` naming each row's commit) pumps the
            // event loop on the GUI thread, so a queued rebuild — a roster
            // heartbeat, a /mirrors reply — can land in the middle of one. The
            // half-built table must not gain a second set of rows from it:
            // that listed every node twice, the duplicates carrying only a
            // name because the rebuild that filled the rest cleared them.
            QTimer::singleShot(0, &window, [&window, dupRoster]() {
                window.testSetHomeRosterAndReloadMirrorPanel(dupRoster);
            });
            // waitForGit only pumps up front once 100ms have passed since the
            // last pump, so wait that out: the reload queued above is then
            // delivered from inside the rebuild below rather than after it.
            QThread::msleep(150);
            window.testSetHomeRosterAndReloadMirrorPanel(dupRoster);
            QApplication::processEvents();
            const QStringList reentrantRows = window.testMirrorNodeRows();
            QSet<QString> seenNodeNames;
            QStringList duplicatedNodes;
            for (const QString &row : reentrantRows) {
                const QString name = row.section(QLatin1Char('|'), 0, 0);
                if (seenNodeNames.contains(name))
                    duplicatedNodes.append(name);
                seenNodeNames.insert(name);
            }
            check(duplicatedNodes.isEmpty(),
                  QString("a rebuild delivered while the Mirror nodes panel is "
                          "building doesn't list nodes twice (adhoc #375, "
                          "duplicates: %1; rows: %2)")
                      .arg(duplicatedNodes.join(QStringLiteral(", ")),
                           reentrantRows.join(QStringLiteral(" ; "))));
        }

        // issue #172: the Branches list must also surface the worktree a branch
        // is checked out in, so an agent's isolated working tree is visible
        // without a trip to the Worktrees tab.
        window.testReloadBranchesPanel();
        const QString listedWt =
            window.testBranchWorktreePath(QStringLiteral("feature/keep-selected"));
        check(listedWt.contains(QStringLiteral("wt-keep")),
              QString("branches list shows the worktree a branch is checked out "
                      "in (#172, worktree cell = %1)").arg(listedWt));
        // The default branch lives in the main checkout, not a linked worktree,
        // so its Worktree cell stays empty rather than pointing at the main tree.
        check(window.testBranchWorktreePath(QStringLiteral("main")).isEmpty(),
              QStringLiteral("a branch checked out in the main tree has an empty "
                             "Worktree cell (#172)"));
        check(window.testBranchesUseCompactColumns(),
              QStringLiteral("Branches folds Updated and Worktree into its compact "
                             "Agent-style leading cell"));
        check(window.testBranchesKeepFlexibleNameColumn(),
              QStringLiteral("Branches keeps its leading cell flexible so inline "
                             "metadata and branch names do not overlap"));
        check(window.testBranchDelegatePaintsSingleTextLayer(
                  QStringLiteral("feature/keep-selected")),
              QStringLiteral("Branches leaves text and icons out of the style "
                             "background layer so its custom row paints each "
                             "branch name exactly once"));
        check(window.testBranchSelectedTextColorIsReadable(
                  QStringLiteral("feature/keep-selected")),
              QStringLiteral("Branches keeps its normal readable text colour "
                             "inside the transparent selected-row outline"));
        const QString branchBadges = window.testBranchVisualBadges(
            QStringLiteral("feature/keep-selected"));
        check(branchBadges.startsWith(QStringLiteral("1|1|0|1|0|")),
              QString("Branches leading cell carries file count, +/- churn, "
                      "worktree and conflict data (got %1)").arg(branchBadges));
        check(branchBadges.section(QLatin1Char('|'), 6, 6) ==
                      QStringLiteral("0") &&
                  branchBadges.section(QLatin1Char('|'), 7, 7) ==
                      QStringLiteral("1"),
              QString("Branches leading cell carries the behind/ahead chart data "
                      "beside file churn (got %1)").arg(branchBadges));

        // Keep an agent session attached to this branch: the dedicated Issue /
        // Agent column is gone, but its status remains useful as the leading
        // branch glyph and the session is reused by the agent-route checks below.
        AgentSession issueSession;
        issueSession.id = 4242;
        issueSession.owner = QStringLiteral("me");
        issueSession.name = QStringLiteral("wtrepo");
        issueSession.branchName = QStringLiteral("feature/keep-selected");
        issueSession.issueNumber = 191;
        issueSession.issueTitle = QStringLiteral(
            "show attachment in branches list with a deliberately complete agent "
            "session title that remains available all the way to the pane edge");
        window.testAddAgentSession(issueSession);
        window.testReloadBranchesPanel();

        // adhoc #251: a branch an agent is working must also carry the agent's
        // status icon in that cell (a spinner while running, a check on success,
        // …), so the list shows how each agent is doing at a glance. A plain
        // branch with no session carries no icon.
        check(window.testBranchAttachmentHasIcon(
                  QStringLiteral("feature/keep-selected")),
              QStringLiteral("branches list stamps the agent's status icon on a "
                             "branch an agent is working (adhoc #251)"));
        check(!window.testBranchAttachmentHasIcon(QStringLiteral("main")),
              QStringLiteral("a branch with no agent session carries no status "
                             "icon (adhoc #251)"));

        check(window.testRenderAgentDetailTitle(issueSession.issueTitle) ==
                      issueSession.issueTitle &&
                  !window.testAgentDetailTitleWraps(),
              QString("agent detail keeps the complete session title on one line "
                      "and lets only the pane edge clip it (got: %1, wraps=%2)")
                  .arg(window.testAgentDetailTitleText())
                  .arg(window.testAgentDetailTitleWraps()));

        // adhoc #185: the default branch must stay pinned to the top of the list.
        // feature/keep-selected was committed to more recently (it's a worktree one
        // commit ahead of main), so a plain committer-date sort would float it above
        // main; the panel must override that and list main first.
        const QStringList order = window.testBranchRowOrder();
        qInfo("branch row order: %s", qPrintable(order.join(QStringLiteral(", "))));
        check(!order.isEmpty() && order.first() == QStringLiteral("main"),
              QString("the default branch is pinned to the top of the branches "
                      "list (adhoc #185, first row = %1)")
                  .arg(order.isEmpty() ? QStringLiteral("<none>") : order.first()));

        // A Code overview branch row is navigation, not an inline review: its
        // only diff destination is the universal Git workspace.
        window.testClickRepoDetailTab(window.testBranchesTabIndex());
        QApplication::processEvents();
        check(window.testOverviewBodyPage() == 2 &&
                  !window.testBranchesPanelOwnsDiffView(),
              QString("Code overview's Branches page contains only the branch "
                      "list, not a diff viewer (page = %1)")
                  .arg(window.testOverviewBodyPage()));
        const bool branchRowClicked = window.testClickBranchRowInOverview(
            QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        check(branchRowClicked && window.testOverviewBodyPage() == 1 &&
                  window.testCommitWorkspacePage() == 2 &&
                  window.testBrowsedBranch() ==
                      QStringLiteral("feature/keep-selected"),
              QString("clicking Code overview > Branches opens that branch in "
                      "Git (clicked = %1, overview page = %2, diff page = %3, "
                      "branch = %4)")
                  .arg(branchRowClicked)
                  .arg(window.testOverviewBodyPage())
                  .arg(window.testCommitWorkspacePage())
                  .arg(window.testBrowsedBranch()));
        check(window.testGitWorkspaceIsExclusive(),
              QStringLiteral("a branch diff gives the Git rail exclusive ownership: "
                             "no Code chrome and no visible diff outside Git"));
        check(window.testGitPromptFloatsBottomLeft(),
              QStringLiteral("Git keeps the global prompt overlay at the "
                             "lower-left without reserving footer height"));
        // Returning through the Code route keeps the same global prompt overlay;
        // it is never reparented into a page-specific footer.
        window.testClickRepoDetailTab(0);
        QApplication::processEvents();
        QFrame *promptWrapper = window.findChild<QFrame *>(
            QStringLiteral("promptWrapper"));
        QWidget *footerDock = window.findChild<QWidget *>(
            QStringLiteral("logDock"));
        QWidget *promptOverlayHost = window.findChild<QWidget *>(
            QStringLiteral("promptOverlayHost"));
        check(promptWrapper && footerDock && promptOverlayHost &&
                  promptWrapper->parentWidget() == promptOverlayHost &&
                  footerDock->isVisibleTo(&window) &&
                  promptWrapper->isVisibleTo(&window),
              QStringLiteral("Code and Git share the same visible global prompt "
                             "overlay"));

        // adhoc #420: following a branch link must land on the branch straight
        // away. The panel's git reads run on a worker thread now, so the
        // selection has to come from the rows already on screen — reading it
        // back without pumping the event loop proves nothing was waited on.
        // The Git range must represent the complete worktree snapshot, including
        // edits the agent has not committed yet — a ref-only main..branch diff
        // silently omitted these and made the consolidated view look empty while
        // an agent was still working.
        const QString livePath = wtPath + QStringLiteral("/live-uncommitted.txt");
        // Move main ahead immediately before opening the branch. Navigation is
        // read-only: merely reviewing this linked agent branch must not create a
        // merge commit or rewrite its worktree.
        {
            QFile mainShared(wtRepo.path() +
                             QStringLiteral("/auto-stash-overlap.txt"));
            mainShared.open(QIODevice::WriteOnly | QIODevice::Truncate);
            for (int i = 1; i <= 12; ++i)
                mainShared.write((i == 12 ? QByteArray("main changed line 12\n")
                                          : QStringLiteral("line %1\n")
                                                .arg(i)
                                                .toUtf8()));
            mainShared.close();
            runGitChecked(wtRepo.path(), {"add", "auto-stash-overlap.txt"});
            runGitChecked(wtRepo.path(),
                          {"commit", "-m", "main advanced before branch review"});

            QFile agentShared(wtPath +
                              QStringLiteral("/auto-stash-overlap.txt"));
            agentShared.open(QIODevice::WriteOnly | QIODevice::Truncate);
            for (int i = 1; i <= 12; ++i)
                agentShared.write((i == 1 ? QByteArray("agent changed line 1\n")
                                          : QStringLiteral("line %1\n")
                                                .arg(i)
                                                .toUtf8()));
            agentShared.close();
        }
        QFile liveFile(livePath);
        if (liveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            liveFile.write("visible before commit\n");
            liveFile.close();
        }
        const QString landed = window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        check(landed == QStringLiteral("feature/keep-selected"),
              QString("clicking a branch link selects the branch without waiting "
                      "for the panel's git reads (adhoc #420, landed on %1)")
                  .arg(landed.isEmpty() ? QStringLiteral("<none>") : landed));
        // adhoc #16: there is no separate branch view any more. A branch link
        // opens one combined view in the Git tab — the graph browses the branch
        // (its button follows the ref) and the right pane opens that branch's
        // diff against the compare base at the same time.
        check(window.testCommitWorkspacePage() == 2 &&
                  window.testBrowsedBranch() ==
                      QStringLiteral("feature/keep-selected") &&
                  window.testBranchDiffBranch() ==
                      QStringLiteral("feature/keep-selected"),
              QString("a branch link opens the branch's graph and its diff "
                      "against main in the one Git view (adhoc #16, page = %1, "
                      "branch = %2, diff = %3)")
                  .arg(window.testCommitWorkspacePage())
                  .arg(window.testBrowsedBranch())
                  .arg(window.testBranchDiffBranch()));
        // The branch symbol -> main indicator names what it's compared against.
        check(window.testCompareIndicatorText() == QStringLiteral("main"),
              QString("the compare indicator points the branch at main (adhoc "
                      "#16, base = %1)")
                  .arg(window.testCompareIndicatorText().isEmpty()
                           ? QStringLiteral("<hidden>")
                           : window.testCompareIndicatorText()));
        QApplication::processEvents();
        const QString reviewHead =
            gitOutput(wtPath, {"rev-parse", "HEAD"}).trimmed();
        const QString reviewCounts = gitOutput(
            wtRepo.path(), {"rev-list", "--left-right", "--count",
                            "main...feature/keep-selected"});
        check(reviewCounts.startsWith(QLatin1Char('1')) &&
                  reviewHead == gitOutput(
                                    wtRepo.path(),
                                    {"rev-parse", "feature/keep-selected"})
                                    .trimmed(),
              QString("opening a branch does not mutate its history "
                      "(main...branch = %1)")
                  .arg(reviewCounts));

        // Bulk synchronization must also leave a divergent active agent branch
        // untouched, and it must not manufacture a merge commit on a divergent
        // inactive ref. A strictly-behind idle ref can still fast-forward.
        runGitChecked(wtRepo.path(),
                      {"branch", "regression/divergent-idle",
                       "feature/keep-selected"});
        runGitChecked(wtRepo.path(),
                      {"branch", "regression/behind-idle", "main^"});
        const QString divergentIdleHead =
            gitOutput(wtRepo.path(),
                      {"rev-parse", "regression/divergent-idle"})
                .trimmed();
        window.testPullBaseIntoAllBranches();
        check(gitOutput(wtPath, {"rev-parse", "HEAD"}).trimmed() == reviewHead &&
                  gitOutput(wtRepo.path(),
                            {"rev-parse", "regression/divergent-idle"})
                          .trimmed() == divergentIdleHead &&
                  gitOutput(wtRepo.path(),
                            {"rev-parse", "regression/behind-idle"})
                          .trimmed() ==
                      gitOutput(wtRepo.path(), {"rev-parse", "main"}).trimmed(),
              QStringLiteral("bulk synchronization only fast-forwards idle refs"));
        runGitChecked(wtRepo.path(),
                      {"branch", "-D", "regression/divergent-idle"});
        runGitChecked(wtRepo.path(),
                      {"branch", "-D", "regression/behind-idle"});

        // The actual toolbar button performs the intentional update. This catches
        // the manual route passing m_branchDiffBranch by reference across
        // event-pumping Git calls.
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QElapsedTimer pullButtonTimer;
        pullButtonTimer.start();
        while (pullButtonTimer.elapsed() < 5000 &&
               !window.testBranchPullEnabled())
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool pullButtonClicked = window.testClickBranchPull();
        QApplication::processEvents();
        const QString manualPullCounts = gitOutput(
            wtRepo.path(),
            {"rev-list", "--left-right", "--count",
             "main...feature/keep-selected"});
        QFile restoredShared(wtPath +
                             QStringLiteral("/auto-stash-overlap.txt"));
        restoredShared.open(QIODevice::ReadOnly);
        const QByteArray restoredSharedText = restoredShared.readAll();
        const QString restoredStatus =
            gitOutput(wtPath, {"status", "--short", "--",
                               "auto-stash-overlap.txt"});
        check(pullButtonClicked && manualPullCounts.startsWith(QLatin1Char('0')) &&
                  restoredSharedText.contains("agent changed line 1\n") &&
                  restoredSharedText.contains("main changed line 12\n") &&
                  restoredStatus.contains(
                      QStringLiteral("auto-stash-overlap.txt")) &&
                  gitOutput(wtPath, {"stash", "list"}).isEmpty(),
              QString("Pull main explicitly updates the linked branch and "
                      "restores local edits (clicked=%1 counts=%2 status=%3 "
                      "stash=%4)")
                  .arg(pullButtonClicked)
                  .arg(manualPullCounts)
                  .arg(restoredStatus,
                       gitOutput(wtPath, {"stash", "list"})));

        // If main edits the exact same hunk as an agent's uncommitted change,
        // Git can finish the merge and only then fail while reapplying its
        // autostash. Pull main is transactional: it must roll the branch back
        // and restore the original edit instead of leaving a broad conflicted /
        // staged worktree that later inflates every agent badge.
        const QString beforeConflictHead =
            gitOutput(wtPath, {"rev-parse", "HEAD"}).trimmed();
        {
            QFile mainShared(wtRepo.path() +
                             QStringLiteral("/auto-stash-overlap.txt"));
            mainShared.open(QIODevice::WriteOnly | QIODevice::Truncate);
            for (int i = 1; i <= 12; ++i)
                mainShared.write((i == 1 ? QByteArray("main changed line 1\n")
                                          : i == 12
                                                ? QByteArray("main changed line 12\n")
                                                : QStringLiteral("line %1\n")
                                                      .arg(i)
                                                      .toUtf8()));
            mainShared.close();
        }
        runGitChecked(wtRepo.path(), {"add", "auto-stash-overlap.txt"});
        runGitChecked(wtRepo.path(),
                      {"commit", "-m", "main overlaps protected agent edit"});
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QElapsedTimer conflictPullTimer;
        conflictPullTimer.start();
        while (conflictPullTimer.elapsed() < 5000 &&
               !window.testBranchPullEnabled())
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        const bool conflictPullClicked = window.testClickBranchPull();
        QApplication::processEvents();
        QFile rolledBackShared(wtPath +
                               QStringLiteral("/auto-stash-overlap.txt"));
        rolledBackShared.open(QIODevice::ReadOnly);
        const QByteArray rolledBackText = rolledBackShared.readAll();
        const QString afterConflictHead =
            gitOutput(wtPath, {"rev-parse", "HEAD"}).trimmed();
        const QString conflictPullCounts = gitOutput(
            wtRepo.path(),
            {"rev-list", "--left-right", "--count",
             "main...feature/keep-selected"});
        const QString unmergedAfterRollback =
            gitOutput(wtPath, {"diff", "--name-only", "--diff-filter=U"});
        check(conflictPullClicked && afterConflictHead == beforeConflictHead &&
                  conflictPullCounts.startsWith(QLatin1Char('1')) &&
                  rolledBackText.contains("agent changed line 1\n") &&
                  !rolledBackText.contains("main changed line 1\n") &&
                  unmergedAfterRollback.trimmed().isEmpty() &&
                  gitOutput(wtPath, {"stash", "list"}).isEmpty(),
              QString("Pull main rolls back an autostash overlap without "
                      "polluting the agent worktree (clicked=%1 head=%2/%3 "
                      "counts=%4 unmerged=%5 stash=%6)")
                  .arg(conflictPullClicked)
                  .arg(afterConflictHead, beforeConflictHead, conflictPullCounts,
                       unmergedAfterRollback,
                       gitOutput(wtPath, {"stash", "list"})));
        // Now that automatic synchronization has completed, introduce the
        // staged deletion and re-open the same branch. This isolates the diff
        // regression without making the earlier pull test reject a dirty tree.
        QFile::remove(wtPath + QStringLiteral("/base-delete.txt"));
        runGitChecked(wtPath, {"add", "-A", "--", "base-delete.txt"});
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QElapsedTimer diffTimer;
        diffTimer.start();
        while (diffTimer.elapsed() < 5000 &&
               (!window.testBranchDiffFiles().contains(
                    QStringLiteral("live-uncommitted.txt")) ||
                !window.testBranchDiffFiles().contains(
                    QStringLiteral("base-delete.txt"))))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        check(window.testBranchDiffFiles().contains(
                  QStringLiteral("live-uncommitted.txt")),
              QString("a worktree comparison includes its uncommitted and "
                      "untracked files (files = %1)")
                  .arg(window.testBranchDiffFiles().join(QStringLiteral(", "))));
        check(window.testBranchDiffFiles().contains(
                  QStringLiteral("base-delete.txt")),
              QString("a staged deletion renders in Git without an unable-to-stat "
                      "error (files = %1)")
                  .arg(window.testBranchDiffFiles().join(QStringLiteral(", "))));
        check(window.testSourceControlPaths() == window.testBranchDiffFiles(),
              QString("the universal CHANGES tree lists the branch range's files "
                      "(tree = %1, diff = %2)")
                  .arg(window.testSourceControlPaths().join(QStringLiteral(", ")),
                       window.testBranchDiffFiles().join(QStringLiteral(", "))));
        const QString branchCommitControls =
            window.testScmCommitControlsState();
        check(window.testSourceControlGitDir() == wtPath &&
                  branchCommitControls.contains(QStringLiteral("commit=enabled")),
              QString("Git can commit an open branch's uncommitted changes in "
                      "that branch's own worktree (target = %1, controls = %2)")
                  .arg(window.testSourceControlGitDir(), branchCommitControls));
        check(window.testClickSourceControlPath(
                  QStringLiteral("live-uncommitted.txt")) &&
                  window.testCommitWorkspacePage() == 2,
              QStringLiteral("clicking a branch file in CHANGES scrolls the "
                             "right-hand range diff to that file"));
        // Every diff keeps the same universal source-control composer and
        // changes tree above the branch's commit graph. The global prompt stays
        // visible below it, so an agent can be launched directly from Git.
        check(window.testGitFilesSlotPage() == 0 &&
                  window.testGitHistorySlotPage() == 0,
              QString("comparing a branch keeps the universal source-control "
                      "panel above the branch graph (files slot = %1, history "
                      "slot = %2)")
                  .arg(window.testGitFilesSlotPage())
                  .arg(window.testGitHistorySlotPage()));
        // adhoc #16: the base end is switchable — re-diff against another
        // branch and the indicator follows.
        window.testSetCompareBase(QStringLiteral("feature/other-base"));
        QApplication::processEvents();
        check(window.testCompareIndicatorText() ==
                  QStringLiteral("feature/other-base"),
              QString("the compare base can be changed off main (adhoc #16, "
                      "base = %1)")
                  .arg(window.testCompareIndicatorText().isEmpty()
                           ? QStringLiteral("<hidden>")
                           : window.testCompareIndicatorText()));
        window.testSetCompareBase(QStringLiteral("main"));
        QApplication::processEvents();
        check(window.testSwitchToWorktreeGitBranch(
                  QStringLiteral("feature/keep-selected")) ==
                  QStringLiteral("feature/keep-selected"),
              QStringLiteral("a named worktree opens in the Git view's combined "
                             "branch view instead of a separate diff pane"));
        // The rail's Git entry is the stable home destination: it always clears
        // a branch/worktree comparison and returns to main.
        window.testClickRailGitButton();
        check(window.testSourceControlDiffText().contains(
                  QStringLiteral("Loading changes on main")),
              QString("the rail clears the previous branch/commit diff before "
                      "refreshing main (pane = \"%1\")")
                  .arg(window.testSourceControlDiffText().left(80).simplified()));
        QApplication::processEvents();
        check(window.testBrowsedBranch() == QStringLiteral("main") &&
                  window.testCommitWorkspacePage() == 0 &&
                  window.testGitFilesSlotPage() == 0 &&
                  window.testGitHistorySlotPage() == 0,
              QString("the rail's Git entry clears the branch comparison and "
                      "returns to main (branch "
                      "= %1, page = %2)")
                  .arg(window.testBrowsedBranch())
                  .arg(window.testCommitWorkspacePage()));
        check(window.testCompareIndicatorText().isEmpty(),
              QString("the compare indicator hides after returning to main (base = %1)")
                  .arg(window.testCompareIndicatorText()));
        window.testNavigateBack();
        QApplication::processEvents();
        check(window.testBrowsedBranch() ==
                  QStringLiteral("feature/keep-selected") &&
                  window.testCommitWorkspacePage() == 2,
              QString("Back returns from main to the prior branch comparison "
                      "(branch = %1, page = %2)")
                  .arg(window.testBrowsedBranch())
                  .arg(window.testCommitWorkspacePage()));
        check(window.testNavForwardToolTip().contains(QStringLiteral("Git · main")),
              QString("Forward identifies its exact main-branch destination (%1)")
                  .arg(window.testNavForwardToolTip()));
        window.testNavigateForward();
        QApplication::processEvents();
        check(window.testBrowsedBranch() == QStringLiteral("main") &&
                  window.testCommitWorkspacePage() == 0,
              QString("Forward returns from the branch comparison to main "
                      "(branch = %1, page = %2)")
                  .arg(window.testBrowsedBranch())
                  .arg(window.testCommitWorkspacePage()));
        check(window.testNavBackToolTip().contains(
                  QStringLiteral("Git · feature/keep-selected")),
              QString("Back identifies its exact branch destination (%1)")
                  .arg(window.testNavBackToolTip()));

        // The branch picker itself participates in the same browser trail. A
        // user can browse a feature, choose main, then walk both directions
        // without losing the detailed range comparison they had open.
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        window.testSwitchToBranchImmediateSelection(QStringLiteral("main"));
        QApplication::processEvents();
        window.testNavigateBack();
        QApplication::processEvents();
        check(window.testBrowsedBranch() ==
                  QStringLiteral("feature/keep-selected") &&
                  window.testCommitWorkspacePage() == 2,
              QString("Back restores a branch comparison after main was selected "
                      "in Git (branch = %1, page = %2)")
                  .arg(window.testBrowsedBranch())
                  .arg(window.testCommitWorkspacePage()));
        window.testNavigateForward();
        QApplication::processEvents();
        check(window.testBrowsedBranch() == QStringLiteral("main") &&
                  window.testCommitWorkspacePage() == 0,
              QString("Forward restores main after replaying a branch selection "
                      "(branch = %1, page = %2)")
                  .arg(window.testBrowsedBranch())
                  .arg(window.testCommitWorkspacePage()));

        // adhoc #50: the trail reaches below the tab bar. A commit's diff and an
        // open file are places of their own, so Back returns to the list they
        // were opened from instead of jumping a whole tab away.
        const QString navCommit =
            gitOutput(wtRepo.path(), {"rev-parse", "main"}).trimmed();
        window.testShowCommit(navCommit);
        QApplication::processEvents();
        check(window.testOpenCommitHash() == navCommit,
              QStringLiteral("opening a commit shows its diff in the Git view"));
        check(window.testNavBackToolTip().contains(QStringLiteral("Git · main")),
              QString("Back out of a commit is identified as the Git view it came "
                      "from (%1)")
                  .arg(window.testNavBackToolTip()));
        window.testNavigateBack();
        QApplication::processEvents();
        check(window.testOpenCommitHash().isEmpty() &&
                  window.testCommitWorkspacePage() == 0,
              QString("Back steps out of a commit diff to the Git view "
                      "(commit = %1, page = %2)")
                  .arg(window.testOpenCommitHash())
                  .arg(window.testCommitWorkspacePage()));
        check(window.testNavForwardToolTip().contains(navCommit.left(7)),
              QString("Forward names the commit it would reopen (%1)")
                  .arg(window.testNavForwardToolTip()));
        window.testNavigateForward();
        QApplication::processEvents();
        check(window.testOpenCommitHash() == navCommit,
              QString("Forward reopens the commit's diff (commit = %1)")
                  .arg(window.testOpenCommitHash()));

        window.testOpenRepoFile(QStringLiteral("base-delete.txt"));
        QApplication::processEvents();
        check(window.testFilesStackPage() == 1 &&
                  window.testOpenRepoFilePath() ==
                      QStringLiteral("base-delete.txt"),
              QString("opening a file shows it in the Code editor (page = %1, "
                      "file = %2)")
                  .arg(window.testFilesStackPage())
                  .arg(window.testOpenRepoFilePath()));
        check(window.testNavBackToolTip().contains(navCommit.left(7)),
              QString("Back from an open file returns to the commit it was "
                      "opened from (%1)")
                  .arg(window.testNavBackToolTip()));
        window.testNavigateBack();
        QApplication::processEvents();
        check(window.testFilesStackPage() == 0 &&
                  window.testOpenCommitHash() == navCommit,
              QString("Back leaves the file editor for the previous place "
                      "(page = %1, commit = %2)")
                  .arg(window.testFilesStackPage())
                  .arg(window.testOpenCommitHash()));
        window.testNavigateForward();
        QApplication::processEvents();
        check(window.testFilesStackPage() == 1 &&
                  window.testOpenRepoFilePath() ==
                      QStringLiteral("base-delete.txt"),
              QString("Forward reopens the file that was on screen (page = %1, "
                      "file = %2)")
                  .arg(window.testFilesStackPage())
                  .arg(window.testOpenRepoFilePath()));
        QFile::remove(livePath);
        // And the refresh it kicked off still lands, leaving that branch selected.
        window.testReloadBranchesPanel();
        QApplication::processEvents();
        const QStringList after = window.testBranchRowOrder();
        check(after.contains(QStringLiteral("feature/keep-selected")),
              QStringLiteral("the background refresh rebuilds the rows after the "
                             "click (adhoc #420)"));
        check(window.testBranchHealthIcon(QStringLiteral("feature/other-base")) ==
                  QStringLiteral("download"),
              QStringLiteral("Branches marks a branch that still needs main merged "
                             "with the same download icon as the Agents list"));

        // adhoc #227: clicking a branch has to repaint for *that* branch straight
        // away. The range pane used to keep the previously viewed branch's diff
        // and changed-file list on screen for as long as the new branch's git
        // read took, so it confidently attributed one branch's changes to
        // another; and a branch already reviewed once must come back instantly
        // from its cached patch rather than through another read.
        QTemporaryDir swapHome;
        const QString swapPath = swapHome.path() + QStringLiteral("/wt-swap");
        runGitChecked(wtRepo.path(), {"branch", "feature/fast-swap", "main"});
        runGitChecked(wtRepo.path(),
                      {"worktree", "add", swapPath, "feature/fast-swap"});
        {
            QFile swapOnly(swapPath + QStringLiteral("/fast-swap-only.txt"));
            swapOnly.open(QIODevice::WriteOnly);
            swapOnly.write("only on fast-swap\n");
            swapOnly.close();
        }
        runGitChecked(swapPath, {"add", "fast-swap-only.txt"});
        runGitChecked(swapPath, {"commit", "-m", "fast-swap only file"});

        const auto waitForDiffText = [&window](const QString &needle) {
            QElapsedTimer diffTimer;
            diffTimer.start();
            while (diffTimer.elapsed() < 5000 &&
                   !window.testBranchDiffText().contains(needle))
                QApplication::processEvents(QEventLoop::AllEvents, 20);
            return window.testBranchDiffText().contains(needle);
        };

        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        check(waitForDiffText(QStringLiteral("branch-change.txt")),
              QStringLiteral("the range pane renders the selected branch's own "
                             "changed file"));

        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/fast-swap"));
        const QString swappedText = window.testBranchDiffText();
        check(!swappedText.contains(QStringLiteral("branch-change.txt")),
              QString("selecting another branch drops the previous branch's diff "
                      "at once rather than leaving it on screen while git is read "
                      "(adhoc #227, pane = \"%1\")")
                  .arg(swappedText.left(60).simplified()));
        check(!window.testSourceControlPaths().contains(
                  QStringLiteral("branch-change.txt")),
              QString("...and the CHANGES list stops listing the branch that was "
                      "left (files = %1)")
                  .arg(window.testSourceControlPaths().join(QStringLiteral(", "))));
        check(waitForDiffText(QStringLiteral("fast-swap-only.txt")),
              QStringLiteral("the newly selected branch's own diff arrives behind "
                             "that placeholder"));
        check(window.testBranchDiffCached(QStringLiteral("feature/fast-swap")),
              QStringLiteral("a rendered branch range is kept for the next visit "
                             "(adhoc #227)"));

        // Back to the first branch: its patch is still held, so the pane repaints
        // from memory on the very next turn of the event loop instead of waiting
        // out a second read of the same diff.
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        check(window.testBranchDiffPaintedFromCache() &&
                  window.testBranchDiffText().contains(
                      QStringLiteral("branch-change.txt")),
              QString("returning to an already-reviewed branch repaints its diff "
                      "without waiting on git (adhoc #227, from cache = %1)")
                  .arg(window.testBranchDiffPaintedFromCache()
                           ? QStringLiteral("yes")
                           : QStringLiteral("no")));

        // adhoc #1384: a branch whose diff can't be read used to leave one red
        // line — "Could not diff <branch>: git timed out" — with no command, no
        // output from git and no way forward. The timeout now names the command
        // it killed and carries whatever the child had printed, the pane shows
        // that verbatim in a terminal block, and transient failures are retried
        // before the user ever sees one.
        {
            QProcess stalled;
            stalled.setProgram(QStringLiteral("git"));
            stalled.setArguments({QStringLiteral("-C"), wtRepo.path(),
                                  QStringLiteral("diff"),
                                  QStringLiteral("--binary")});
            stalled.start();
            const QString timeoutErr = forkmesh::ui::gitTimeoutError(stalled, 8000);
            check(timeoutErr.contains(QStringLiteral("git timed out after 8s")) &&
                      timeoutErr.contains(QStringLiteral("diff --binary")),
                  QString("a killed git read reports the command it stalled on, "
                          "not a bare \"git timed out\" (adhoc #1384, err = %1)")
                      .arg(timeoutErr.left(120).simplified()));
            check(forkmesh::ui::isTransientGitError(timeoutErr) &&
                      forkmesh::ui::isTransientGitError(QStringLiteral(
                          "Unable to create '/r/.git/index.lock': File exists.")),
                  QStringLiteral("a timeout and a contended index lock are both "
                                 "worth another attempt (adhoc #1384)"));
            check(!forkmesh::ui::isTransientGitError(
                      QStringLiteral("fatal: bad revision 'nope'")),
                  QStringLiteral("a deterministic git error is not retried, so a "
                                 "real problem still shows straight away "
                                 "(adhoc #1384)"));
            const QString failHtml = forkmesh::ui::branchDiffErrorHtml(
                QStringLiteral("agent/thing"),
                QStringLiteral("git timed out after 8s: git -C /r diff\n"
                               "error: unable to read /r/.git/index"),
                3);
            check(failHtml.contains(QStringLiteral("Could not diff agent/thing")) &&
                      failHtml.contains(QStringLiteral("<pre")) &&
                      failHtml.contains(
                          QStringLiteral("unable to read /r/.git/index")) &&
                      failHtml.contains(QStringLiteral("Tried 3 times")) &&
                      failHtml.contains(QStringLiteral("href='retry:diff'")),
                  QString("the failure pane shows git's terminal output and a "
                          "Retry link (adhoc #1384, html = %1)")
                      .arg(failHtml.left(120).simplified()));
        }

        // Leave the fixture as the branch/merge tests below expect it.
        runGitChecked(wtRepo.path(),
                      {"worktree", "remove", "--force", swapPath});
        runGitChecked(wtRepo.path(), {"branch", "-D", "feature/fast-swap"});
        window.testReloadBranchesPanel();
        QApplication::processEvents();

        // adhoc #119: merging from the comparison ends it — the branch's work
        // is in main, so leaving its diff open only shows the user something
        // they're finished with. Re-open the branch, then let its "Merge to
        // main" button run: the Git view must go back to the working tree exactly
        // as if the pane's ✕ had been clicked.
        //
        // This fixture's linked worktree lives inside the parent checkout, so git
        // reports it as untracked content and the merge would refuse ("the checkout
        // has uncommitted changes"). Exclude it locally — the tracked tree is clean,
        // which is the state that check is really about.
        QFile excludeFile(wtRepo.path() + QStringLiteral("/.git/info/exclude"));
        if (excludeFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
            excludeFile.write("wt-keep/\n");
            excludeFile.close();
        }
        window.testSwitchToBranchImmediateSelection(
            QStringLiteral("feature/keep-selected"));
        QApplication::processEvents();
        // adhoc #16: re-opening the branch opens the one combined view — the
        // graph on the branch, its diff against main on the right pane, both
        // ends named by the branch button and the compare indicator. No second
        // click into a separate review page.
        check(window.testCommitWorkspacePage() == 2 &&
                  window.testBrowsedBranch() ==
                      QStringLiteral("feature/keep-selected") &&
                  window.testGitFilesSlotPage() == 0 &&
                  window.testGitHistorySlotPage() == 0,
              QString("re-opening the branch shows its graph and diff against "
                      "main with the universal source-control panel (page = %1, branch = %2, files "
                      "slot = %3, history slot = %4)")
                  .arg(window.testCommitWorkspacePage())
                  .arg(window.testBrowsedBranch())
                  .arg(window.testGitFilesSlotPage())
                  .arg(window.testGitHistorySlotPage()));
        check(window.testCompareIndicatorText() == QStringLiteral("main"),
              QString("the branch is compared against main by default (adhoc "
                      "#16, base = %1)")
                  .arg(window.testCompareIndicatorText().isEmpty()
                           ? QStringLiteral("<hidden>")
                           : window.testCompareIndicatorText()));
        const bool mergeClicked = window.testClickBranchReviewMerge(false);
        QApplication::processEvents();
        const QString mainTip =
            gitOutput(wtRepo.path(), {"log", "--oneline", "-1", "main"});
        check(mergeClicked && window.testCommitWorkspacePage() == 0 &&
                  window.testGitFilesSlotPage() == 0 &&
                  window.testGitHistorySlotPage() == 0,
              QString("merging from the branch comparison closes it and hands the "
                      "Git view back to the working tree (adhoc #119, clicked = "
                      "%1, page = %2, files slot = %3, history slot = %4, main "
                      "tip = %5)")
                  .arg(mergeClicked ? QStringLiteral("yes") : QStringLiteral("no"))
                  .arg(window.testCommitWorkspacePage())
                  .arg(window.testGitFilesSlotPage())
                  .arg(window.testGitHistorySlotPage())
                  .arg(mainTip.trimmed()));
        check(window.testCompareIndicatorText().isEmpty(),
              QString("closing the comparison hides the compare indicator "
                      "(adhoc #16, base = %1)")
                  .arg(window.testCompareIndicatorText()));
        check(mainTip.contains(QStringLiteral("Merge feature/keep-selected into main")),
              QString("the comparison's merge button really merged the branch "
                      "(adhoc #119, main tip = %1)").arg(mainTip.trimmed()));

        // The merge path reloads the persistent agent store, while this fixture
        // was injected in memory only. Restore it before exercising the separate
        // cross-repository Branch-button route below.
        const QString agentRouteBranch = QStringLiteral("agent/files-visible");
        const QString agentRouteWt =
            wtRepo.path() + QStringLiteral("/wt-agent-files");
        const QString agentRouteBaseRef =
            gitOutput(wtRepo.path(), {"rev-parse", "main"}).trimmed();
        runGitChecked(wtRepo.path(), {"branch", agentRouteBranch, "main"});
        runGitChecked(wtRepo.path(),
                      {"worktree", "add", agentRouteWt, agentRouteBranch});
        QFile agentRouteFile(agentRouteWt + QStringLiteral("/agent-live.txt"));
        if (agentRouteFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            agentRouteFile.write("visible from the agent branch route\n");
            agentRouteFile.close();
        }
        // Reproduce the report's mismatch: main gains 23 tracked files after
        // the agent forked. A snapshot `main..agent` comparison wrongly shows
        // them as reverse changes; the merge-base range must show agent-live only.
        QDir(wtRepo.path()).mkpath(QStringLiteral("main-only"));
        for (int i = 0; i < 23; ++i) {
            QFile unrelatedFile(
                wtRepo.path() +
                QStringLiteral("/main-only/primary-unrelated-%1.txt").arg(i));
            if (unrelatedFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                unrelatedFile.write("belongs to the primary checkout\n");
                unrelatedFile.close();
            }
        }
        runGitChecked(wtRepo.path(), {"add", "--", "main-only"});
        runGitChecked(wtRepo.path(),
                      {"commit", "-m", "main advanced before agent branch review"});
        issueSession.branchName = agentRouteBranch;
        issueSession.baseBranch = QStringLiteral("main");
        issueSession.baseRef = agentRouteBaseRef;
        window.testAddAgentSession(issueSession);
        // Model the intermittent stale list badge from the report. The live
        // branch review below must reconcile it to the exact rendered set.
        window.testSetCachedAgentDiffFiles(issueSession.id, 3);

        // adhoc #131: the agent detail page's "Branch" button opens that session's
        // branch and diff against main. The sessions list is global, so the click
        // still has to bind the Git view to the
        // session's own repository first; from another repo's detail page it
        // would otherwise render that repo's (missing) branch. Park the detail
        // view on "me/r", then take the route for the session on "me/wtrepo".
        window.testOpenRepository(repoIdx);
        QApplication::processEvents();
        window.testSwitchToAgentBranch(issueSession.id);
        QApplication::processEvents();
        const QString agentBranchDir = window.testRepoGitDir();
        check(agentBranchDir.startsWith(wtRepo.path()),
              QString("the agent's Branch button binds the Git view to that "
                      "session's repository (adhoc #131, git dir = %1)")
                  .arg(agentBranchDir));
        check(window.testCommitWorkspacePage() == 2 &&
                  window.testBrowsedBranch() == agentRouteBranch &&
                  window.testBranchDiffBranch() == agentRouteBranch,
              QString("the agent's Branch button binds the graph and range to "
                      "the same branch (page = %1, graph = %2, diff = %3)")
                  .arg(window.testCommitWorkspacePage())
                  .arg(window.testBrowsedBranch(), window.testBranchDiffBranch()));
        check(window.testGitFilesSlotPage() == 0 &&
                  window.testGitHistorySlotPage() == 0,
              QString("the agent's branch keeps the source-control panel above "
                      "the branch graph (files slot = %1, history "
                      "slot = %2)")
                  .arg(window.testGitFilesSlotPage())
                  .arg(window.testGitHistorySlotPage()));
        QElapsedTimer agentDiffTimer;
        agentDiffTimer.start();
        while (agentDiffTimer.elapsed() < 5000 &&
               (!window.testSourceControlPaths().contains(
                    QStringLiteral("agent-live.txt")) ||
                window.testCachedAgentDiffFiles(issueSession.id) !=
                    window.testSourceControlPaths().size()))
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        check(window.testSourceControlPaths().contains(
                  QStringLiteral("agent-live.txt")),
              QString("an agent's Branch button always fills CHANGES with its "
                      "complete worktree diff (files = %1)")
                  .arg(window.testSourceControlPaths().join(QStringLiteral(", "))));
        check(window.testCompareIndicatorText() == QStringLiteral("main"),
              QString("an agent's Branch button always compares against main "
                      "(base = %1)")
                  .arg(window.testCompareIndicatorText()));
        check(window.testComparedBranchText() == agentRouteBranch,
              QString("an agent review names the agent branch on the left instead "
                      "of displaying main -> main (left = %1)")
                  .arg(window.testComparedBranchText()));
        bool containsPrimaryChange = false;
        for (const QString &path : window.testSourceControlPaths()) {
            if (path.startsWith(QStringLiteral("main-only/primary-unrelated-"))) {
                containsPrimaryChange = true;
                break;
            }
        }
        check(!containsPrimaryChange &&
                  window.testSourceControlPaths().size() == 1,
              QString("an agent branch excludes the primary checkout's 23 unrelated "
                      "changes (files = %1)")
                  .arg(window.testSourceControlPaths().join(QStringLiteral(", "))));
        check(window.testCachedAgentDiffFiles(issueSession.id) ==
                  window.testSourceControlPaths().size(),
              QString("opening an agent branch self-heals a stale 3-file badge to "
                      "the live rendered count (badge = %1, files = %2)")
                  .arg(window.testCachedAgentDiffFiles(issueSession.id))
                  .arg(window.testSourceControlPaths().size()));
        QElapsedTimer agentPullTimer;
        agentPullTimer.start();
        QString agentPullCounts;
        while (agentPullTimer.elapsed() < 5000) {
            QApplication::processEvents(QEventLoop::AllEvents, 20);
            agentPullCounts = gitOutput(
                wtRepo.path(),
                {"rev-list", "--left-right", "--count",
                 "main..." + agentRouteBranch});
            if (agentPullCounts.startsWith(QLatin1Char('0')))
                break;
        }
        check(agentPullCounts.startsWith(QLatin1Char('0')),
              QString("opening an agent branch automatically runs its highlighted "
                      "Pull main action (main...branch = %1)")
                  .arg(agentPullCounts));
        check(window.testClickSourceControlPath(QStringLiteral("agent-live.txt")),
              QStringLiteral("an agent branch's CHANGES row scrolls the right-hand "
                             "diff to that file"));
    }

    // adhoc #183/follow-up: the repo's default (merge-base) branch must stay
    // anchored to main and NOT follow the working tree's HEAD. Parking the
    // checkout on a feature branch — what the commits-area branch switcher or a
    // transient branches-page merge does — must never silently change the default
    // branch out from under the merge editor.
    {
        QTemporaryDir defaultBranchRepo;
        if (initGitRepo(defaultBranchRepo)) {
            // Leave HEAD on a feature branch, exactly as if the user had switched
            // to it in the commits area.
            runGitChecked(defaultBranchRepo.path(),
                          {"checkout", "-b", "feature/parked"});
            runGitChecked(defaultBranchRepo.path(),
                          {"commit", "--allow-empty", "-m", "work on feature"});
            const int idx = window.testAddLocalRepository("me", "dbrepo",
                                                          defaultBranchRepo.path());
            window.testOpenRepository(idx);
            QApplication::processEvents();
            check(window.testRepoDefaultBranch() == QStringLiteral("main"),
                  QString("default branch stays main while HEAD is parked on a "
                          "feature branch (got %1)")
                      .arg(window.testRepoDefaultBranch()));
        }
    }

    // adhoc #116: a fresh install's first clone lands well after the repo-detail
    // view opened, so every ref-backed label starts out empty. Reading them back
    // used to be gated on the Code/Branches tab being on screen, so with any
    // other tab up the status strip's bottom-left branch button and the branch
    // count stayed on their empty-repo values for good — and the lazily-loaded
    // tab badges kept the zero they were built with whatever tab was showing.
    // Open a repo with no refs at all, move off Code, then let the clone land:
    // the next push-driven refresh must catch all of it up.
    {
        QTemporaryDir lateRepo;
        QWidget *statusBar =
            window.findChild<QWidget *>(QStringLiteral("appStatusBar"));
        QPushButton *branchButton =
            statusBar ? statusBar->findChild<QPushButton *>(
                            QStringLiteral("ghostButton"))
                      : nullptr;
        if (lateRepo.isValid() && branchButton) {
            // An empty directory: the record exists and its path exists, but git
            // has nothing to report — exactly the window between "the repo is in
            // the list" and "its first clone finished".
            const int idx = window.testAddLocalRepository("me", "laterepo",
                                                          lateRepo.path());
            window.testOpenRepository(idx);
            QApplication::processEvents();
            // Off the Code tab: it re-reads refs on every refresh anyway, so
            // leaving it up would hide the regression this pins.
            window.testShowRepoIssuesTab();
            QApplication::processEvents();
            const QString beforeBranch = branchButton->text();

            // The clone lands: main plus two more branches, and one workflow.
            const bool cloned =
                initGitRepo(lateRepo) &&
                runGitChecked(lateRepo.path(), {"branch", "release/1"}) &&
                runGitChecked(lateRepo.path(), {"branch", "feature/two"});
            QDir(lateRepo.path()).mkpath(QStringLiteral(".forkmesh"));
            QFile workflow(
                QDir(lateRepo.path()).filePath(QStringLiteral(".forkmesh/ci.yml")));
            const bool wroteWorkflow =
                workflow.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                workflow.write("name: ci\non: [push]\nsteps:\n  - run: true\n") > 0;
            workflow.close();

            if (cloned && wroteWorkflow) {
                window.testRefreshOpenRepoDetail();
                // The Actions badge and branch snapshot load on separate worker
                // threads; wait for both rather than letting the faster one end
                // the settle loop while the branch count still reads zero.
                QElapsedTimer settle;
                settle.start();
                while (settle.elapsed() < 10000 &&
                       (window.testRepoActionsTabText() !=
                            QStringLiteral("Actions (1)") ||
                        window.testRepoBranchesButtonText() !=
                            QStringLiteral("3 branches"))) {
                    QApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                check(beforeBranch != QStringLiteral("main") &&
                          branchButton->text() == QStringLiteral("main"),
                      QString("status strip's branch button picks up main once "
                              "the first clone lands (was %1, now %2)")
                          .arg(beforeBranch, branchButton->text()));
                check(window.testRepoBranchesButtonText() ==
                          QStringLiteral("3 branches"),
                      QString("branch count catches up with the landed clone "
                              "(got %1)")
                          .arg(window.testRepoBranchesButtonText()));
                // The Actions tab was never opened: its badge must still be right.
                check(window.testRepoActionsTabText() ==
                          QStringLiteral("Actions (1)"),
                      QString("workflow count loads without opening the Actions "
                              "tab (got %1)")
                          .arg(window.testRepoActionsTabText()));
            }
        }
    }

    // Issue #232: repository About metadata belongs under the ForkMesh metadata
    // directory, not as a root-level info.json that collides with project files.
    {
        QTemporaryDir aboutRepo;
        if (initGitRepo(aboutRepo)) {
            const int idx =
                window.testAddLocalRepository("me", "aboutrepo", aboutRepo.path());
            window.testOpenRepository(idx);
            QApplication::processEvents();

            const bool saved = window.testSaveRepoAboutMetadata(
                QStringLiteral("About from test"),
                QStringLiteral("https://forkmesh.com"));
            const QString metadataPath =
                QDir(aboutRepo.path()).filePath(QStringLiteral(".forkmesh/info.json"));
            QFile metadata(metadataPath);
            const bool hasMetadata = metadata.open(QIODevice::ReadOnly);
            const QJsonObject obj =
                hasMetadata ? QJsonDocument::fromJson(metadata.readAll()).object()
                            : QJsonObject();

            check(saved && hasMetadata &&
                      obj.value(QStringLiteral("about")).toString() ==
                          QStringLiteral("About from test") &&
                      obj.value(QStringLiteral("website")).toString() ==
                          QStringLiteral("https://forkmesh.com") &&
                      !QFileInfo::exists(
                          QDir(aboutRepo.path()).filePath(QStringLiteral("info.json"))),
                  QStringLiteral("repo about metadata saves to .forkmesh/info.json "
                                 "and leaves root info.json absent (#232)"));
        }
    }

    // issue #251 / adhoc #99: the Settings "Default agent" choice should seed the
    // agent pickers. A window built while the default is Claude Code must start
    // both the quick-add and issue-detail pickers there (not the OpenAI fallback),
    // and changing the default afterwards must update the live pickers. Codex is a
    // first-class provider in the same picker, and the quick-add popup should show
    // every provider at once instead of opening as a tiny scrolled list.
    {
        QSettings().remove(QStringLiteral("agents/quickAddProvider"));
        QSettings().setValue(QStringLiteral("agents/defaultProvider"),
                             QStringLiteral("claude-code"));
        MainWindow seeded;
        // Enter the app shell through the same start action a user takes. A
        // single event pump does not guarantee the window's deferred-startup
        // timer has switched away from the setup page yet.
        seeded.testEnableSessionStartBypass(true);
        seeded.testSetSetupInputs(window.testUserName(),
                                  window.testSavedSolanaAddress());
        seeded.testStartSession();
        seeded.show();
        QApplication::processEvents();
        const int seededRepo = seeded.testAddLocalRepository(
            QStringLiteral("me"), QStringLiteral("provider-picker"),
            repoDir.path());
        check(seeded.testOpenRepository(seededRepo),
              QStringLiteral("provider-picker test navigates to repository detail"));
        QApplication::processEvents();
        check(seeded.testQuickAddAgentProvider() == QStringLiteral("claude-code") &&
                  seeded.testIssueAgentProvider() == QStringLiteral("claude-code"),
              QString("default agent seeds the pickers (quick-add %1, issue %2)")
                  .arg(seeded.testQuickAddAgentProvider(),
                       seeded.testIssueAgentProvider()));

        QComboBox *quickProvider =
            seeded.findChild<QComboBox *>(QStringLiteral("quickAddAgentSelector"));
        QComboBox *quickAgentModel = seeded.findChild<QComboBox *>(
            QStringLiteral("quickAddAgentModelSelector"));
        QStringList agentModelLabels;
        bool allAgentModelsHaveIcons = quickAgentModel;
        bool agentModelIconsAreClean = quickAgentModel;
        if (quickAgentModel) {
            for (int i = 0; i < quickAgentModel->count(); ++i) {
                agentModelLabels << quickAgentModel->itemText(i);
                allAgentModelsHaveIcons &= !quickAgentModel->itemIcon(i).isNull();
                agentModelIconsAreClean &=
                    !iconContainsChromaKey(quickAgentModel->itemIcon(i));
            }
        }
        const int gpt55Row = quickAgentModel
                                 ? quickAgentModel->findText(
                                       QStringLiteral("GPT-5.5"),
                                       Qt::MatchStartsWith)
                                 : -1;
        // Rows are the bare model name — the merged-success count is painted
        // beside it in the open popup only — and the tooltip still says which
        // agent runs the model.
        check(quickAgentModel && quickAgentModel->isVisible() &&
                  quickAgentModel->maxVisibleItems() >= quickAgentModel->count() &&
                  agentModelLabels.contains(QStringLiteral("Auto")) &&
                  agentModelLabels.contains(QStringLiteral("GPT-5.5")) &&
                  agentModelLabels.contains(QStringLiteral("OpenAI API")) &&
                  agentModelLabels.contains(QStringLiteral("Claude API")) &&
                  std::none_of(agentModelLabels.cbegin(), agentModelLabels.cend(),
                               [](const QString &label) {
                                   return label.contains(
                                              QStringLiteral("Claude Code")) ||
                                          label.endsWith(QStringLiteral("Codex"));
                               }) &&
                  gpt55Row >= 0 &&
                  // The tooltip opens with the account the run would use, so the
                  // model/agent line and its merged tally sit below that.
                  quickAgentModel->itemData(gpt55Row, Qt::ToolTipRole).toString()
                      .contains(QStringLiteral("GPT-5.5 · Codex\nMerged success:")) &&
                  allAgentModelsHaveIcons && agentModelIconsAreClean && quickProvider &&
                  !quickProvider->isVisible() && !seeded.testQuickAddModelVisible(),
              QString("one icon-rich composer dropdown combines agents and models "
                      "(%1 | GPT-5.5 tooltip: %2)")
                  .arg(agentModelLabels.join(QStringLiteral(", ")),
                       gpt55Row >= 0
                           ? quickAgentModel->itemData(gpt55Row, Qt::ToolTipRole)
                                 .toString()
                                 .replace(QLatin1Char('\n'), QLatin1Char('|'))
                           : QStringLiteral("missing")));
        // Only the top model lines wear the World's robot portraits. Everything
        // below them — the raw API agents, the Cloudflare chat models — keeps the
        // abstract mark it always had, so the menu does not read as one wall of
        // faces (adhoc #1545).
        const auto rowWearsPortrait = [&](int row) {
            if (!quickAgentModel || row < 0)
                return false;
            const QImage worn =
                quickAgentModel->itemIcon(row).pixmap(QSize(32, 32)).toImage();
            for (int slot = 0; slot < 7; ++slot)
                if (worn ==
                    forkmesh::ui::agentControlIcon(slot).pixmap(QSize(32, 32))
                        .toImage())
                    return true;
            return false;
        };
        int cloudflareRow = -1;
        for (int i = 0; quickAgentModel && i < quickAgentModel->count(); ++i) {
            if (quickAgentModel->itemData(i).toString() ==
                forkmesh::ui::kCloudflareAiProvider) {
                cloudflareRow = i;
                break;
            }
        }
        const int fable5Row =
            quickAgentModel ? quickAgentModel->findText(QStringLiteral("Fable 5"),
                                                       Qt::MatchStartsWith)
                            : -1;
        const int haiku45Row =
            quickAgentModel ? quickAgentModel->findText(QStringLiteral("Haiku 4.5"),
                                                       Qt::MatchStartsWith)
                            : -1;
        check(fable5Row >= 0 && haiku45Row >= 0 && cloudflareRow >= 0 &&
                  rowWearsPortrait(fable5Row) && rowWearsPortrait(haiku45Row) &&
                  !rowWearsPortrait(
                      quickAgentModel->findText(QStringLiteral("Claude API"))) &&
                  !rowWearsPortrait(
                      quickAgentModel->findText(QStringLiteral("OpenAI API"))) &&
                  !rowWearsPortrait(cloudflareRow) &&
                  forkmesh::ui::agentModelPortraitIconIndex(
                      QStringLiteral("claude-opus-5")) == 0 &&
                  forkmesh::ui::agentModelPortraitIconIndex(
                      QStringLiteral("gpt-5.6-sol")) == 4 &&
                  forkmesh::ui::agentModelPortraitIconIndex(
                      QStringLiteral("gpt-5.5")) < 0,
              QString("only the top models wear portraits (Fable 5 row %1, "
                      "Claude API row %2, Cloudflare row %3)")
                  .arg(fable5Row)
                  .arg(quickAgentModel
                           ? quickAgentModel->findText(QStringLiteral("Claude API"))
                           : -1)
                  .arg(cloudflareRow));
        // The menu is ordered by merged-agent success, then model power. With
        // no merged history the static fallback line-up is strongest first.
        QStringList rankedLabels;
        for (int i = 0; i < quickAgentModel->count(); ++i) {
            // Manual and the two API agents carry no model of their own.
            if (quickAgentModel->itemData(i, Qt::UserRole + 1).toString().isEmpty())
                continue;
            // Cloudflare chat models form their own group below the ranked
            // coding agents and are verified separately.
            const QString provider = quickAgentModel->itemData(i).toString();
            if (provider != QStringLiteral("claude-code") &&
                provider != QStringLiteral("codex"))
                continue;
            rankedLabels << quickAgentModel->itemText(i);
        }
        check(rankedLabels == QStringList({QStringLiteral("Auto"),
                                           QStringLiteral("Fable 5"),
                                           QStringLiteral("Opus 5"),
                                           QStringLiteral("Sonnet 5"),
                                           QStringLiteral("Opus 4.8"),
                                           QStringLiteral("Opus 4.7"),
                                           QStringLiteral("Opus 4.6"),
                                           QStringLiteral("Opus 4.5"),
                                           QStringLiteral("Sonnet 4.6"),
                                           QStringLiteral("Sonnet 4.5"),
                                           QStringLiteral("Haiku 4.5"),
                                           QStringLiteral("GPT-5.5"),
                                           QStringLiteral("GPT-5.4"),
                                           QStringLiteral("GPT-5.4-Mini")}),
              QString("composer model rows are bare model names, sorted strongest first when tied (%1)")
                  .arg(rankedLabels.join(QStringLiteral(", "))));
        // The merged tally rides the popup-only description role, so the badge on
        // the prompt stays the model name alone (adhoc #1565).
        check(seeded.testQuickAddAgentModelMergedNote(
                  QStringLiteral("claude-opus-4-8")) ==
                      QStringLiteral("0 merged") &&
                  !seeded.testQuickAddAgentModelLabel(
                       QStringLiteral("claude-opus-4-8"))
                       .contains(QStringLiteral("merged")),
              QStringLiteral("merged counts live in the popup rows, not the "
                             "closed model badge"));
        QComboBox *canonicalModel =
            seeded.findChild<QComboBox *>(QStringLiteral("quickAddModelSelector"));
        int concreteClaudeChoice = -1;
        QString concreteClaudeModel;
        if (quickAgentModel) {
            for (int i = 0; i < quickAgentModel->count(); ++i) {
                const QString candidate =
                    quickAgentModel->itemData(i, Qt::UserRole + 1).toString();
                if (quickAgentModel->itemData(i).toString() ==
                        QStringLiteral("claude-code") &&
                    candidate != QStringLiteral("auto")) {
                    concreteClaudeChoice = i;
                    concreteClaudeModel = candidate;
                    break;
                }
            }
        }
        if (concreteClaudeChoice >= 0)
            quickAgentModel->setCurrentIndex(concreteClaudeChoice);
        QApplication::processEvents();
        check(concreteClaudeChoice >= 0 &&
                  seeded.testQuickAddAgentProvider() ==
                      QStringLiteral("claude-code") &&
                  canonicalModel &&
                  canonicalModel->currentData().toString() == concreteClaudeModel,
              QStringLiteral("one combined-menu click updates provider and model state"));

        // The composer puts models with the most merged work first, keeping a
        // merged-success count beside every model name.
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const auto addUse = [&seeded](int id, const QString &model,
                                          qint64 createdAtMs, bool merged) {
                AgentSession session;
                session.id = id;
                session.owner = QStringLiteral("me");
                session.name = QStringLiteral("provider-picker");
                session.prompt = QStringLiteral("Model frequency fixture");
                session.provider = QStringLiteral("claude-code");
                session.model = model;
                session.status = AgentStatus::Success;
                session.merged = merged;
                session.createdAtMs = createdAtMs;
                session.startedAtMs = createdAtMs;
                session.finishedAtMs = createdAtMs + 1000;
                seeded.testAddAgentSession(session);
            };
            for (int i = 0; i < 4; ++i)
                addUse(144501 + i, QStringLiteral("claude-sonnet-4-6"),
                       nowMs - (i + 1) * 60000, i < 3);
            for (int i = 0; i < 2; ++i)
                addUse(144505 + i, QStringLiteral("claude-opus-4-8"),
                       nowMs - (i + 5) * 60000, i < 1);
            seeded.testRefreshQuickAddAgentModelSelector();
            QApplication::processEvents();
            const QString opusLabel = seeded.testQuickAddAgentModelLabel(
                QStringLiteral("claude-opus-4-8"));
            const QString sonnetLabel = seeded.testQuickAddAgentModelLabel(
                QStringLiteral("claude-sonnet-4-6"));
            const int sonnetRow = quickAgentModel->findText(sonnetLabel);
            const int opusRow = quickAgentModel->findText(opusLabel);
            check(sonnetRow >= 0 && opusRow > sonnetRow &&
                      sonnetLabel == QStringLiteral("Sonnet 4.6") &&
                      opusLabel == QStringLiteral("Opus 4.8") &&
                      seeded.testQuickAddAgentModelMergedNote(
                          QStringLiteral("claude-sonnet-4-6")) ==
                          QStringLiteral("3 merged") &&
                      seeded.testQuickAddAgentModelMergedNote(
                          QStringLiteral("claude-opus-4-8")) ==
                          QStringLiteral("1 merged"),
                  QString("the model menu orders by merged success and shows "
                          "the count (Sonnet row %1, Opus row %2)")
                      .arg(sonnetRow).arg(opusRow));
            // The record has to be on the menu from the first frame (adhoc
            // #1565). The composer is built before initAgents() reads the
            // sessions off disk, so a restart used to open with every model on
            // "0 merged" until some later reload happened to refresh the menu —
            // the user's whole merged history, apparently wiped.
            {
                // Same window proves the composer comes back to its corner: a
                // prompt left floating mid-workspace in the last session must not
                // be where the next launch opens (adhoc #1565).
                QSettings placement;
                placement.setValue(QStringLiteral("prompt/floating"), true);
                placement.setValue(QStringLiteral("prompt/floatPos"),
                                   QPoint(24, 48));
                placement.setValue(QStringLiteral("prompt/floatSize"),
                                   QSize(320, 200));
                MainWindow restarted;
                const QString restartedSonnet =
                    restarted.testQuickAddAgentModelMergedNote(
                        QStringLiteral("claude-sonnet-4-6"));
                const QString restartedOpus =
                    restarted.testQuickAddAgentModelMergedNote(
                        QStringLiteral("claude-opus-4-8"));
                check(restartedSonnet == QStringLiteral("3 merged") &&
                          restartedOpus == QStringLiteral("1 merged"),
                      QString("a fresh window shows the stored merged counts "
                              "without waiting for a reload (Sonnet %1, Opus %2)")
                          .arg(restartedSonnet, restartedOpus));
                restarted.resize(1200, 720);
                restarted.show();
                QApplication::processEvents();
                auto *restartedDock =
                    restarted.findChild<QWidget *>(QStringLiteral("logDock"));
                auto *restartedPrompt = restarted.findChild<QWidget *>(
                    QStringLiteral("promptOverlayHost"));
                check(restarted.testPromptOverlayPlacement() ==
                              QStringLiteral("anchored") &&
                          restartedDock && restartedPrompt &&
                          restartedPrompt->parentWidget() == restartedDock &&
                          restartedPrompt->geometry().center().x() >
                              restartedDock->rect().center().x() &&
                          restartedPrompt->geometry().bottom() ==
                              restartedDock->rect().bottom() &&
                          !placement.value(QStringLiteral("prompt/floating"))
                               .toBool(),
                      QString("a launch pins the prompt to the lower-right corner "
                              "however it was left (placement %1)")
                          .arg(restarted.testPromptOverlayPlacement()));
                stopChildProcesses(restarted);
            }
            for (int id = 144501; id <= 144506; ++id)
                seeded.testRemoveAgentSession(id);
            seeded.testRefreshQuickAddAgentModelSelector();
            QApplication::processEvents();
        }
        // Every active Workers AI fallback must appear as its own selectable
        // composer row. Selecting each row updates the same hidden provider and
        // model controls quickAddIssue() reads when it sends /api/ai/ask.
        bool allCloudflareModelsSelectable = quickAgentModel && canonicalModel;
        QStringList selectedCloudflareModels;
        for (const auto &choice :
             forkmesh::ui::cloudflareAiFallbackModels()) {
            int row = -1;
            for (int i = 0; quickAgentModel && i < quickAgentModel->count(); ++i) {
                if (quickAgentModel->itemData(i).toString() ==
                        QStringLiteral("cloudflare-ai") &&
                    quickAgentModel->itemData(i, Qt::UserRole + 1).toString() ==
                        choice.first) {
                    row = i;
                    break;
                }
            }
            allCloudflareModelsSelectable &= row >= 0;
            if (row < 0)
                continue;
            quickAgentModel->setCurrentIndex(row);
            QApplication::processEvents();
            selectedCloudflareModels << canonicalModel->currentData().toString();
            allCloudflareModelsSelectable &=
                seeded.testQuickAddAgentProvider() ==
                    QStringLiteral("cloudflare-ai") &&
                canonicalModel->currentData().toString() == choice.first;
        }
        check(allCloudflareModelsSelectable &&
                  selectedCloudflareModels.size() ==
                      forkmesh::ui::cloudflareAiFallbackModels().size(),
              QString("every active Cloudflare model is selectable in the "
                      "prompt area (%1)")
                  .arg(selectedCloudflareModels.join(QStringLiteral(", "))));
        if (concreteClaudeChoice >= 0)
            quickAgentModel->setCurrentIndex(concreteClaudeChoice);
        QApplication::processEvents();

        // adhoc #38: the composer's speed (reasoning-effort) picker sits next to
        // the mode selector, offers the CLI's ladder with "Ultra" for xhigh, and
        // writes the same setting the "/" popup's effort dots do — so a pick here
        // is what the next run is actually launched with.
        QComboBox *quickSpeed =
            seeded.findChild<QComboBox *>(QStringLiteral("quickAddSpeedSelector"));
        QStringList speedLabels;
        if (quickSpeed) {
            for (int i = 0; i < quickSpeed->count(); ++i)
                speedLabels << quickSpeed->itemText(i);
        }
        check(quickSpeed && quickSpeed->isVisible() &&
                  quickSpeed->width() <= 32 &&
                  quickSpeed->accessibleName().startsWith(
                      QStringLiteral("Reasoning effort:")) &&
                  speedLabels == QStringList({QStringLiteral("Low"),
                                              QStringLiteral("Medium"),
                                              QStringLiteral("High"),
                                              QStringLiteral("Ultra"),
                                              QStringLiteral("Max")}) &&
                  std::all_of(
                      speedLabels.cbegin(), speedLabels.cend(),
                      [quickSpeed](const QString &label) {
                          const QIcon icon =
                              quickSpeed->itemIcon(quickSpeed->findText(label));
                          return !icon.isNull() && !iconContainsChromaKey(icon);
                      }),
              QString("composer speed picker offers the effort ladder (%1)")
                  .arg(speedLabels.join(QStringLiteral(", "))));
        QComboBox *quickMode =
            seeded.findChild<QComboBox *>(QStringLiteral("quickAddModeSelector"));
        // adhoc #1204: each row also carries the permission it grants, spelled out
        // beside the label once the popup is open (Qt::UserRole + 7), so the open
        // menu is not four bare words.
        bool modesExplainPermissions = quickMode;
        if (quickMode) {
            for (int i = 0; i < quickMode->count(); ++i) {
                modesExplainPermissions &=
                    !quickMode->itemData(i, Qt::UserRole + 7).toString().isEmpty();
            }
        }
        check(quickMode && quickMode->width() <= 32 &&
                  quickMode->accessibleName().startsWith(
                      QStringLiteral("Permission mode:")) &&
                  quickMode->itemText(0) == QStringLiteral("Auto") &&
                  quickMode->itemText(1) == QStringLiteral("Ask") &&
                  quickMode->itemText(2) == QStringLiteral("Plan") &&
                  quickMode->itemText(3) == QStringLiteral("Edit") &&
                  modesExplainPermissions &&
                  !quickMode->itemIcon(0).isNull() &&
                  !quickMode->itemIcon(3).isNull() &&
                  !iconContainsChromaKey(quickMode->itemIcon(0)) &&
                  !iconContainsChromaKey(quickMode->itemIcon(3)),
              QStringLiteral("mode picker is icon-only until its labeled menu opens"));
        if (quickSpeed) {
            const int ultra = quickSpeed->findData(QStringLiteral("xhigh"));
            quickSpeed->setCurrentIndex(ultra);
            QApplication::processEvents();
            check(ultra >= 0 &&
                      QSettings()
                              .value(QStringLiteral("agents/claudeEffort"))
                              .toString() == QStringLiteral("xhigh"),
                  QStringLiteral("picking a speed persists the effort the next run "
                                 "is launched with"));
        }
        // The task-list button is the top of the send column, above "add" and
        // "new". It files the prompt in General instead of starting a genie
        // agent (adhoc #151). The strip of session dots that used to sit above
        // the prompt is gone (adhoc #38) — its state lives in the top bar now.
        auto *genieButton =
            seeded.findChild<QPushButton *>(QStringLiteral("quickAddGenieButton"));
        check(genieButton && genieButton->isVisible() &&
                  genieButton->text() == QStringLiteral("task") &&
                  genieButton->toolTip().contains(QStringLiteral("general task list")) &&
                  !genieButton->toolTip().contains(QStringLiteral("agent"),
                                                   Qt::CaseInsensitive),
              QStringLiteral("the composer task button files a General task"));
        auto *repoSizeChart = seeded.findChild<QWidget *>(QStringLiteral("repoSizeChart"));
        auto *repoLinesChart = seeded.findChild<QWidget *>(QStringLiteral("repoLinesChart"));
        auto *repoFilesChart = seeded.findChild<QWidget *>(QStringLiteral("repoFilesChart"));
        auto *overviewList = seeded.findChild<QTreeWidget *>(QStringLiteral("overviewList"));
        auto *ratchet = seeded.findChild<QToolButton *>(QStringLiteral("repoRatchetButton"));
        check(repoSizeChart && repoLinesChart && repoFilesChart && ratchet &&
                  repoSizeChart->width() == 34 && repoLinesChart->width() == 34 &&
                  repoFilesChart->width() == 34 && ratchet->isCheckable() &&
                  ratchet->text() == QStringLiteral("Ratchet"),
              QStringLiteral("repository trends use three history charts"));
        check(overviewList && overviewList->columnCount() == 2,
              QStringLiteral("overview keeps entry metrics and updated time on the left"));
        // The YOLO / Task checkboxes and the corner "Enter" badge are gone from
        // the composer (adhoc #120): the only Enter indicator is the green
        // outline on whichever send button Enter activates.
        auto *composerDock = seeded.findChild<QWidget *>(QStringLiteral("logDock"));
        QStringList composerChecks;
        if (composerDock) {
            for (auto *box : composerDock->findChildren<QCheckBox *>())
                composerChecks << box->text();
        }
        check(composerDock && !composerChecks.contains(QStringLiteral("YOLO")) &&
                  !composerChecks.contains(QStringLiteral("Task")),
              QString("the composer has no YOLO/Task toggles (%1)")
                  .arg(composerChecks.join(QStringLiteral(", "))));
        checkFooterOverlayGeometry(seeded);
        check(seeded.findChild<QLabel *>(QStringLiteral("quickAddEnterBadge")) ==
                  nullptr,
              QStringLiteral("no corner Enter badge on the send buttons"));
        check(seeded.findChild<QWidget *>(QStringLiteral("agentStatusRow")) == nullptr &&
                  seeded.findChild<QPushButton *>(
                      QStringLiteral("agentStatusMore")) == nullptr,
              QStringLiteral("the agent dot strip and its More button are gone from "
                             "above the composer"));

        seeded.testSetQuickAddAgentProvider(QStringLiteral("codex"));
        QApplication::processEvents();
        const QStringList codexModels = seeded.testQuickAddModelLabels();
        check(!seeded.testQuickAddModelVisible() &&
                  quickAgentModel && quickAgentModel->isVisible() &&
                  quickAgentModel->currentText().startsWith(QStringLiteral("GPT-")) &&
                  quickAgentModel
                      ->itemData(quickAgentModel->currentIndex(), Qt::ToolTipRole)
                      .toString()
                      .contains(QStringLiteral("· Codex\nMerged success:")) &&
                  !seeded.testQuickAddModelEditable() && codexModels ==
                      QStringList({QStringLiteral("GPT-5.5"),
                                   QStringLiteral("GPT-5.4"),
                                   QStringLiteral("GPT-5.4-Mini")}),
              QString("Codex prompt picker shows only ChatGPT-supported Codex models (%1)")
                  .arg(codexModels.join(QStringLiteral(", "))));
        check(std::none_of(codexModels.cbegin(), codexModels.cend(),
                           [](const QString &label) {
                               return label.startsWith(QStringLiteral("Claude "));
                           }),
              QStringLiteral("Codex prompt picker does not show Claude models"));
        check(std::none_of(codexModels.cbegin(), codexModels.cend(),
                           [](const QString &label) {
                               return label.endsWith(QStringLiteral(" Codex"));
                           }),
              QStringLiteral("Codex prompt picker does not show ChatGPT-unsupported Codex API models"));
        MainWindow rememberedPromptProvider;
        rememberedPromptProvider.testEnableSessionStartBypass(true);
        rememberedPromptProvider.show();
        QApplication::processEvents();
        check(rememberedPromptProvider.testQuickAddAgentProvider() ==
                  QStringLiteral("codex"),
              QString("prompt area remembers the last selected agent provider (%1)")
                  .arg(rememberedPromptProvider.testQuickAddAgentProvider()));
        stopChildProcesses(rememberedPromptProvider);
        seeded.testSetQuickAddAgentProvider(QStringLiteral("claude-api"));
        QApplication::processEvents();
        check(!seeded.testQuickAddModelVisible() && quickAgentModel->isVisible() &&
                  quickAgentModel->currentText() == QStringLiteral("Claude API"),
              QStringLiteral("combined picker stays visible for API-only providers"));

        // The default-agent control belongs to the independently deferred
        // Settings page. Visit it before driving the combo like a user. Start
        // the composer-model visibility check below from a clean sheet: the
        // list is filled as the page is built, so the setting has to be cleared
        // before that happens, not after.
        QSettings().remove(QStringLiteral("agents/composerHiddenModels"));
        seeded.testShowSettingsSection();
        QApplication::processEvents();

        // adhoc #1557: Settings → Agents chooses which of the catalog's models
        // the composer's prompt dropdown lists. Unticking one drops it from the
        // menu and persists that choice; the model the composer is currently set
        // to stays listed even when unticked, so the picker can always show what
        // the next run would launch; re-ticking brings it back.
        {
            QListWidget *modelVisibility = seeded.findChild<QListWidget *>(
                QStringLiteral("composerModelVisibilityList"));
            const auto menuRow = [quickAgentModel](const QString &provider,
                                                   const QString &model) {
                for (int i = 0; quickAgentModel && i < quickAgentModel->count();
                     ++i) {
                    if (quickAgentModel->itemData(i).toString() == provider &&
                        quickAgentModel->itemData(i, Qt::UserRole + 1)
                                .toString() == model)
                        return i;
                }
                return -1;
            };
            // Put the composer back on a concrete Claude model so the rule that
            // the *selected* row survives being unticked has something to hold.
            const int claudeRow =
                menuRow(QStringLiteral("claude-code"), concreteClaudeModel);
            if (claudeRow >= 0)
                quickAgentModel->setCurrentIndex(claudeRow);
            QApplication::processEvents();

            const QString codexKey = forkmesh::ui::composerModelKey(
                QStringLiteral("codex"), QStringLiteral("gpt-5.4-mini"));
            const QString selectedKey = forkmesh::ui::composerModelKey(
                QStringLiteral("claude-code"), concreteClaudeModel);
            const auto rowFor = [modelVisibility](const QString &key) {
                for (int i = 0; modelVisibility && i < modelVisibility->count();
                     ++i) {
                    if (modelVisibility->item(i)->data(Qt::UserRole).toString() ==
                        key)
                        return modelVisibility->item(i);
                }
                return static_cast<QListWidgetItem *>(nullptr);
            };
            QListWidgetItem *codexRow = rowFor(codexKey);
            QListWidgetItem *selectedRow = rowFor(selectedKey);
            const bool startsTicked =
                codexRow && codexRow->checkState() == Qt::Checked &&
                selectedRow && selectedRow->checkState() == Qt::Checked &&
                menuRow(QStringLiteral("codex"),
                        QStringLiteral("gpt-5.4-mini")) >= 0;
            if (codexRow)
                codexRow->setCheckState(Qt::Unchecked);
            QApplication::processEvents();
            const bool hiddenAfterUntick =
                menuRow(QStringLiteral("codex"),
                        QStringLiteral("gpt-5.4-mini")) < 0 &&
                QSettings()
                    .value(QStringLiteral("agents/composerHiddenModels"))
                    .toStringList()
                    .contains(codexKey);
            // The composer is sitting on this Claude model; hiding it must not
            // take the row that shows what is selected out of the menu.
            if (selectedRow)
                selectedRow->setCheckState(Qt::Unchecked);
            QApplication::processEvents();
            const bool selectedStaysListed =
                menuRow(QStringLiteral("claude-code"), concreteClaudeModel) >= 0 &&
                seeded.testQuickAddAgentProvider() ==
                    QStringLiteral("claude-code");
            if (codexRow)
                codexRow->setCheckState(Qt::Checked);
            if (selectedRow)
                selectedRow->setCheckState(Qt::Checked);
            QApplication::processEvents();
            const bool restored =
                menuRow(QStringLiteral("codex"),
                        QStringLiteral("gpt-5.4-mini")) >= 0 &&
                QSettings()
                    .value(QStringLiteral("agents/composerHiddenModels"))
                    .toStringList()
                    .isEmpty();
            check(modelVisibility && codexRow && selectedRow && startsTicked &&
                      hiddenAfterUntick && selectedStaysListed && restored,
                  QString("Settings picks which models the composer dropdown "
                          "lists (ticked %1, hidden %2, selection kept %3, "
                          "restored %4)")
                      .arg(startsTicked)
                      .arg(hiddenAfterUntick)
                      .arg(selectedStaysListed)
                      .arg(restored));
            QSettings().remove(QStringLiteral("agents/composerHiddenModels"));
        }

        seeded.testSetDefaultAgentProvider(QStringLiteral("claude-api"));
        check(seeded.testQuickAddAgentProvider() == QStringLiteral("claude-api") &&
                  seeded.testIssueAgentProvider() == QStringLiteral("claude-api"),
              QString("changing the default updates the live pickers "
                      "(quick-add %1, issue %2)")
                  .arg(seeded.testQuickAddAgentProvider(),
                       seeded.testIssueAgentProvider()));
        seeded.testSetDefaultAgentProvider(QStringLiteral("codex"));
        check(seeded.testQuickAddAgentProvider() == QStringLiteral("codex") &&
                  seeded.testIssueAgentProvider() == QStringLiteral("codex"),
              QString("Codex default updates the live pickers (quick-add %1, issue %2)")
                  .arg(seeded.testQuickAddAgentProvider(),
                       seeded.testIssueAgentProvider()));
        seeded.testSetDefaultAgentProvider(QStringLiteral("claude-api"));
        QApplication::processEvents();
        stopChildProcesses(seeded);
    }

    // A previously verified email should be obvious from Settings > General >
    // Profile, even on a later launch where the desktop only has the cached
    // account marker.
    {
        QSettings settings;
        const QString nodeKey = QStringLiteral("account/nodeName");
        const QString authedKey = QStringLiteral("account/authedName");
        const QString verifiedKey =
            QStringLiteral("account/emailVerified/verified-node");
        const bool hadNode = settings.contains(nodeKey);
        const bool hadAuthed = settings.contains(authedKey);
        const bool hadVerified = settings.contains(verifiedKey);
        const QVariant oldNode = settings.value(nodeKey);
        const QVariant oldAuthed = settings.value(authedKey);
        const QVariant oldVerified = settings.value(verifiedKey);

        settings.setValue(nodeKey, QStringLiteral("verified-node"));
        settings.setValue(authedKey, QStringLiteral("verified-node"));
        settings.setValue(verifiedKey, true);

        MainWindow verified;
        verified.testEnableSessionStartBypass(true);
        verified.show();
        verified.testShowSettingsSection();
        QApplication::processEvents();
        QLabel *badge =
            verified.findChild<QLabel *>(QStringLiteral("emailVerifiedBadge"));
        check(badge && !badge->isHidden() &&
                  badge->text() == QStringLiteral("Email is verified"),
              QStringLiteral("settings profile shows the cached email-verified badge"));
        stopChildProcesses(verified);

        if (hadNode)
            settings.setValue(nodeKey, oldNode);
        else
            settings.remove(nodeKey);
        if (hadAuthed)
            settings.setValue(authedKey, oldAuthed);
        else
            settings.remove(authedKey);
        if (hadVerified)
            settings.setValue(verifiedKey, oldVerified);
        else
            settings.remove(verifiedKey);
    }

    // Adhoc #113: a first run hands the MACHINE a generated node name, but the
    // person has no username yet — chat speaks as an anonymous "Guest ####"
    // (accountKind "guest", like the website's visitors) until a username is
    // typed or an account is claimed. Typing one ends guest mode.
    {
        QSettings settings;
        const QString nodeKey = QStringLiteral("account/nodeName");
        const QString generatedKey = QStringLiteral("account/generatedNodeName");
        const QString handleKey = QStringLiteral("profile/handle");
        const QString displayKey = QStringLiteral("profile/displayName");
        const QVariant oldNode = settings.value(nodeKey);
        const QVariant oldGenerated = settings.value(generatedKey);
        const QVariant oldHandle = settings.value(handleKey);
        const QVariant oldDisplay = settings.value(displayKey);
        settings.remove(nodeKey);
        settings.remove(generatedKey);
        settings.remove(handleKey);
        settings.remove(displayKey);

        MainWindow fresh;
        fresh.testEnableSessionStartBypass(true);
        const QString generatedName = settings.value(nodeKey).toString();
        check(!generatedName.isEmpty() &&
                  settings.value(generatedKey).toString() == generatedName,
              QStringLiteral("first run records the generated node name"));
        check(fresh.testChatIdentityIsGuest(),
              QStringLiteral("first run chats as a guest, not as the node"));
        static const QRegularExpression guestShape(
            QStringLiteral("^Guest \\d{4}$"));
        check(guestShape.match(fresh.testChatDisplayName()).hasMatch(),
              QStringLiteral("guest chat name is \"Guest ####\" (%1)")
                  .arg(fresh.testChatDisplayName()));
        check(fresh.testMachineNodeName() == generatedName,
              QStringLiteral("the machine keeps the generated node name"));

        // A desktop guest still advertises its machine node name, so node
        // surfaces keep its row; a browser guest (no nodeName) stays filtered
        // (adhoc #308).
        MemberInfo desktopGuest;
        desktopGuest.name = QStringLiteral("Guest 8888");
        desktopGuest.accountKind = QStringLiteral("guest");
        desktopGuest.nodeName = generatedName;
        MemberInfo browserGuest;
        browserGuest.name = QStringLiteral("Guest 1667");
        browserGuest.accountKind = QStringLiteral("guest");
        check(!forkmesh::ui::isTemporaryChatGuest(desktopGuest),
              QStringLiteral("a guest advertising a node name keeps node rows"));
        check(forkmesh::ui::isTemporaryChatGuest(browserGuest),
              QStringLiteral("a browser guest without a node stays filtered"));

        // Typing a username replaces the generated name and ends guest mode.
        fresh.testSetSetupInputs(QStringLiteral("carol"), QString());
        fresh.testStartSession();
        check(!fresh.testChatIdentityIsGuest(),
              QStringLiteral("typing a username ends guest mode"));
        check(fresh.testChatDisplayName() == QStringLiteral("carol"),
              QStringLiteral("chat then speaks as the chosen username"));
        stopChildProcesses(fresh);

        if (oldNode.isValid())
            settings.setValue(nodeKey, oldNode);
        else
            settings.remove(nodeKey);
        if (oldGenerated.isValid())
            settings.setValue(generatedKey, oldGenerated);
        else
            settings.remove(generatedKey);
        if (oldHandle.isValid())
            settings.setValue(handleKey, oldHandle);
        if (oldDisplay.isValid())
            settings.setValue(displayKey, oldDisplay);
    }

    // Issue #203: quick-adding an issue without assigning it to an agent should
    // land the user on that new issue — its detail pane opens automatically,
    // just as the assign-an-agent path jumps straight to the new session.
    {
        QTemporaryDir quickAddRepo;
        if (initGitRepo(quickAddRepo)) {
            MainWindow qaWindow;
            qaWindow.testEnableSessionStartBypass(true);
            qaWindow.testSetSetupInputs(window.testUserName(),
                                        window.testSavedSolanaAddress());
            qaWindow.testStartSession();
            qaWindow.show();
            QApplication::processEvents();
            const int idx = qaWindow.testAddLocalRepository(
                "me", "qarepo", quickAddRepo.path());
            qaWindow.testOpenRepository(idx);
            qaWindow.testShowRepoIssuesTab();
            QApplication::processEvents();

            // With no issue open yet the detail pane is collapsed; quick-adding
            // one (no agent) must reveal it on the freshly-created issue.
            const bool hiddenBefore = !qaWindow.testIssueDetailVisible();
            const int number = qaWindow.testQuickAddIssueNoAgent(
                QStringLiteral("Land me on the detail pane"));
            QApplication::processEvents();

            check(hiddenBefore && number > 0 && qaWindow.testIssueDetailVisible(),
                  QString("quick-add without an agent opens the new issue's "
                          "detail pane (hiddenBefore=%1 number=%2 visible=%3) "
                          "(#203)")
                      .arg(hiddenBefore)
                      .arg(number)
                      .arg(qaWindow.testIssueDetailVisible()));
            stopChildProcesses(qaWindow);
        }
    }

    // Issue #287: the headless "mirrors" view leads with this node's own CPU and
    // memory so an operator watching a durable daemon can see its load, and the
    // "status" view carries the same Load line.
    {
        const QStringList mirrorLines = window.headlessMirrorLines();
        check(!mirrorLines.isEmpty() &&
                  mirrorLines.first().startsWith(QStringLiteral("node load:")) &&
                  mirrorLines.first().contains(QStringLiteral("cpu")) &&
                  mirrorLines.first().contains(QStringLiteral("mem")),
              QStringLiteral("headless mirrors view leads with a cpu/memory load line"));

        bool statusHasLoad = false;
        for (const QString &line : window.headlessStatusLines()) {
            if (line.startsWith(QStringLiteral("Load:")) &&
                line.contains(QStringLiteral("cpu")) &&
                line.contains(QStringLiteral("mem"))) {
                statusHasLoad = true;
                break;
            }
        }
        check(statusHasLoad,
              QStringLiteral("headless status view includes a cpu/memory load line"));
    }

    // issue #154: references inside an issue/PR comment body become in-app links.
    {
        check(MainWindow::autolinkReferences(QStringLiteral("see #123 please")) ==
                  QStringLiteral("see [#123](forkmesh-ref:123) please"),
              QStringLiteral("autolink turns #123 into a ref link"));
        check(MainWindow::autolinkReferences(QStringLiteral("fixed in a1b2c3d.")) ==
                  QStringLiteral("fixed in [a1b2c3d](forkmesh-commit:a1b2c3d)."),
              QStringLiteral("autolink turns a pasted commit SHA into a commit link"));
        check(MainWindow::autolinkReferences(QStringLiteral("build 1234567 ok")) ==
                  QStringLiteral("build 1234567 ok"),
              QStringLiteral("autolink leaves a plain number (no a-f) untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("use `#5` token")) ==
                  QStringLiteral("use `#5` token"),
              QStringLiteral("autolink leaves references in inline code untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("```\n#5\n```")) ==
                  QStringLiteral("```\n#5\n```"),
              QStringLiteral("autolink leaves references in a fenced block untouched"));
        check(MainWindow::autolinkReferences(QStringLiteral("[#5](http://x)")) ==
                  QStringLiteral("[#5](http://x)"),
              QStringLiteral("autolink never nests inside an existing markdown link"));
        check(MainWindow::autolinkReferences(QStringLiteral("at http://x/#5 only")) ==
                  QStringLiteral("at http://x/#5 only"),
              QStringLiteral("autolink leaves a #fragment inside a URL untouched"));
        check(MainWindow::autolinkReferences(
                  QStringLiteral("ref forkmesh://issue/o/r/12#e3 here")) ==
                  QStringLiteral("ref <forkmesh://issue/o/r/12#e3> here"),
              QStringLiteral("autolink wraps a pasted forkmesh:// permalink as a link"));
        check(MainWindow::autolinkReferences(
                  QStringLiteral("see forkmesh://pull/o/r/7.")) ==
                  QStringLiteral("see <forkmesh://pull/o/r/7>."),
              QStringLiteral("autolink leaves trailing punctuation out of a permalink"));

        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("review feat/clickable-agent-transcripts")) ==
                  QStringLiteral("review [feat/clickable-agent-transcripts]"
                                 "(forkmesh-branch:feat%2Fclickable-agent-transcripts)"),
              QStringLiteral("agent transcript links a feature branch"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("open qt_client/src/ClaudeTranscriptView.cpp:1445")) ==
                  QStringLiteral("open [qt_client/src/ClaudeTranscriptView.cpp:1445]"
                                 "(forkmesh-file:qt_client%2Fsrc%2FClaudeTranscriptView.cpp?line=1445)"),
              QStringLiteral("agent transcript links a repo file at a line"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("open /repo/qt_client/src/MainWindow.cpp#L42")) ==
                  QStringLiteral("open [/repo/qt_client/src/MainWindow.cpp#L42]"
                                 "(forkmesh-file:%2Frepo%2Fqt_client%2Fsrc%2FMainWindow.cpp?line=42)"),
              QStringLiteral("agent transcript links an absolute file at a line"));
        check(ClaudeTranscriptView::linkifyReferences(QStringLiteral("edit MainWindow.h")) ==
                  QStringLiteral("edit [MainWindow.h](forkmesh-file:MainWindow.h)"),
              QStringLiteral("agent transcript links a bare filename"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("see #123 and a1b2c3d")) ==
                  QStringLiteral("see [#123](forkmesh-ref:123) and "
                                 "[a1b2c3d](forkmesh-commit:a1b2c3d)"),
              QStringLiteral("agent transcript links issue and commit references"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("show d6c14744")) ==
                  QStringLiteral("show [d6c14744](forkmesh-commit:d6c14744)"),
              QStringLiteral("agent transcript links hexadecimal commit references"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("```\nMainWindow.h\nfeat/not-a-link\n```")) ==
                  QStringLiteral("```\nMainWindow.h\nfeat/not-a-link\n```"),
              QStringLiteral("agent transcript leaves fenced code untouched"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("[MainWindow.h](https://example.test/file)")) ==
                  QStringLiteral("[MainWindow.h](https://example.test/file)"),
              QStringLiteral("agent transcript never nests an existing link"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("use `MainWindow.h` next")) ==
                  QStringLiteral("use [MainWindow.h](forkmesh-file:MainWindow.h) next"),
              QStringLiteral("agent transcript makes an exact inline filename clickable"));
        check(ClaudeTranscriptView::linkifyReferences(
                  QStringLiteral("visit https://example.com/MainWindow.h")) ==
                  QStringLiteral("visit https://example.com/MainWindow.h"),
              QStringLiteral("agent transcript leaves web URLs intact"));
    }

    // issue #195: a commit SHA mentioned in a commit message body becomes a
    // commit: link the detail view navigates to via showCommit, so clicking a
    // commit hash brings you to that commit. "#123" still resolves to issue/PR.
    {
        check(forkmesh::ui::linkifyIssueRefs(QStringLiteral("reverts a1b2c3d4 now")) ==
                  QStringLiteral("reverts <a href=\"commit:a1b2c3d4\" "
                                 "style=\"color:#58a6ff;text-decoration:none\">"
                                 "a1b2c3d4</a> now"),
              QStringLiteral("commit message linkifies a SHA into a commit: link (#195)"));
        check(forkmesh::ui::linkifyIssueRefs(QStringLiteral("fixes #42")) ==
                  QStringLiteral("fixes <a href=\"ref:42\" "
                                 "style=\"color:#58a6ff;text-decoration:none\">#42</a>"),
              QStringLiteral("commit message still linkifies #123 into a ref: link"));
        check(forkmesh::ui::linkifyIssueRefs(QStringLiteral("build 1234567 ok")) ==
                  QStringLiteral("build 1234567 ok"),
              QStringLiteral("commit message leaves a plain number (no a-f) untouched"));
    }

    // adhoc #88: the agent list's Model column shows a human-readable label
    // for the session's selected LLM, falling back to the raw id for anything
    // not in the known-alias table, and blank (provider default) when unset.
    {
        check(forkmesh::ui::agentModelLabel(QStringLiteral("")).isEmpty(),
              QStringLiteral("agentModelLabel is blank for no selected model"));
        check(forkmesh::ui::agentModelLabel(QStringLiteral("   ")).isEmpty(),
              QStringLiteral("agentModelLabel treats whitespace-only as unset"));
        check(forkmesh::ui::agentModelLabel(QStringLiteral("opus")) ==
                  QStringLiteral("Opus"),
              QStringLiteral("agentModelLabel maps the short \"opus\" alias"));
        check(forkmesh::ui::agentModelLabel(QStringLiteral("sonnet")) ==
                  QStringLiteral("Sonnet"),
              QStringLiteral("agentModelLabel maps the short \"sonnet\" alias"));
        check(forkmesh::ui::agentModelLabel(QStringLiteral("claude-opus-4-8")) ==
                  QStringLiteral("Opus 4.8"),
              QStringLiteral("agentModelLabel maps a full model id to its friendly name"));
        check(forkmesh::ui::agentModelLabel(QStringLiteral("some-future-model-id")) ==
                  QStringLiteral("some-future-model-id"),
              QStringLiteral("agentModelLabel passes an unknown model id through as-is"));
    }

    // adhoc #76: continuing a session with a different provider must not carry
    // the previous provider's model across. agentModelMatchesProvider is the
    // guard that keeps a Claude model off a Codex run (and vice versa), and
    // treats an empty model as "use the provider default".
    {
        using forkmesh::ui::agentModelIsClaudeStyle;
        using forkmesh::ui::agentModelMatchesProvider;
        check(agentModelIsClaudeStyle(QStringLiteral("claude-opus-4-8")),
              QStringLiteral("a claude-* id is Claude-style"));
        check(agentModelIsClaudeStyle(QStringLiteral("opus")),
              QStringLiteral("the short opus alias is Claude-style"));
        check(agentModelIsClaudeStyle(QStringLiteral("auto")),
              QStringLiteral("the auto router sentinel is Claude-style"));
        check(!agentModelIsClaudeStyle(QStringLiteral("gpt-5.5")),
              QStringLiteral("a gpt-* id is not Claude-style"));
        check(!agentModelIsClaudeStyle(QString()),
              QStringLiteral("an empty model is not Claude-style"));

        check(agentModelMatchesProvider(QStringLiteral("codex"), QString()),
              QStringLiteral("an empty model matches any provider (uses its default)"));
        check(!agentModelMatchesProvider(QStringLiteral("codex"),
                                         QStringLiteral("claude-opus-4-8")),
              QStringLiteral("a Claude model does not match the Codex provider"));
        check(agentModelMatchesProvider(QStringLiteral("codex"),
                                        QStringLiteral("gpt-5.5")),
              QStringLiteral("a gpt model matches the Codex provider"));
        check(agentModelMatchesProvider(QStringLiteral("claude-code"),
                                        QStringLiteral("claude-opus-4-8")),
              QStringLiteral("a Claude model matches the Claude Code provider"));
        check(!agentModelMatchesProvider(QStringLiteral("claude-code"),
                                         QStringLiteral("gpt-5.5")),
              QStringLiteral("a gpt model does not match the Claude Code provider"));
        check(agentModelMatchesProvider(QStringLiteral("claude-api"),
                                        QStringLiteral("opus")),
              QStringLiteral("the opus alias matches a Claude API provider"));
    }

    // adhoc #191: the issue looper (and per-issue agent assignment) must work on
    // a node that only mirrors a repo it doesn't host. Such a repo has a bare
    // network mirror and no working tree, so the gate now resolves the bare mirror
    // as the git dir agents run against instead of refusing with "only the host".
    {
        QTemporaryDir mirrorDir;
        const bool madeBare =
            mirrorDir.isValid() &&
            runGitChecked(mirrorDir.path(), {"init", "--bare", "-q"});
        const int mirrorIdx =
            window.testAddPublishedRepository("someone", "theirrepo", mirrorDir.path());
        check(madeBare &&
                  window.testRepoAgentGitDir(mirrorIdx) == mirrorDir.path(),
              QStringLiteral("a mirror-only repo resolves its bare mirror as the "
                             "agent/looper git dir (adhoc #191)"));

        QTemporaryDir localRepo;
        const bool madeLocal = initGitRepo(localRepo);
        const int localIdx =
            window.testAddLocalRepository("me", "minerepo", localRepo.path());
        check(madeLocal &&
                  window.testRepoAgentGitDir(localIdx) == localRepo.path(),
              QStringLiteral("a hosted repo still resolves its working tree as the "
                             "agent/looper git dir"));

        QTemporaryDir emptyDir;
        const int noneIdx = window.testAddPublishedRepository(
            "someone", "uncached", emptyDir.path() + QStringLiteral("/missing.git"));
        check(window.testRepoAgentGitDir(noneIdx).isEmpty(),
              QStringLiteral("a repo with neither a working tree nor a cached "
                             "mirror has no agent/looper git dir"));
    }

    // adhoc #38: the issue looper skips issues that are already assigned (a
    // looper on this or another mirror claimed them) so two loopers never work
    // the same task, and otherwise picks the highest-priority open issue with no
    // local agent session, breaking ties on the lowest number.
    {
        auto makeIssue = [](int number, int priority, const QStringList &assignees,
                            const QString &status = QStringLiteral("open")) {
            Issue i;
            i.number = number;
            i.priority = priority;
            i.assignees = assignees;
            i.status = status;
            return i;
        };
        QList<Issue> issues;
        issues << makeIssue(1, 2, {});                    // open, unclaimed
        issues << makeIssue(2, 1, {QStringLiteral("nodeB")}); // higher priority but claimed
        issues << makeIssue(3, 2, {});                    // open, unclaimed, ties #1
        auto none = [](int) { return false; };

        const Issue *pick = MainWindow::looperPickNext(issues, none);
        check(pick && pick->number == 1,
              QStringLiteral("looper skips the assigned issue and takes the "
                             "lowest-numbered open one (adhoc #38)"));

        // With #1 already worked by a local agent, the tie falls to #3 — #2 stays
        // skipped because it is assigned.
        auto onlyOne = [](int n) { return n == 1; };
        const Issue *pick2 = MainWindow::looperPickNext(issues, onlyOne);
        check(pick2 && pick2->number == 3,
              QStringLiteral("looper skips issues with a local session and never "
                             "takes an assigned issue"));

        // Every open issue claimed/worked -> nothing to pick.
        QList<Issue> allClaimed;
        allClaimed << makeIssue(4, 1, {QStringLiteral("nodeA")});
        allClaimed << makeIssue(5, 1, {}, QStringLiteral("closed"));
        check(MainWindow::looperPickNext(allClaimed, none) == nullptr,
              QStringLiteral("looper finds nothing when all open issues are "
                             "assigned"));
    }

    // issue #291: when an agent task's worktree or PR is merged into the base
    // branch, the session must be flagged "merged" on both the agent list's Status
    // column and its detail page. Inject a finished session on a branch, drive the
    // eager in-app merge path (the one mergeWorktreeIntoMain / mergeCurrentPull
    // run), and confirm the Status cell flips from the run status to "merged".
    {
        window.testOpenRepository(repoIdx);
        AgentSession mergeSession;
        mergeSession.id = 2910;
        mergeSession.owner = QStringLiteral("me");
        mergeSession.name = QStringLiteral("r");
        mergeSession.branchName = QStringLiteral("agent/issue-291-merge-note");
        mergeSession.issueNumber = 291;
        mergeSession.issueTitle = QStringLiteral("note a task merging into main");
        mergeSession.baseBranch = QStringLiteral("main");
        mergeSession.status = AgentStatus::Success;
        // adhoc #1443: the "#" cell leads with the provider that ran the session,
        // and the hover card names it.
        mergeSession.provider = QStringLiteral("claude-code");
        check(!mergeSession.merged,
              QStringLiteral("the merge-note fixture starts unmerged"));
        check(window.testAgentStatusCellText(mergeSession.id).isEmpty(),
              QStringLiteral("the merge-note fixture uses an unused session id"));
        window.testAddAgentSession(mergeSession);

        // Before the merge the Status cell shows the run status, not "merged".
        check(window.testAgentStatusCellText(2910) == QStringLiteral("Success") &&
                  !window.testAgentSessionMerged(2910),
              QString("a finished agent session is not flagged merged until its "
                      "worktree/PR lands (issue #291, cell = %1)")
                  .arg(window.testAgentStatusCellText(2910)));

        // A completed agent cannot claim that its branch landed merely by naming
        // it. Only a merge path that already proved the Git/PullStore operation
        // succeeded may set the durable merged state.
        check(!window.testMarkAgentBranchMerged(
                  QStringLiteral("agent/issue-291-merge-note")),
              QStringLiteral("an unverified agent branch cannot mark its session "
                             "merged (issue #291)"));
        check(!window.testAgentSessionMerged(2910) &&
                  window.testAgentStatusCellText(2910) == QStringLiteral("Success"),
              QStringLiteral("a rejected merge claim leaves the run status intact "
                             "(issue #291)"));

        check(window.testMarkAgentBranchMerged(
                  QStringLiteral("agent/issue-291-merge-note"),
                  /*mergeVerified=*/true),
              QStringLiteral("a verified branch merge flags its agent session "
                             "(issue #291)"));

        // The Status column now reads "merged" and the flag is persisted, so both
        // the list and the detail page surface the note.
        check(window.testAgentSessionMerged(2910) &&
                  window.testAgentStatusCellText(2910) == QStringLiteral("merged"),
              QString("a merged agent task reads \"merged\" on its status column "
                      "(issue #291, cell = %1)")
                  .arg(window.testAgentStatusCellText(2910)));

        // adhoc #403: the Status cell's branch chip also carries the session's
        // files-changed count, its worktree's uncommitted-entry count and the
        // worktree path (empty once the checkout is gone) — the three badges
        // AgentBranchButtonDelegate paints beside the branch glyph.
        AgentDiffStat chip;
        chip.files = 7;
        chip.dirty = 2;
        chip.worktree = QStringLiteral("/tmp/wt-291");
        chip.behind = 9;
        chip.ahead = 4;
        check(window.testAgentStatusCellBadges(2910, chip) ==
                  QStringLiteral("7|2|/tmp/wt-291|9|4"),
              QString("the branch chip carries files/dirty/worktree and branch "
                      "health badges "
                      "(adhoc #403, got %1)")
                  .arg(window.testAgentStatusCellBadges(2910, chip)));
        const QString agentTip = window.testAgentStatusCellToolTip(2910, chip);
        check(agentTip.startsWith(QStringLiteral("<table")) &&
                  agentTip.contains(QStringLiteral("Agent #2910")) &&
                  agentTip.contains(QStringLiteral("7 files changed")) &&
                  agentTip.contains(QStringLiteral("2 uncommitted changes")) &&
                  agentTip.contains(QStringLiteral("4 ahead")) &&
                  agentTip.count(QStringLiteral("<tr>")) ==
                      agentTip.count(QStringLiteral("<img ")),
              QStringLiteral("every line in an agent hover card has an icon"));
        // adhoc #1443: the hover card also has to explain the marks the cell
        // paints — which agent ran it, the branch button's ring colour, the amber
        // dot on its corner, and the down arrow beside it — since none of them
        // can be read off the row on their own.
        check(agentTip.contains(QStringLiteral("Claude Code")) &&
                  agentTip.contains(QStringLiteral("`claude` CLI")),
              QStringLiteral("the hover card names the provider that ran the "
                             "session (adhoc #1443)"));
        check(agentTip.contains(QStringLiteral("blue ring")) &&
                  agentTip.contains(QStringLiteral("still on disk")),
              QStringLiteral("the hover card explains the branch button's ring "
                             "colour (adhoc #1443)"));
        check(agentTip.contains(QStringLiteral("amber dot")),
              QStringLiteral("the hover card explains the dot on the branch "
                             "button (adhoc #1443)"));
        check(agentTip.contains(QString::fromUtf8("\xE2\xAC\x87 arrow")) &&
                  agentTip.contains(QStringLiteral("9 commits behind main")),
              QStringLiteral("the hover card explains the down arrow beside the "
                             "branch button (adhoc #1443)"));
        // QStringLiteral wraps a u"" literal, so a UTF-8 byte escape in one lands
        // as a code point per byte: the churn line used to read "added Â·
        // removed" in the card. Every separator has to survive as itself.
        check(agentTip.contains(QString::fromUtf8("added \xC2\xB7 ")) &&
                  !agentTip.contains(QString::fromUtf8("\xC3\x82")),
              QStringLiteral("the hover card's punctuation is not mangled into "
                             "mojibake (adhoc #1443)"));
        // A session whose checkout has been cleaned up says so, and says the ring
        // goes grey with it.
        AgentDiffStat gone = chip;
        gone.worktree.clear();
        gone.dirty = 0;
        gone.behind = 0;
        const QString goneTip = window.testAgentStatusCellToolTip(2910, gone);
        check(goneTip.contains(QStringLiteral("grey ring")) &&
                  goneTip.contains(QStringLiteral("no dot")) &&
                  !goneTip.contains(QString::fromUtf8("\xE2\xAC\x87 arrow")),
              QStringLiteral("a cleaned-up, clean, up-to-date session explains a "
                             "grey ring and no dot, and mentions no arrow "
                             "(adhoc #1443)"));
        // A cleaned-up session with no patch yet leaves every badge unknown, so
        // the chip falls back to the plain branch button.
        check(window.testAgentStatusCellBadges(2910, AgentDiffStat()) ==
                  QStringLiteral("-1|-1||-1|-1"),
              QString("a session with no patch/worktree paints a bare branch chip "
                      "(adhoc #403, got %1)")
                  .arg(window.testAgentStatusCellBadges(2910, AgentDiffStat())));

        // A branch with no attached session must not be flagged.
        check(!window.testMarkAgentBranchMerged(
                  QStringLiteral("agent/issue-291-unrelated")),
              QStringLiteral("merging an unrelated branch flags no agent session "
                             "(issue #291)"));
    }

    // A branch is mutable after an agent starts. In particular, an agent that
    // resets it to an advanced main must not be read as having merged its work:
    // the commits after baseRef belong to main, not to the agent. The background
    // detector instead needs a tip it observed while that tip was outside main.
    {
        QTemporaryDir mergeDetectionRepo;
        if (initGitRepo(mergeDetectionRepo)) {
            const int mergeDetectionRepoIdx = window.testAddLocalRepository(
                "me", "merge-detection", mergeDetectionRepo.path());
            window.testOpenRepository(mergeDetectionRepoIdx);
            QElapsedTimer idleTimer;
            idleTimer.start();
            while (window.testAgentMergeStateRefreshing() && idleTimer.elapsed() < 5000)
                QApplication::processEvents(QEventLoop::AllEvents, 10);

            const QString baseRef =
                gitOutput(mergeDetectionRepo.path(), {"rev-parse", "main"});
            const QString resetBranch =
                QStringLiteral("agent/issue-291-reset-to-main");
            runGitChecked(mergeDetectionRepo.path(), {"checkout", "-b", resetBranch});
            QFile resetFile(mergeDetectionRepo.path() + QStringLiteral("/reset.txt"));
            if (resetFile.open(QIODevice::WriteOnly)) {
                resetFile.write("discarded agent draft\n");
                resetFile.close();
            }
            runGitChecked(mergeDetectionRepo.path(), {"add", "reset.txt"});
            runGitChecked(mergeDetectionRepo.path(),
                          {"commit", "-m", "temporary agent draft"});
            runGitChecked(mergeDetectionRepo.path(), {"checkout", "main"});
            runGitChecked(mergeDetectionRepo.path(),
                          {"commit", "--allow-empty", "-m", "advance main"});
            runGitChecked(mergeDetectionRepo.path(),
                          {"branch", "-f", resetBranch, "main"});

            AgentSession resetSession;
            resetSession.id = 2911;
            resetSession.owner = QStringLiteral("me");
            resetSession.name = QStringLiteral("merge-detection");
            resetSession.branchName = resetBranch;
            resetSession.baseRef = baseRef;
            resetSession.baseBranch = QStringLiteral("main");
            resetSession.status = AgentStatus::Success;
            window.testAddAgentSession(resetSession);
            window.testRefreshAgentMergeState();
            QElapsedTimer resetScanTimer;
            resetScanTimer.start();
            while (window.testAgentMergeStateRefreshing() &&
                   resetScanTimer.elapsed() < 5000)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            check(!window.testAgentMergeStateRefreshing() &&
                      !window.testAgentSessionMerged(resetSession.id),
                  QStringLiteral("resetting an agent branch to an advanced main does "
                                 "not self-report a merge (issue #291)"));

            const QString landedBranch =
                QStringLiteral("agent/issue-291-observed-tip");
            runGitChecked(mergeDetectionRepo.path(), {"checkout", "-b", landedBranch});
            QFile landedFile(mergeDetectionRepo.path() + QStringLiteral("/landed.txt"));
            if (landedFile.open(QIODevice::WriteOnly)) {
                landedFile.write("agent work that landed\n");
                landedFile.close();
            }
            runGitChecked(mergeDetectionRepo.path(), {"add", "landed.txt"});
            runGitChecked(mergeDetectionRepo.path(),
                          {"commit", "-m", "agent change"});
            const QString observedHead =
                gitOutput(mergeDetectionRepo.path(), {"rev-parse", landedBranch});
            runGitChecked(mergeDetectionRepo.path(), {"checkout", "main"});
            runGitChecked(mergeDetectionRepo.path(),
                          {"merge", "--no-ff", landedBranch, "-m", "merge agent work"});

            AgentSession landedSession;
            landedSession.id = 2912;
            landedSession.owner = QStringLiteral("me");
            landedSession.name = QStringLiteral("merge-detection");
            landedSession.branchName = landedBranch;
            landedSession.baseBranch = QStringLiteral("main");
            landedSession.mergeCandidateHead = observedHead;
            landedSession.status = AgentStatus::Success;
            window.testAddAgentSession(landedSession);
            window.testRefreshAgentMergeState();
            QElapsedTimer landedScanTimer;
            landedScanTimer.start();
            while (window.testAgentMergeStateRefreshing() &&
                   landedScanTimer.elapsed() < 5000)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            check(!window.testAgentMergeStateRefreshing() &&
                      window.testAgentSessionMerged(landedSession.id),
                  QStringLiteral("an observed agent tip is marked merged only after "
                                 "Git proves it reached main (issue #291)"));
            window.testOpenRepository(repoIdx);
        }
    }

    // The top bar's search box searches the page in front of you: on the Agents
    // tab it narrows the session list as each character lands, matches sessions on
    // what their transcripts say (not just their prompt), and drives the open
    // session's transcript search so hits highlight in place.
    {
        window.testOpenRepository(repoIdx); // "me/r", the Agents tab's repo
        window.testOpenAgentsOverview();

        // A PR opened from an Agent branch persists the number on that exact
        // repo's session. Same-number PRs and same-named branches in another
        // repository must never cross-link.
        AgentSession prAgent;
        prAgent.id = 7391;
        prAgent.owner = QStringLiteral("me");
        prAgent.name = QStringLiteral("r");
        prAgent.branchName = QStringLiteral("agent/adhoc-7391-pr-link");
        prAgent.status = AgentStatus::Success;
        window.testAddAgentSession(prAgent);
        AgentSession foreignPrAgent = prAgent;
        foreignPrAgent.id = 7392;
        foreignPrAgent.owner = QStringLiteral("someone-else");
        window.testAddAgentSession(foreignPrAgent);
        check(window.testBindAgentSessionsToPull(
                  739, QStringLiteral("agent/adhoc-7391-pr-link")) &&
                  window.testAgentSessionPullNumber(7391) == 739 &&
                  window.testAgentSessionPullNumber(7392) == 0,
              QStringLiteral("PR creation durably binds only the matching "
                             "repo's Agent session"));
        check(window.testAgentSessionForPullId(
                  739, QStringLiteral("agent/adhoc-7391-pr-link")) == 7391,
              QStringLiteral("PR lookup is repository-scoped by number and "
                             "branch"));
        check(window.testAgentPrButtonText(7391) ==
                  QStringLiteral("View PR #739"),
              QStringLiteral("an Agent's Create PR action becomes a numbered "
                             "link to the created pull request"));

        check(window.testRepoRequiresPeerApproval(),
              QStringLiteral("repositories require peer approval by default"));
        window.testSetRepoRequirePeerApproval(false);
        check(!window.testRepoRequiresPeerApproval(),
              QStringLiteral("repository settings can make peer approval "
                             "optional"));
        bool savedOptionalPolicy = false;
        {
            QSettings settings;
            const int count = settings.beginReadArray(
                QStringLiteral("repositories/items"));
            for (int i = 0; i < count; ++i) {
                settings.setArrayIndex(i);
                if (settings.value(QStringLiteral("owner")).toString() ==
                        QLatin1String("me") &&
                    settings.value(QStringLiteral("name")).toString() ==
                        QLatin1String("r")) {
                    savedOptionalPolicy =
                        !settings.value(QStringLiteral("requirePeerApproval"),
                                        true)
                             .toBool();
                    break;
                }
            }
            settings.endArray();
        }
        check(savedOptionalPolicy,
              QStringLiteral("the optional peer-approval policy persists on "
                             "the repository record"));
        window.testSetRepoRequirePeerApproval(true);

        check(window.testBindAgentSessionsToPull(
                  740, QStringLiteral("manual/pr-740")) &&
                  window.testAgentSessionForPullId(
                      740, QStringLiteral("manual/pr-740")) > 0,
              QStringLiteral("a manual PR receives a provenance-only Agent "
                             "association"));
        QApplication::processEvents();

        AgentSession titled;
        titled.id = 7401;
        titled.owner = QStringLiteral("me");
        titled.name = QStringLiteral("r");
        titled.issueTitle =
            QStringLiteral("only seeing recovery pings, want failure pings too");
        titled.status = AgentStatus::Success;
        window.testAddAgentSession(titled);

        AgentSession quiet;
        quiet.id = 7402;
        quiet.owner = QStringLiteral("me");
        quiet.name = QStringLiteral("r");
        quiet.issueTitle = QStringLiteral("tidy the release checklist");
        quiet.status = AgentStatus::Success;
        window.testAddAgentSession(quiet);
        // Only this session's transcript mentions the query; its title does not.
        window.testAppendAgentTranscript(
            quiet.id, QStringLiteral("checked the recovery ping path first"));

        AgentSession silent;
        silent.id = 7403;
        silent.owner = QStringLiteral("me");
        silent.name = QStringLiteral("r");
        silent.issueTitle = QStringLiteral("bump the icon cache");
        silent.status = AgentStatus::Success;
        window.testAddAgentSession(silent);

        // Typing up top filters the list below straight away — no Enter, and no
        // waiting on the dropdown's debounce.
        window.testTypeGlobalSearch(QStringLiteral("recovery"));
        QApplication::processEvents();
        check(window.testAgentSearchText() == QStringLiteral("recovery"),
              QString("the top-bar search mirrors into the Agents page's own "
                      "filter (got \"%1\")")
                  .arg(window.testAgentSearchText()));
        check(window.testTranscriptSearchText() == QStringLiteral("recovery"),
              QString("the same query drives the open session's transcript "
                      "search (got \"%1\")")
                  .arg(window.testTranscriptSearchText()));
        QStringList titles = window.testAgentRowTitles();
        check(titles.size() == 1 && titles.first().contains(
                  QStringLiteral("only seeing recovery pings")),
              QString("the list is narrowed to the session whose title matches "
                      "(rows: %1)")
                  .arg(titles.join(QStringLiteral(" | "))));

        // The transcript scan is debounced off the GUI thread; run it now and let
        // its result land. The session that only ever said "recovery" mid-run
        // joins the list, and says how many times it was found.
        window.testRunAgentTranscriptSearch();
        QElapsedTimer transcriptSearchTimer;
        transcriptSearchTimer.start();
        while (transcriptSearchTimer.elapsed() < 5000) {
            QApplication::processEvents();
            titles = window.testAgentRowTitles();
            if (titles.size() == 2)
                break;
        }
        const QString transcriptRow = titles.filter(
            QStringLiteral("release checklist")).value(0);
        check(titles.size() == 2 && !transcriptRow.isEmpty(),
              QString("a session whose transcript holds the query stays in the "
                      "list even though its title does not (rows: %1)")
                  .arg(titles.join(QStringLiteral(" | "))));
        check(transcriptRow.contains(QStringLiteral("1 in transcript")),
              QString("the row says the match came from the transcript (got "
                      "\"%1\")")
                  .arg(transcriptRow));
        check(!titles.join(QLatin1Char(' ')).contains(
                  QStringLiteral("icon cache")),
              QString("a session that matches neither title nor transcript stays "
                      "filtered out (rows: %1)")
                  .arg(titles.join(QStringLiteral(" | "))));

        // Clearing the top bar hands the whole list back.
        window.testTypeGlobalSearch(QString());
        QApplication::processEvents();
        titles = window.testAgentRowTitles();
        check(window.testAgentSearchText().isEmpty() && titles.size() >= 3,
              QString("clearing the top-bar search unfilters the session list "
                      "(filter \"%1\", %2 rows)")
                  .arg(window.testAgentSearchText())
                  .arg(titles.size()));
    }

    // adhoc #222: a session started from a pasted screenshot shows it as a little
    // square at the head of its row, and clicking that square opens the picture.
    // The scan for "Attached image:" lines and the decode both run off the GUI
    // thread, so the row fills in a beat after the session appears.
    {
        window.testOpenRepository(repoIdx); // "me/r", the Agents tab's repo
        window.testOpenAgentsOverview();
        QApplication::processEvents();

        QTemporaryDir shots;
        check(shots.isValid(), QStringLiteral("attachment fixture dir is valid"));
        const QString shotPath = shots.filePath(QStringLiteral("paste-222.png"));
        QImage shot(48, 24, QImage::Format_ARGB32);
        shot.fill(QColor("#3fb950"));
        check(shot.save(shotPath),
              QStringLiteral("the attachment fixture image is written to disk"));

        AgentSession pictured;
        pictured.id = 7411;
        pictured.owner = QStringLiteral("me");
        pictured.name = QStringLiteral("r");
        pictured.issueTitle = QStringLiteral("make the toolbar match this");
        pictured.prompt =
            QStringLiteral("make the toolbar match this\nAttached image: %1")
                .arg(shotPath);
        pictured.status = AgentStatus::Success;
        window.testAddAgentSession(pictured);

        AgentSession plain;
        plain.id = 7412;
        plain.owner = QStringLiteral("me");
        plain.name = QStringLiteral("r");
        plain.issueTitle = QStringLiteral("rename the release checklist");
        plain.prompt = QStringLiteral("rename the release checklist");
        plain.status = AgentStatus::Success;
        window.testAddAgentSession(plain);

        // The scan is delivered from a worker and the decode that follows it is a
        // second hop, so run the pass and then wait for the square itself.
        window.testScanAgentSessionImages();
        QElapsedTimer attachmentTimer;
        attachmentTimer.start();
        while (attachmentTimer.elapsed() < 5000) {
            QApplication::processEvents();
            if (window.testAgentRowHasThumbnail(7411))
                break;
        }
        check(window.testAgentRowImages(7411) == QStringList{shotPath},
              QString("the picture named in a session's prompt reaches its row "
                      "(adhoc #222, got %1)")
                  .arg(window.testAgentRowImages(7411).join(QStringLiteral(" | "))));
        check(window.testAgentRowHasThumbnail(7411),
              QStringLiteral("that row draws the attachment as a thumbnail "
                             "(adhoc #222)"));
        check(window.testAgentRowImages(7412).isEmpty() &&
                  !window.testAgentRowHasThumbnail(7412),
              QStringLiteral("a session with no attachment keeps a bare row "
                             "(adhoc #222)"));
    }

    // adhoc #15: the hidden rich renderer still pages its newest segment for
    // footer deep-links and add-to-prompt actions without returning the old
    // scrolling pane to the Log page.
    // scrolling to the top loads the next older segment instead of capping
    // history at whatever first rendered.
    {
        window.testResetNetworkLog();
        for (int i = 0; i < 800; ++i)
            window.testLogSystem(QString("Segment test line %1").arg(i));
        window.testShowLogSection();
        QApplication::processEvents();
        // Force a from-scratch render (as a cold start / first tab visit would)
        // over the now-populated buffer, rather than the live per-line append
        // path the loop above already exercised.
        window.testRebuildNetworkLogView();

        QTextBrowser *logView = window.testNetworkLogView();
        check(logView != nullptr, QStringLiteral("network log view exists"));
        if (logView) {
            const int initialBlocks = logView->document()->blockCount();
            check(initialBlocks < 800,
                  QString("initial network log render is segmented, not the full "
                          "800 lines (blocks=%1)")
                      .arg(initialBlocks));
            const QString initialText = logView->toPlainText();
            check(initialText.contains(QStringLiteral("Segment test line 799")) &&
                      !initialText.contains(QStringLiteral("Segment test line 0\n")) &&
                      !initialText.endsWith(QStringLiteral("Segment test line 0")),
                  QStringLiteral("initial segment shows the newest line but not "
                                 "the oldest"));

            // Scrolling to the top should pull in an older batch, growing the
            // rendered block count.
            window.testScrollNetworkLogToTop();
            const int afterOneScroll = logView->document()->blockCount();
            check(afterOneScroll > initialBlocks,
                  QString("scrolling to the top loads an older segment "
                          "(blocks %1 -> %2)")
                      .arg(initialBlocks)
                      .arg(afterOneScroll));

            // Keep scrolling to the top until the very first logged line
            // surfaces (or give up after a generous number of loads) — proves
            // history keeps loading further back, not just once.
            bool reachedOldest = false;
            for (int i = 0; i < 10 && !reachedOldest; ++i) {
                window.testScrollNetworkLogToTop();
                reachedOldest =
                    logView->toPlainText().contains(QStringLiteral("Segment test line 0\n")) ||
                    logView->toPlainText().endsWith(QStringLiteral("Segment test line 0"));
            }
            check(reachedOldest,
                  QStringLiteral("repeated scroll-to-top eventually reaches the "
                                 "oldest logged line"));

            // Once everything is loaded, scrolling to the top again is a no-op
            // (no crash, no further growth).
            const int fullBlocks = logView->document()->blockCount();
            window.testScrollNetworkLogToTop();
            check(logView->document()->blockCount() == fullBlocks,
                  QStringLiteral("scrolling to the top with nothing older left "
                                 "does not change the view"));
        }
    }

    // adhoc #442: UI stalls are their own log category, and the chip row always
    // offers a Stalls filter so a freeze can be pulled up on demand.
    {
        window.testShowLogSection();
        QApplication::processEvents();
        // Opening a deferred panel can itself trip the stall watchdog on a
        // heavily loaded CI host. Reset after it is open so this assertion
        // measures the empty-filter state, not test-machine startup latency.
        window.testResetNetworkLog();
        check(window.testLogFilterChipLabels().contains(QStringLiteral("STALL")),
              QStringLiteral("the log filter row offers a Stalls chip before any "
                             "stall has been recorded"));

        window.testSetLogFilter(QStringLiteral("STALL"));
        QTextBrowser *logView = window.testNetworkLogView();
        check(logView && logView->toPlainText().contains(
                             QStringLiteral("No STALL events recorded")),
              QStringLiteral("the Stalls filter says so when nothing stalled"));

        window.testLogSystem(QStringLiteral("Pushed 2 commits to origin"));
        window.testLogSystem(
            QStringLiteral("UI stalled ~900 ms (event loop blocked) while "
                           "git ls-tree (could not be avoided)"));
        const QStringList stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) == QStringLiteral("STALL"),
              QStringLiteral("a recorded UI stall badges as STALL, not ERROR, even "
                             "when its blocking call reads like a failure"));
        if (logView) {
            const QString filtered = logView->toPlainText();
            check(filtered.contains(QStringLiteral("UI stalled ~900 ms")) &&
                      !filtered.contains(QStringLiteral("Pushed 2 commits")) &&
                      !filtered.contains(QStringLiteral("No STALL events recorded")),
                  QStringLiteral("the Stalls filter shows the stall and hides "
                                 "unrelated log lines"));
        }
        window.testSetLogFilter(QString());
        if (logView)
            check(logView->toPlainText().contains(QStringLiteral("Pushed 2 commits")),
                  QStringLiteral("clearing the filter restores every log line"));

        // adhoc #64: each chip carries how many buffered lines it covers, and
        // All counts the whole buffer.
        {
            const QStringList labels = window.testLogFilterChipLabels();
            check(labels.contains(QStringLiteral("All 2")) &&
                      labels.contains(QStringLiteral("STALL 1")) &&
                      labels.contains(QStringLiteral("GIT 1")),
                  QStringLiteral("log filter chips show the event count for each "
                                 "category"));
        }
        window.testResetNetworkLog();
        check(window.testLogFilterChipLabels().contains(QStringLiteral("STALL")),
              QStringLiteral("an empty category's chip shows no count at all"));
    }

    // Background outcome lines split by which side of the ✓ / ✕ they report, so
    // "how much of this session was not backgrounded" is countable on its own
    // instead of sharing one tally with the healthy runs.
    {
        window.testResetNetworkLog();
        window.testLogSystem(forkmesh::backgroundOutcomeLine(
            QStringLiteral("git"), 1, 48, QString(), /*backgrounded=*/true));
        QStringList stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) == QStringLiteral("BGTASK"),
              QStringLiteral("a backgrounded outcome line badges as BGTASK"));

        window.testLogSystem(forkmesh::backgroundOutcomeLine(
            QStringLiteral("git"), 3, 1400, QString(), /*backgrounded=*/false));
        stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) == QStringLiteral("BGBLOCK"),
              QStringLiteral("work that was not backgrounded badges as BGBLOCK, "
                             "its own category"));

        const QStringList labels = window.testLogFilterChipLabels();
        check(labels.contains(QStringLiteral("BGTASK 1")) &&
                  labels.contains(QStringLiteral("BGBLOCK 1")),
              QStringLiteral("the log filter row counts the backgrounded and "
                             "not-backgrounded halves separately"));
        window.testResetNetworkLog();
    }

    // adhoc #1546: red means "this request failed". The verbose net line quotes
    // the peeked response body, and the ping inbox's recovery notices name the
    // outage they close ("Last failure: …") — a healthy 200 whose payload says
    // {"ok":true,…} must not be painted red by words from that body. A reply
    // that really failed still is, including a 200 carrying {"ok":false,…}.
    {
        window.testResetNetworkLog();
        window.testLogSystem(QStringLiteral(
            "net GET 200 [body: {\"ok\":true,\"notifications\":[{\"kind\":"
            "\"operational_alert\",\"title\":\"Mirror node - mirror10 "
            "recovered\",\"body\":\"Down for under a minute. Last failure: "
            "mirror10 has not supplied a fresh signed ForkMesh repository "
            "proof\"}]}] https://forkmesh.com/api/notifications?node=jett "
            "\xC2\xB7 ping inbox"));
        QStringList stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) != QStringLiteral("ERROR"),
              QStringLiteral("a 200 whose ok:true body quotes a recovered "
                             "outage is not badged ERROR"));

        window.testResetNetworkLog();
        window.testLogSystem(QStringLiteral(
            "net POST 200 [body: {\"ok\":false,\"error\":\"forbidden\"}] "
            "https://forkmesh.com/api/notifications \xC2\xB7 ping inbox"));
        stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) == QStringLiteral("ERROR"),
              QStringLiteral("a 200 whose body reports ok:false is still ERROR"));

        window.testResetNetworkLog();
        window.testLogSystem(QStringLiteral(
            "net GET ERR 503 Service Unavailable [body: {\"ok\":false,"
            "\"error\":\"mirror_unavailable\"}] "
            "https://forkmesh.com/api/repo/jett/forkmesh \xC2\xB7 repo fetch"));
        stored = window.testNetworkLog();
        check(!stored.isEmpty() &&
                  window.testLogBadgeFor(stored.last()) == QStringLiteral("ERROR"),
              QStringLiteral("a failed reply is still classified from the body "
                             "that explains it"));
        window.testResetNetworkLog();
    }

    // adhoc #73: clicking the footer stall badge drafts a "fix these stalls"
    // prompt (with the log locations) into the quick-add composer, and every
    // stall also lands in the main app log rather than only the dialog.
    {
        window.testResetNetworkLog();
        window.testRecordUiStall(2100, QStringLiteral("git ls-tree"),
                                 QStringLiteral("#0 ForkMesh::renderAgentDiff()"));
        const QStringList logged = window.testNetworkLog();
        check(!logged.isEmpty() &&
                  logged.last().contains(QStringLiteral("UI stalled ~2100 ms")) &&
                  logged.last().contains(QStringLiteral("git ls-tree")),
              QStringLiteral("a recorded stall is written to the main app log"));

        if (window.testDraftStallPromptInComposer()) {
            const QString drafted = window.testQuickAddText();
            check(drafted.contains(QStringLiteral("Please fix these UI stalls")),
                  QStringLiteral("the stall badge fills the composer with a fix-it prompt"));
            check(drafted.contains(QStringLiteral("App log:")) &&
                      drafted.contains(QStringLiteral("network_log.txt")),
                  QStringLiteral("the drafted prompt names where the log file lives"));
            check(drafted.contains(QStringLiteral("renderAgentDiff")),
                  QStringLiteral("the drafted prompt carries the recorded backtrace"));
            // adhoc #90: the sampled frames only name the call that happened to
            // be on the stack, so the prompt also asks for a sweep of the logs.
            check(drafted.contains(QStringLiteral("never got backgrounded")),
                  QStringLiteral("the drafted prompt asks for un-backgrounded work too"));
            check(drafted.size() <= 16000,
                  QStringLiteral("the drafted prompt fits the composer's length cap"));
            check(drafted == window.testStallFixPrompt(),
                  QStringLiteral("the composer holds exactly the stall fix-it prompt"));
        }
        window.testResetNetworkLog();
    }

    // adhoc #436: hosts with a hardcoded mark (api.anthropic.com serves no
    // /favicon.ico, so fetching one logged a 404 error line of its own) resolve
    // locally, and every log line that names a host leads with an icon in both
    // the full Log view and the footer strip.
    {
        window.testResetNetworkLog();
        window.testLogSystem(
            QStringLiteral("net POST https://api.anthropic.com/v1/messages 200"));
        QApplication::processEvents();

        check(window.testFaviconCached(QStringLiteral("api.anthropic.com")),
              QStringLiteral("an Anthropic host resolves its favicon locally, "
                             "with no network fetch (adhoc #436)"));

        window.testLogSystem(
            QStringLiteral("net GET https://api.mainnet-beta.solana.com 200"));
        QApplication::processEvents();
        check(window.testFaviconCached(
                  QStringLiteral("api.mainnet-beta.solana.com")),
              QStringLiteral("the Solana RPC host resolves its favicon locally, "
                             "without requesting its unsupported favicon path"));

        auto leadsWithIcon = [](QTextEdit *view) {
            if (!view)
                return false;
            for (QTextBlock b = view->document()->firstBlock(); b.isValid();
                 b = b.next()) {
                for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                    const QTextCharFormat fmt = it.fragment().charFormat();
                    if (fmt.isImageFormat() &&
                        fmt.toImageFormat().name().startsWith(
                            QStringLiteral("favicon://")))
                        return true;
                }
            }
            return false;
        };

        window.testShowLogSection();
        window.testRebuildNetworkLogView();
        check(leadsWithIcon(window.testNetworkLogView()),
              QStringLiteral("the full Log view renders the site favicon inline"));
        check(leadsWithIcon(window.testFooterLogView()),
              QStringLiteral("the footer live-log strip renders the site favicon "
                             "inline too (adhoc #436)"));
    }

    // adhoc #114: every log entry leads, furthest left, with a plus that hands
    // that entry to the footer prompt box — one click instead of a
    // select-copy-paste round trip. Covered in both log surfaces.
    {
        window.testResetNetworkLog();
        const QString entry = QStringLiteral("Pushed 3 commits to origin/main");
        window.testLogSystem(entry);
        window.testShowLogSection();
        window.testRebuildNetworkLogView();
        QApplication::processEvents();

        const QString prefix = QStringLiteral("fmlogprompt:");
        // The anchor carries the entry's own dated line, so a click needs no
        // lookup back into the log buffer.
        auto firstPromptHref = [&prefix](QTextEdit *view) {
            if (!view)
                return QString();
            for (QTextBlock b = view->document()->firstBlock(); b.isValid();
                 b = b.next()) {
                for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
                    const QString href =
                        it.fragment().charFormat().anchorHref();
                    if (href.startsWith(prefix))
                        return href;
                }
            }
            return QString();
        };
        const QString logHref = firstPromptHref(window.testNetworkLogView());
        check(!logHref.isEmpty(),
              QStringLiteral("the full Log view leads every entry with an "
                             "add-to-prompt icon"));
        check(!firstPromptHref(window.testFooterLogView()).isEmpty(),
              QStringLiteral("the footer live-log strip leads every entry with "
                             "one too"));
        check(QUrl::fromPercentEncoding(logHref.mid(prefix.size()).toLatin1())
                  .endsWith(entry),
              QStringLiteral("the icon's anchor carries the log entry itself"));

        // Click it where it actually paints (the leftmost strip of a row), not
        // through a test-only shortcut, so the event-filter wiring is covered.
        // Hit-testing is by anchor content, not by "first icon in the viewport":
        // the footer strip keeps the lines it has already streamed, so its top
        // visible row is some older entry, not the one logged just above.
        auto clickPromptIcon = [&prefix, &entry](QTextEdit *view) {
            if (!view)
                return false;
            for (int y = 0; y < view->viewport()->height(); ++y) {
                for (int x = 0; x < 40; ++x) {
                    const QPoint pos(x, y);
                    const QString href = view->anchorAt(pos);
                    if (!href.startsWith(prefix) ||
                        !QUrl::fromPercentEncoding(
                             href.mid(prefix.size()).toLatin1())
                             .endsWith(entry))
                        continue;
                    const QPointF global = view->viewport()->mapToGlobal(pos);
                    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                                      global, Qt::LeftButton, Qt::LeftButton,
                                      Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                                        global, Qt::LeftButton, Qt::NoButton,
                                        Qt::NoModifier);
                    QApplication::sendEvent(view->viewport(), &press);
                    QApplication::sendEvent(view->viewport(), &release);
                    return true;
                }
            }
            return false;
        };

        const QString beforeClick = window.testQuickAddText();
        if (clickPromptIcon(window.testNetworkLogView())) {
            const QString afterClick = window.testQuickAddText();
            check(afterClick.endsWith(entry) && afterClick != beforeClick,
                  QStringLiteral("clicking a Log entry's icon appends that entry "
                                 "to the footer prompt"));
        } else {
            check(false, QStringLiteral("the Log entry's add-to-prompt icon is "
                                        "hit-testable in the view"));
        }
        if (clickPromptIcon(window.testFooterLogView())) {
            const QString afterFooter = window.testQuickAddText();
            check(afterFooter.endsWith(entry) &&
                      afterFooter.count(entry) == 2,
                  QStringLiteral("the footer strip's icon appends to the prompt "
                                 "instead of opening the full Log"));
        }
        window.testResetNetworkLog();
    }

    // "Merge & clean up" must leave nothing of the run behind: the branch's work
    // lands in main, and its branch *and* its Agent entry go with the cleanup.
    // Keeping the session listed was reported as "the agent entry is still around".
    // Runs last: it opens a repository of its own and deletes sessions from the
    // shared agent store, so it must not hand either on to another block.
    QTemporaryDir cleanupRepo;
    if (initGitRepo(cleanupRepo)) {
        const QString cleanBranch = QStringLiteral("agent/adhoc-1304-clean");
        runGitChecked(cleanupRepo.path(), {"checkout", "-q", "-b", cleanBranch});
        {
            QFile landed(cleanupRepo.path() + QStringLiteral("/landed.txt"));
            landed.open(QIODevice::WriteOnly);
            landed.write("agent work\n");
            landed.close();
        }
        runGitChecked(cleanupRepo.path(), {"add", "landed.txt"});
        runGitChecked(cleanupRepo.path(), {"commit", "-m", "agent work"});
        runGitChecked(cleanupRepo.path(), {"checkout", "-q", "main"});
        const int cleanupIdx =
            window.testAddLocalRepository("me", "cleanrepo", cleanupRepo.path());
        window.testOpenRepository(cleanupIdx);
        QApplication::processEvents();

        AgentSession ran;
        ran.id = 13040;
        ran.owner = QStringLiteral("me");
        ran.name = QStringLiteral("cleanrepo");
        ran.branchName = cleanBranch;
        ran.issueTitle = QStringLiteral("clean up after the merge");
        ran.baseBranch = QStringLiteral("main");
        ran.status = AgentStatus::Success;
        window.testAddAgentSession(ran);
        // The provenance-only record a manual PR leaves behind documents this same
        // branch, so it can't outlive the branch either.
        AgentSession provenance = ran;
        provenance.id = 13041;
        provenance.associationOnly = true;
        provenance.prNumber = 1304;
        window.testAddAgentSession(provenance);
        // A session on another branch is untouched by this cleanup.
        AgentSession other = ran;
        other.id = 13042;
        other.branchName = QStringLiteral("agent/adhoc-1304-other");
        other.associationOnly = false;
        other.prNumber = 0;
        window.testAddAgentSession(other);
        // adhoc #1537: a session whose stored status never came off "Running" —
        // a terminal `result` event that never landed, a run killed with the app,
        // a completion poll still waiting on a background process (adhoc
        // #143/#157) — must not make the merge insist the user stop an agent that
        // finished long ago ("Agent #N is still working" over completed work).
        // Nothing owns a runner, stream or queue slot for this id, so no work can
        // be in flight; if the gate believed the status anyway its modal would
        // block this test instead of letting the merge land below.
        AgentSession staleRunning = ran;
        staleRunning.id = 13043;
        staleRunning.status = AgentStatus::Running;
        staleRunning.startedAtMs =
            QDateTime::currentMSecsSinceEpoch() - 3600 * 1000;
        window.testAddAgentSession(staleRunning);

        // A prior crashed ref publication must not poison every later branch
        // merge. Model the orphaned lock from the reported failure; a lock this
        // old cannot belong to a live HEAD update and is safe to recover.
        const QString staleHeadLock =
            cleanupRepo.path() + QStringLiteral("/.git/HEAD.lock");
        QFile staleLock(staleHeadLock);
        if (staleLock.open(QIODevice::WriteOnly)) {
            staleLock.setFileTime(QDateTime::currentDateTime().addSecs(-120),
                                  QFileDevice::FileModificationTime);
            staleLock.close();
        }

        check(window.testMergeBranchAndCleanUp(cleanBranch),
              QStringLiteral("\"Merge & clean up\" lands the agent branch in the "
                             "default branch, and a session left stuck on "
                             "\"Running\" with nothing executing it doesn't hold "
                             "the merge back (adhoc #1537)"));
        check(!QFileInfo::exists(staleHeadLock),
              QStringLiteral("merge recovers an orphaned HEAD.lock instead of "
                             "misreporting a content conflict"));
        check(gitOutput(cleanupRepo.path(), {"branch", "--list", cleanBranch})
                  .isEmpty(),
              QStringLiteral("\"Merge & clean up\" deletes the merged branch"));
        check(!window.testHasAgentSession(13040),
              QStringLiteral("\"Merge & clean up\" deletes the agent entry that "
                             "produced the merged branch"));
        check(!window.testHasAgentSession(13041),
              QStringLiteral("\"Merge & clean up\" deletes the branch's "
                             "provenance-only Agent record too"));
        check(!window.testHasAgentSession(13043),
              QStringLiteral("\"Merge & clean up\" also clears the session that was "
                             "stuck on \"Running\" for the merged branch"));
        check(window.testHasAgentSession(13042),
              QStringLiteral("\"Merge & clean up\" keeps agent entries for other "
                             "branches"));
        check(window.testAgentStatusCellText(13040).isEmpty(),
              QString("the cleaned-up agent session leaves no row behind "
                      "(cell = %1)")
                  .arg(window.testAgentStatusCellText(13040)));
    }

    stopChildProcesses(window);
    return failures == 0 ? 0 : 1;
}
