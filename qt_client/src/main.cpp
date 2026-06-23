#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QIcon>
#include <QMessageBox>
#include <QStyleFactory>
#include <QStyleHints>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <unistd.h>
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");
    // App icon: the cube cropped out of the ForkMesh logo.
    app.setWindowIcon(QIcon(QStringLiteral(":/app/forkmesh.png")));
    app.setStyle(QStyleFactory::create("Fusion"));

    // Refuse to run as root: ForkMesh runs git, action workflows and shell
    // steps, so running privileged is dangerous and unnecessary. This is a hard
    // refusal with no opt-out — including under sudo, where the real uid is 0
    // even though the effective uid may have been dropped.
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (geteuid() == 0 || getuid() == 0) {
        QMessageBox::critical(
            nullptr, "ForkMesh",
            "ForkMesh must not be run as root. It runs git and workflow commands "
            "with your privileges, so running as root is unsafe.\n\n"
            "Start it again as your normal user.");
        return 1;
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
    window.show();
    qInfo().noquote() << QStringLiteral("[startup +%1ms] window shown; entering event loop")
                             .arg(startup.elapsed(), 5);
    return app.exec();
}
