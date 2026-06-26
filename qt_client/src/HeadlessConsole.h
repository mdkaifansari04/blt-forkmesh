#pragma once

#include <QByteArray>
#include <QObject>
#include <QTextStream>

class MainWindow;
class ChatBackend;
class QCoreApplication;
class QSocketNotifier;

// Interactive stdin REPL for running ForkMesh on a headless server / VM. The full
// backend (mesh presence, repo serving, mirror sync, owned-inbox polling) runs in
// the Qt event loop via MainWindow exactly as it does on the desktop; this console
// only observes and drives it. Created by main() when launched with --headless (or
// when no display is detected). Unix-only stdin handling; on other platforms it
// degrades to a live event-streaming daemon with no command input.
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

    MainWindow *m_window;
    QCoreApplication *m_app;
    QTextStream m_out;
    QSocketNotifier *m_stdin = nullptr;
    QByteArray m_inBuf;
    bool m_echoEvents = true;
};
