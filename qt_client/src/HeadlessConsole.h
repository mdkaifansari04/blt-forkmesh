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
//
// It is a durable daemon: it keeps running until explicitly stopped — by the
// `quit`/`exit` command or a SIGINT/SIGTERM (e.g. `kill`, `systemctl stop`,
// Ctrl-C). Losing stdin (EOF / `</dev/null` when launched by a service manager)
// does NOT stop the node; it just detaches the command prompt and the backend
// runs on (issue #287).
class HeadlessConsole : public QObject
{
    Q_OBJECT
public:
    HeadlessConsole(MainWindow *window, QCoreApplication *app,
                    QObject *parent = nullptr);

private slots:
    void onStdinActivated();
    void onSignal();

private:
    void printBanner();
    void printHelp();
    void prompt();
    void dispatch(const QString &line);
    void printLines(const QStringList &lines);
    void logEvent(const QString &text);
    void attachFeed(ChatBackend *backend);
    void detachStdin();          // stop reading stdin but keep the node running
    void shutdown(const QString &reason); // explicit stop: close + quit the loop
    void installSignalHandlers();
    // Run the nightly end-to-end mesh-loop self-test (issue #352): the sibling
    // `forkmesh-e2e` binary, streamed to this console. Used by the `e2e` command.
    void runMeshLoopSelfTest();

    MainWindow *m_window;
    QCoreApplication *m_app;
    QTextStream m_out;
    QSocketNotifier *m_stdin = nullptr;
    QSocketNotifier *m_signalNotifier = nullptr;
    QByteArray m_inBuf;
    bool m_echoEvents = true;

    // Self-pipe so an async SIGINT/SIGTERM handler can hand off to the event loop
    // (only async-signal-safe ::write happens in the handler).
    static int s_signalFd[2];
    static void unixSignalHandler(int sig);
};
