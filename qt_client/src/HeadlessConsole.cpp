#include "HeadlessConsole.h"

#include "ChatBackend.h"
#include "MainWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QStringList>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

int HeadlessConsole::s_signalFd[2] = {-1, -1};

HeadlessConsole::HeadlessConsole(MainWindow *window, QCoreApplication *app,
                                 QObject *parent)
    : QObject(parent), m_window(window), m_app(app), m_out(stdout)
{
    printBanner();

    // Re-wire the live event feed whenever a backend is (re)created — e.g. after
    // the `setup` command connects, or on an auto-reconnect.
    connect(window, &MainWindow::backendAttached, this,
            [this](ChatBackend *backend) { attachFeed(backend); });
    if (ChatBackend *backend = window->currentBackend())
        attachFeed(backend);

    // A durable daemon must stop only on an explicit signal, never on a stray
    // terminal hang-up. Catch SIGINT/SIGTERM for a clean shutdown.
    installSignalHandlers();

#if defined(Q_OS_UNIX)
    m_stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_stdin, &QSocketNotifier::activated, this,
            &HeadlessConsole::onStdinActivated);
    prompt();
#else
    m_out << "Interactive input is unavailable on this platform; running as a "
             "log-streaming daemon (stop with SIGINT/SIGTERM)."
          << Qt::endl;
    m_out.flush();
#endif
}

void HeadlessConsole::unixSignalHandler(int sig)
{
#if defined(Q_OS_UNIX)
    // Async-signal-safe: only poke the self-pipe; the real work runs in onSignal.
    const char byte = static_cast<char>(sig);
    const ssize_t n = ::write(s_signalFd[0], &byte, 1);
    (void)n;
#else
    (void)sig;
#endif
}

void HeadlessConsole::installSignalHandlers()
{
#if defined(Q_OS_UNIX)
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, s_signalFd) != 0)
        return;
    m_signalNotifier =
        new QSocketNotifier(s_signalFd[1], QSocketNotifier::Read, this);
    connect(m_signalNotifier, &QSocketNotifier::activated, this,
            &HeadlessConsole::onSignal);

    struct sigaction sa = {};
    sa.sa_handler = &HeadlessConsole::unixSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
#endif
}

void HeadlessConsole::printBanner()
{
    m_out << "ForkMesh headless node — type 'help' for commands, 'quit' to exit."
          << Qt::endl;
    printLines(m_window->headlessStatusLines());
}

void HeadlessConsole::printHelp()
{
    m_out << "Commands:\n"
             "  status              node, account, connection, peers, repos,\n"
             "                      pulls/discussions/branches/commits, load\n"
             "  peers               list known nodes in the mesh\n"
             "  repos               list local repositories\n"
             "  mirrors             repos this node mirrors / serves + cpu & memory\n"
             "  sync                sync mirrors + poll owned inboxes now\n"
             "  e2e                 run the end-to-end mesh-loop self-test\n"
             "                      (publish->browse->clone->issue->agent PR->merge)\n"
             "  setup <name> [sol]  pick a node name and connect (first run)\n"
             "  connect <name>      alias for setup\n"
             "  update              update to the latest version, rebuild & restart\n"
             "  daemon              detach this prompt and keep running in background\n"
             "  log on|off          toggle the live event feed\n"
             "  help                this help\n"
             "  quit | exit         shut down the node\n"
             "\n"
             "Durable daemon: the node keeps running until you `quit`/`exit` or send\n"
             "SIGINT/SIGTERM (Ctrl-C, kill, systemctl stop). Closing stdin does NOT\n"
             "stop it — run it detached with `forkmesh --headless </dev/null &`, nohup\n"
             "or a systemd service and it stays up. The `daemon` command does the same\n"
             "on demand: it releases the prompt so you can exit the shell while the\n"
             "node keeps serving.\n";
    m_out.flush();
}

void HeadlessConsole::prompt()
{
    m_out << "forkmesh> ";
    m_out.flush();
}

void HeadlessConsole::printLines(const QStringList &lines)
{
    for (const QString &line : lines)
        m_out << line << Qt::endl;
    m_out.flush();
}

void HeadlessConsole::logEvent(const QString &text)
{
    if (!m_echoEvents)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    // Leading '\r' so the line overwrites the (un-newlined) prompt, then redraw it.
    m_out << '\r' << '[' << ts << "] " << text << Qt::endl;
    prompt();
}

