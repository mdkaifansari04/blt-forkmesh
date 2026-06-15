#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ForkMesh");
    app.setOrganizationName("ForkMesh");
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(Theme::kStyleSheet);

    MainWindow window;
    window.show();
    return app.exec();
}
