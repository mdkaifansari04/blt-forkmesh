#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QStyleFactory>
#include <QStyleHints>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");
    app.setStyle(QStyleFactory::create("Fusion"));

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