void HeadlessConsole::attachFeed(ChatBackend *backend)
{
    if (!backend)
        return;
    connect(backend, &ChatBackend::systemMessage, this,
            [this](const QString &t) { logEvent(t); });
    connect(backend, &ChatBackend::statusChanged, this,
            [this](const QString &s) { logEvent(QStringLiteral("status: ") + s); });
    connect(backend, &ChatBackend::rosterChanged, this,
            [this](const QList<MemberInfo> &members) {
                int online = 0;
                for (const MemberInfo &p : members)
                    if (p.online)
                        ++online;
                logEvent(QStringLiteral("roster: %1 online / %2 known")
                             .arg(online)
                             .arg(members.size()));
            });
    connect(backend, &ChatBackend::mirrorUpdated, this,
            [this](const QString &owner, const QString &peer) {
                logEvent(QStringLiteral("mirror updated: %1 (by %2)")
                             .arg(owner, peer));
            });
    connect(backend, &ChatBackend::fatalError, this,
            [this](const QString &m) { logEvent(QStringLiteral("ERROR: ") + m); });
}

void HeadlessConsole::dispatch(const QString &raw)
{
    const QString line = raw.trimmed();
    if (line.isEmpty()) {
        prompt();
        return;
    }
    const QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
    const QString cmd = parts.first().toLower();
    const QStringList args = parts.mid(1);

    if (cmd == QLatin1String("help") || cmd == QLatin1String("?")) {
        printHelp();
    } else if (cmd == QLatin1String("status")) {
        printLines(m_window->headlessStatusLines());
    } else if (cmd == QLatin1String("peers")) {
        printLines(m_window->headlessRosterLines());
    } else if (cmd == QLatin1String("repos")) {
        printLines(m_window->headlessRepoLines());
    } else if (cmd == QLatin1String("mirrors")) {
        printLines(m_window->headlessMirrorLines());
    } else if (cmd == QLatin1String("sync")) {
        m_window->headlessSyncNow();
        m_out << "sync triggered" << Qt::endl;
    } else if (cmd == QLatin1String("e2e")) {
        runMeshLoopSelfTest();
    } else if (cmd == QLatin1String("setup") || cmd == QLatin1String("connect")) {
        if (args.isEmpty()) {
            m_out << "usage: setup <node-name> [solana-address]" << Qt::endl;
        } else {
            m_window->headlessStart(args.value(0), args.value(1));
            m_out << "connecting as '" << args.value(0).toLower() << "'…"
                  << Qt::endl;
        }
    } else if (cmd == QLatin1String("update")) {
        m_out << "Updating ForkMesh from the live install mirror, then rebuilding\n"
                 "and restarting. Progress is logged below; the node relaunches\n"
                 "itself (headless) once the rebuild finishes."
              << Qt::endl;
        m_out.flush();
        m_window->headlessUpdateRestart();
    } else if (cmd == QLatin1String("daemon")) {
        // Go into daemon mode on demand: release the interactive prompt so the
        // operator can exit the shell (or close the SSH session) while the backend
        // keeps serving. Same end state as losing stdin, but explicit. The node
        // still stops on `quit` (no longer reachable from here), SIGINT or SIGTERM.
        detachStdin();
        m_out << "Entering daemon mode: the node keeps running in the background.\n"
                 "You can exit this shell now (Ctrl-D / close the terminal) without\n"
                 "stopping it. Stop it later with SIGINT/SIGTERM (kill, systemctl stop)."
              << Qt::endl;
        m_out.flush();
        return;
    } else if (cmd == QLatin1String("log")) {
        const QString v = args.value(0).toLower();
        if (v == QLatin1String("on")) {
            m_echoEvents = true;
            m_out << "event log on" << Qt::endl;
        } else if (v == QLatin1String("off")) {
            m_echoEvents = false;
            m_out << "event log off" << Qt::endl;
        } else {
            m_out << "usage: log on|off  (currently "
                  << (m_echoEvents ? "on" : "off") << ")" << Qt::endl;
        }
    } else if (cmd == QLatin1String("quit") || cmd == QLatin1String("exit")) {
        shutdown(QStringLiteral("shutting down…"));
        return;
    } else {
        m_out << "unknown command: " << cmd << "  (type 'help')" << Qt::endl;
    }
    prompt();
}

