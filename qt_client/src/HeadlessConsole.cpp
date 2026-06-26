#include "HeadlessConsole.h"

#include "ChatBackend.h"
#include "MainWindow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QSocketNotifier>
#include <QStringList>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

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

#if defined(Q_OS_UNIX)
    m_stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_stdin, &QSocketNotifier::activated, this,
            &HeadlessConsole::onStdinActivated);
    prompt();
#else
    m_out << "Interactive input is unavailable on this platform; running as a "
             "log-streaming daemon."
          << Qt::endl;
    m_out.flush();
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
             "  status              node, account, connection, peers, repos\n"
             "  peers               list known nodes in the mesh\n"
             "  repos               list local repositories\n"
             "  mirrors             list repos this node mirrors / serves\n"
             "  sync                sync mirrors + poll owned inboxes now\n"
             "  setup <name> [sol]  pick a node name and connect (first run)\n"
             "  connect <name>      alias for setup\n"
             "  log on|off          toggle the live event feed\n"
             "  help                this help\n"
             "  quit | exit         shut down the node\n";
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
    } else if (cmd == QLatin1String("setup") || cmd == QLatin1String("connect")) {
        if (args.isEmpty()) {
            m_out << "usage: setup <node-name> [solana-address]" << Qt::endl;
        } else {
            m_window->headlessStart(args.value(0), args.value(1));
            m_out << "connecting as '" << args.value(0).toLower() << "'…"
                  << Qt::endl;
        }
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
        m_out << "shutting down…" << Qt::endl;
        m_out.flush();
        m_window->close();
        m_app->quit();
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
        // EOF (Ctrl-D) or read error: shut the node down cleanly.
        if (m_stdin)
            m_stdin->setEnabled(false);
        m_out << Qt::endl << "stdin closed; shutting down…" << Qt::endl;
        m_out.flush();
        m_window->close();
        m_app->quit();
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
