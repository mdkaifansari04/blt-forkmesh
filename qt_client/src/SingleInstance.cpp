#include "SingleInstance.h"

#include <QDebug>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

namespace forkmesh {

namespace {

// A fixed, unqualified name: QLocalServer already scopes the underlying
// socket/pipe per user (a per-user temp dir on Unix, a per-session namespace on
// Windows), so two different users on the same box never collide.
const char kServerName[] = "forkmesh-single-instance";

QLockFile *g_lockFile = nullptr;
QLocalServer *g_server = nullptr;
std::function<void()> g_activationHandler;

} // namespace

bool acquireSingleInstance()
{
    const QString dir = QDir::homePath() + QStringLiteral("/.forkmesh");
    QDir().mkpath(dir);

    g_lockFile = new QLockFile(dir + QStringLiteral("/forkmesh.lock"));
    // Default QLockFile behaviour already does the right thing here: staleness
    // is decided by checking whether the PID recorded in the lock file is still
    // alive (the lock directory is always local, so that check is reliable),
    // not by a fixed age — so a primary instance open for days still correctly
    // blocks a second launch, while one that crashed is recognised and cleared
    // immediately.
    if (!g_lockFile->tryLock()) {
        // Another live instance holds the lock. Ping it to raise its window
        // instead of silently doing nothing (or, worse, opening a second one).
        QLocalSocket socket;
        socket.connectToServer(QString::fromLatin1(kServerName));
        if (socket.waitForConnected(500)) {
            socket.write("activate");
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
        }
        delete g_lockFile;
        g_lockFile = nullptr;
        return false;
    }

    // We now hold the lock, so any prior server socket at this name belongs to
    // a dead process (QLockFile just proved that) — clear it before listening
    // so a leftover socket file can't make our own listen() fail.
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    g_server = new QLocalServer();
    g_server->setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(g_server, &QLocalServer::newConnection, g_server, [] {
        while (QLocalSocket *socket = g_server->nextPendingConnection()) {
            socket->waitForReadyRead(200);
            socket->deleteLater();
            if (g_activationHandler)
                g_activationHandler();
        }
    });
    if (!g_server->listen(QString::fromLatin1(kServerName))) {
        // Non-fatal: we still hold the instance lock (so a second launch can't
        // proceed), it just won't be able to raise our window when it bounces.
        qWarning().noquote() << "ForkMesh: single-instance activation socket"
                                 " unavailable:"
                              << g_server->errorString();
    }
    return true;
}

void onSingleInstanceActivation(std::function<void()> handler)
{
    g_activationHandler = std::move(handler);
}

void releaseSingleInstance()
{
    if (g_server) {
        g_server->close();
        g_server->deleteLater();
        g_server = nullptr;
    }
    if (g_lockFile) {
        g_lockFile->unlock();
        delete g_lockFile;
        g_lockFile = nullptr;
    }
}

} // namespace forkmesh
