#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
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
    app.setStyle(QStyleFactory::create("Fusion"));

    // Refuse to run as root: ForkMesh runs git, action workflows and shell
    // steps, so running privileged is dangerous and unnecessary. (Set
    // FORKMESH_ALLOW_ROOT=1 only if you really know what you are doing.)
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (geteuid() == 0 && qEnvironmentVariableIntValue("FORKMESH_ALLOW_ROOT") != 1) {
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

    MainWindow window;
    window.show();
    return app.exec();
}
