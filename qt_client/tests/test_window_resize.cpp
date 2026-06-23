#include "../src/MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include <algorithm>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition) {
        qInfo("PASS: %s", qPrintable(what));
    } else {
        qCritical("FAIL: %s", qPrintable(what));
        ++failures;
    }
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

} // namespace

int main(int argc, char *argv[])
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        qCritical("FAIL: could not create temporary settings directory");
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setOrganizationName("ForkMeshTests");
    app.setApplicationName("WindowResize");

    MainWindow window;
    window.show();
    QApplication::processEvents();

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

    return failures == 0 ? 0 : 1;
}
