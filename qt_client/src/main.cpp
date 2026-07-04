#include "CrashHandler.h"
#include "HeadlessConsole.h"
#include "MainWindow.h"
#include "PlatformLogFilter.h"
#include "ServerNode.h"
#include "SingleInstance.h"
#include "Theme.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QMetaObject>
#include <QElapsedTimer>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleHints>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <unistd.h>
#endif

namespace {

// Headless = no GUI available / wanted. Triggered explicitly with --headless or
// --cli, or auto-detected on Linux when there's no display server to connect to
// (so installing on a server / VM "just works" instead of aborting on the xcb
// plugin). We force the always-present "offscreen" Qt platform so QApplication
// still constructs — the whole backend (mesh, repo serving, mirror sync) runs in
// the event loop regardless of a display, and a stdin REPL drives it.
bool detectHeadless(const QStringList &args)
{
    if (args.contains(QStringLiteral("--headless")) ||
        args.contains(QStringLiteral("--cli")))
        return true;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // Honour an explicit platform choice (e.g. QT_QPA_PLATFORM=offscreen in CI).
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        return qgetenv("QT_QPA_PLATFORM").startsWith("offscreen") ||
               qgetenv("QT_QPA_PLATFORM").startsWith("minimal");
    if (!qEnvironmentVariableIsSet("DISPLAY") &&
        !qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        return true;
#endif
    return false;
}

// QApplication whose notify() wraps every event delivery in a try/catch. A C++
// exception thrown out of a slot invoked by the event loop — e.g. a handler for
// a tab click / currentChanged — is undefined behaviour in Qt6 and typically
// takes the whole app down with nothing logged ("it crashes when I click a
// tab"). Catching it here turns that silent crash into a logged fault (main log
// + durable crash log via forkmesh::logCaughtFault) and keeps the app running
// instead of dying. Signals (SIGSEGV etc.) still go through the CrashHandler.
class ForkMeshApplication : public QApplication
{
public:
    using QApplication::QApplication;

    bool notify(QObject *receiver, QEvent *event) override
    {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception &e) {
            forkmesh::logCaughtFault(describe(receiver, event),
                                     QString::fromUtf8(e.what()));
        } catch (...) {
            forkmesh::logCaughtFault(describe(receiver, event),
                                     QStringLiteral("(non-std exception)"));
        }
        // Swallow the fault: returning to the event loop keeps the window alive
        // rather than letting the exception unwind through Qt's C event loop.
        return false;
    }

private:
    static QString describe(QObject *receiver, QEvent *event)
    {
        const QString cls = receiver && receiver->metaObject()
                                ? QString::fromLatin1(receiver->metaObject()->className())
                                : QStringLiteral("(null)");
        const QString name = receiver ? receiver->objectName() : QString();
        const int type = event ? int(event->type()) : -1;
        return QStringLiteral("event delivery to %1%2 (event type %3)")
            .arg(cls,
                 name.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(name))
            .arg(type);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    // First thing, before anything can fault: install the crash handlers so an
    // unexpected exit/crash leaves a backtrace in ~/.forkmesh/diagnostics/
    // crashes.log ("sometimes the app exits / crashes" with nothing to explain
    // why). The durable file is also what the opt-in telemetry upload sends on
    // the next startup (issue #354). Cheap; opens one fd plus a couple buffers.
    forkmesh::installCrashHandler(
        QDir::homePath() + QStringLiteral("/.forkmesh/diagnostics/crashes.log"));

    // Collect args before QApplication so headless/root flags are visible while we
    // still control the Qt platform plugin selection.
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);

    const bool headless = detectHeadless(rawArgs);
    const bool allowRoot = rawArgs.contains(QStringLiteral("--allow-root")) ||
                           qEnvironmentVariableIsSet("FORKMESH_ALLOW_ROOT");

    // Under headless, run on the offscreen platform so QApplication initialises
    // without a display (unless the user already pinned a platform). This is what
    // turns the "could not connect to display / xcb plugin" abort into a working
    // CLI node.
    if (headless && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Headless always runs on offscreen/minimal, whose propagateSizeHints()
    // warning would otherwise spam the `forkmesh>` console. Install the filter
    // only here so the desktop GUI keeps Qt's untouched default logging.
    if (headless)
        forkmesh::installPlatformLogFilter();

    // The embedded terminal (TerminalWidget) renders itself from a forkpty PTY —
    // no xterm, no X11 reparenting — so it works the same on X11 and Wayland and
    // we no longer force the xcb backend. Forcing xcb pushed Wayland sessions onto
    // XWayland, which maps a black frame before the first paint (a jarring black
    // screen on launch). Letting Qt pick the native platform shows the themed
    // window immediately.
    ForkMeshApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");
    // App icon: the cube cropped out of the ForkMesh logo.
    app.setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    // On Wayland (the GNOME default) the dock/taskbar icon is NOT taken from
    // setWindowIcon — the compositor matches the window's app-id to an installed
    // .desktop file. This name must equal the basename of the desktop entry that
    // install.sh writes (forkmesh.desktop) for GNOME to show our logo and let it
    // be pinned. Harmless on X11/macOS/Windows.
    QGuiApplication::setDesktopFileName(QStringLiteral("forkmesh"));
    app.setStyle(QStyleFactory::create("Fusion"));

    // Refuse to run a second instance for this user. This is the actual fix for
    // "a new ForkMesh window opens seemingly at random": every trigger for that
    // (a desktop session restore, a login-autostart entry racing a manually
    // opened window, a double-click landing while a slow cold start is still
    // loading) used to hand back a brand new, fully independent process — its
    // own mesh backend and repo-hosting server reading/writing the same
    // ~/.forkmesh data out from under the first one. Now a duplicate launch
    // just raises the existing window and exits.
    if (!forkmesh::acquireSingleInstance()) {
        qInfo().noquote()
            << "ForkMesh is already running; focusing the existing window.";
        return 0;
    }

    // Bundle a colour-emoji font so 🎉/🙊/✅ paint in full colour in chat
    // messages (and everywhere else) even on systems that ship no colour-emoji
    // font of their own (common on Linux). Registering it and appending it to
    // the application font's fallback family list makes Qt render colour glyphs
    // for any emoji codepoint the primary UI family is missing, rather than the
    // flat black-and-white boxes seen without it.
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoColorEmoji.ttf"));
    {
        QFont base = app.font();

        // Prefer a modern, consistent UI family over the raw platform default
        // (which on many Linux boxes is a dated bitmap-ish sans). We don't bundle
        // a font — that would bloat the binary — so we just pick the first family
        // from a prioritised list that the system actually has installed, and fall
        // back to whatever Qt chose otherwise. This is a no-op on systems that ship
        // none of them, so nothing regresses; where one is present (Segoe UI on
        // Windows, SF Pro on macOS, Inter/Roboto/Noto on Linux) the whole app gets
        // a cleaner, more uniform look.
        const QStringList installed = QFontDatabase::families();
        for (const QString &preferred : {QStringLiteral("Inter"),
                                         QStringLiteral("SF Pro Text"),
                                         QStringLiteral("Segoe UI"),
                                         QStringLiteral("Roboto"),
                                         QStringLiteral("Noto Sans"),
                                         QStringLiteral("Ubuntu"),
                                         QStringLiteral("Cantarell")}) {
            if (installed.contains(preferred)) {
                base.setFamily(preferred);
                break;
            }
        }

        // Keep the chosen UI family first, then append colour-emoji fallbacks so
        // any codepoint the primary family is missing still paints in full colour.
        QStringList families{base.family()};
        for (const QString &emoji : {QStringLiteral("Noto Color Emoji"),
                                     QStringLiteral("Apple Color Emoji"),
                                     QStringLiteral("Segoe UI Emoji")}) {
            if (!families.contains(emoji))
                families << emoji;
        }
        base.setFamilies(families);
        app.setFont(base);
    }

    // Host-stats reporting (CPU/RAM/disk in the Mirror nodes view) is off by
    // default on the desktop but on for headless installs done from the Hosts
    // tab, so an operator can monitor the servers they provisioned. Seed the
    // toggles on the first headless launch; once set, the value persists and a
    // later operator edit is never overwritten (adhoc #23).
    if (headless) {
        QSettings settings;
        for (const QString &key : {TelemetrySettings::kReportCpu,
                                   TelemetrySettings::kReportMemory,
                                   TelemetrySettings::kReportDisk}) {
            if (!settings.contains(key))
                settings.setValue(key, true);
        }
        // Auto-update (kAutoUpdateSetting, MainWindowInternal.h) is likewise on by
        // default for a headless install — no one is around to click "Update,
        // rebuild & restart" on an operator-run VM — but off by default on
        // desktop; seed once, same as the telemetry toggles above.
        if (!settings.contains(QStringLiteral("update/autoUpdate")))
            settings.setValue(QStringLiteral("update/autoUpdate"), true);
    }

    // Refuse to run as root: ForkMesh runs git, action workflows and shell
    // steps, so running privileged is dangerous and unnecessary. This is a hard
    // refusal with no opt-out — including under sudo, where the real uid is 0
    // even though the effective uid may have been dropped. Headless can opt out
    // with --allow-root / FORKMESH_ALLOW_ROOT (e.g. a single-purpose VM), and
    // reports to stderr rather than a dialog nobody would see.
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (geteuid() == 0 || getuid() == 0) {
        const char *refusal =
            "ForkMesh must not be run as root. It runs git and workflow commands "
            "with your privileges, so running as root is unsafe.\n"
            "Start it again as your normal user.";
        if (headless) {
            if (!allowRoot) {
                qCritical().noquote()
                    << refusal
                    << "\n(Pass --allow-root or set FORKMESH_ALLOW_ROOT=1 to "
                       "override on a dedicated VM.)";
                return 1;
            }
            qWarning().noquote()
                << "ForkMesh: running as root (--allow-root). git and workflow "
                   "steps will run with root privileges.";
        } else {
            QMessageBox::critical(
                nullptr, "ForkMesh",
                "ForkMesh must not be run as root. It runs git and workflow "
                "commands with your privileges, so running as root is unsafe.\n\n"
                "Start it again as your normal user.");
            return 1;
        }
    }
#endif

    // Apply the saved theme (system/dark/light); when set to "system", follow
    // the OS color scheme and switch live as it changes.
    MainWindow::applyTheme();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [](Qt::ColorScheme) { MainWindow::applyTheme(); });
#endif

    // Detailed startup timing to the terminal so a slow launch is diagnosable.
    QElapsedTimer startup;
    startup.start();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] constructing MainWindow")
                             .arg(startup.elapsed(), 5);
    MainWindow window;
    // Tell the window it's running without a GUI so its auto-start path can
    // register a fresh mirror's account non-interactively (the desktop pops a
    // "Join ForkMesh" dialog for that, which a headless VM cannot click).
    window.setHeadlessMode(headless);
    qInfo().noquote() << QStringLiteral("[startup +%1ms] MainWindow constructed")
                             .arg(startup.elapsed(), 5);
    // A later launch attempt bounces off acquireSingleInstance() above and
    // pings us instead; raise and focus our window in response. Under headless
    // (offscreen platform) there is no window to focus — raise()/activateWindow()
    // are no-ops there anyway, but the offscreen plugin logs "This plugin does
    // not support raise()" on every call, which makes a successful re-run of a
    // headless install (bouncing off an already-running node) look like an
    // error in the SSH install log. Skip the no-op calls entirely headless.
    forkmesh::onSingleInstanceActivation([&window, headless] {
        if (headless)
            return;
        window.setWindowState((window.windowState() & ~Qt::WindowMinimized) |
                              Qt::WindowActive);
        window.show();
        window.raise();
        window.activateWindow();
    });
    // show() works under the offscreen platform too (rendering to an offscreen
    // surface) and drives the same deferred-startup path — including the
    // headless/offscreen safety net in MainWindow::showEvent — so auto-restore and
    // auto-connect behave identically headless and on the desktop.
    window.show();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] window shown; entering event loop")
                             .arg(startup.elapsed(), 5);

    // In headless mode, the node runs the full backend but there's no GUI to
    // interact with, so attach a stdin REPL to observe and drive it.
    HeadlessConsole *console = nullptr;
    if (headless)
        console = new HeadlessConsole(&window, &app, &app);
    Q_UNUSED(console);

    return app.exec();
}
