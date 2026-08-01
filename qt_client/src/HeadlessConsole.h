#pragma once

#include <QByteArray>
#include <QObject>
#include <QTextStream>

class MainWindow;
class ChatBackend;
class QCoreApplication;
class QSocketNotifier;













class HeadlessConsole : public QObject
{
    Q_OBJECT
public:
    HeadlessConsole(MainWindow *window, QCoreApplication *app,
                    QObject *parent = nullptr);

private slots:
    void onStdinActivated();

private:
    void printBanner();
    void printHelp();
    void prompt();
    void dispatch(const QString &line);
    void printLines(const QStringList &lines);
    void logEvent(const QString &text);
    void attachFeed(ChatBackend *backend);
    void detachStdin();
    void shutdown(const QString &reason);


    void runMeshLoopSelfTest();

    MainWindow *m_window;
    QCoreApplication *m_app;
    QTextStream m_out;
    QSocketNotifier *m_stdin = nullptr;
    QByteArray m_inBuf;
    bool m_echoEvents = true;
};
