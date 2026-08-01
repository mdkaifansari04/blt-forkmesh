#include "SingleInstance.h"

#include <QDebug>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

namespace forkmesh {

namespace {




const char kServerName[] = "forkmesh-single-instance";

QLockFile *g_lockFile = nullptr;
QLocalServer *g_server = nullptr;
std::function<void(const QString &)> g_activationHandler;

}

bool acquireSingleInstance(const QString &activationTarget)
{
    const QString dir = QDir::homePath() + QStringLiteral("/.forkmesh");
    QDir().mkpath(dir);

    g_lockFile = new QLockFile(dir + QStringLiteral("/forkmesh.lock"));






    if (!g_lockFile->tryLock()) {


        QLocalSocket socket;
        socket.connectToServer(QString::fromLatin1(kServerName));
        if (socket.waitForConnected(500)) {
            const QByteArray target = activationTarget.toUtf8();
            socket.write(target.isEmpty() ? QByteArray("activate")
                                          : QByteArray("activate\n") +
                                                target.left(128));
            socket.waitForBytesWritten(500);
            socket.disconnectFromServer();
        }
        delete g_lockFile;
        g_lockFile = nullptr;
        return false;
    }




    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    g_server = new QLocalServer();
    g_server->setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(g_server, &QLocalServer::newConnection, g_server, [] {
        while (QLocalSocket *socket = g_server->nextPendingConnection()) {
            socket->waitForReadyRead(200);
            const QByteArray message = socket->read(160);
            socket->deleteLater();
            if (g_activationHandler) {
                const QByteArray prefix("activate\n");
                const QString target =
                    message.startsWith(prefix)
                        ? QString::fromUtf8(message.mid(prefix.size()))
                        : QString();
                g_activationHandler(target);
            }
        }
    });
    if (!g_server->listen(QString::fromLatin1(kServerName))) {


        qWarning().noquote() << "ForkMesh: single-instance activation socket"
                                 " unavailable:"
                              << g_server->errorString();
    }
    return true;
}

void onSingleInstanceActivation(
    std::function<void(const QString &activationTarget)> handler)
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

}