void HeadlessConsole::onStdinActivated()
{
#if defined(Q_OS_UNIX)
    char buf[4096];
    const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
        // EOF (Ctrl-D) or a closed input pipe — e.g. launched with `</dev/null`
        // by a service manager. A durable daemon must NOT die just because its
        // controlling input went away: detach the prompt and keep serving. The
        // node still stops on `quit`, SIGINT or SIGTERM.
        detachStdin();
        m_out << Qt::endl
              << "stdin closed; continuing to run as a background daemon "
                 "(stop with Ctrl-C / kill / systemctl stop)."
              << Qt::endl;
        m_out.flush();
        return;
    }
    m_inBuf.append(buf, int(n));
    int nl;
    while ((nl = m_inBuf.indexOf('\n')) >= 0) {
        const QString cmdLine = QString::fromUtf8(m_inBuf.left(nl));
        m_inBuf.remove(0, nl + 1);
        dispatch(cmdLine);
    }
#endif
}

void HeadlessConsole::detachStdin()
{
    // A closed fd is permanently "ready", so leaving the notifier enabled would
    // spin the event loop. Disable and drop it; the daemon runs on without it.
    if (m_stdin) {
        m_stdin->setEnabled(false);
        m_stdin->deleteLater();
        m_stdin = nullptr;
    }
}

void HeadlessConsole::onSignal()
{
#if defined(Q_OS_UNIX)
    if (m_signalNotifier)
        m_signalNotifier->setEnabled(false);
    char sig = 0;
    const ssize_t n = ::read(s_signalFd[1], &sig, 1);
    (void)n;
    if (m_signalNotifier)
        m_signalNotifier->setEnabled(true);
    shutdown(QStringLiteral("received signal %1; shutting down…")
                 .arg(int(static_cast<unsigned char>(sig))));
#endif
}

void HeadlessConsole::runMeshLoopSelfTest()
{
    // The full loop lives in the `forkmesh-e2e` self-test binary (built by
    // `cmake --build --target check`), which drives publish -> browse -> clone
    // -> issue -> agent PR -> merge against an in-process relay stub with a stub
    // agent. Running it as a subprocess keeps the test harness (and its stub
    // relay) out of the shipping app while still letting an operator kick the
    // whole loop from a headless node — the nightly reliability check (#352).
    const QString dir = QCoreApplication::applicationDirPath();
    const QString runner = QDir(dir).filePath(QStringLiteral("forkmesh-e2e"));
    const QString stubAgent =
        QDir(dir).filePath(QStringLiteral("forkmesh-e2e-stub-agent"));
    if (!QFileInfo::exists(runner)) {
        m_out << "e2e self-test binary not found next to this build ("
              << runner << ").\n"
              << "Build it with: cmake --build <build> --target check" << Qt::endl;
        m_out.flush();
        return;
    }

    m_out << "Running end-to-end mesh-loop self-test…" << Qt::endl;
    m_out.flush();

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (QFileInfo::exists(stubAgent))
        env.insert(QStringLiteral("FORKMESH_E2E_STUB_AGENT"), stubAgent);
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&proc, &QProcess::readyReadStandardOutput, this, [this, &proc] {
        m_out << QString::fromUtf8(proc.readAllStandardOutput());
        m_out.flush();
    });
    // Keep the node's event loop live (it keeps serving) while the self-test
    // runs, instead of blocking on waitForFinished.
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            &loop, &QEventLoop::quit);
    proc.start(runner, QStringList{});
    if (!proc.waitForStarted(5000)) {
        m_out << "e2e self-test failed to start." << Qt::endl;
        m_out.flush();
        return;
    }
    loop.exec();
    m_out << QString::fromUtf8(proc.readAllStandardOutput());
    m_out << (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0
                  ? "e2e self-test PASSED."
                  : "e2e self-test FAILED.")
          << Qt::endl;
    m_out.flush();
}

void HeadlessConsole::shutdown(const QString &reason)
{
    detachStdin();
    m_out << Qt::endl << reason << Qt::endl;
    m_out.flush();
    m_window->close();
    m_app->quit();
}
