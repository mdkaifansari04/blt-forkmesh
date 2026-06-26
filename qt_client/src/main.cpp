#include "HeadlessConsole.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
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

} // namespace

int main(int argc, char *argv[])
{
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

    // The embedded terminal (TerminalWidget) renders itself from a forkpty PTY —
    // no xterm, no X11 reparenting — so it works the same on X11 and Wayland and
    // we no longer force the xcb backend. Forcing xcb pushed Wayland sessions onto
    // XWayland, which maps a black frame before the first paint (a jarring black
    // screen on launch). Letting Qt pick the native platform shows the themed
    // window immediately.
    QApplication app(argc, argv);
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
    qInfo().noquote() << QStringLiteral("[startup +%1ms] MainWindow constructed")
                             .arg(startup.elapsed(), 5);
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
