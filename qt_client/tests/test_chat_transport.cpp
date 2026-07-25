#include "MainnodeRoom.h"
#include "ServerNode.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstdio>
#include <functional>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

class WebSocketHarness
{
public:
    WebSocketHarness()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                QTcpSocket *socket = server.nextPendingConnection();
                ++connectionCount;
                sockets.append(socket);
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QObject::deleteLater);
                // The first connection deliberately blackholes the HTTP
                // upgrade. Every retry is accepted immediately.
                if (connectionCount == 1)
                    continue;
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket] { acceptHandshake(socket); });
            }
        });
    }

    bool listen()
    {
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1%2")
                        .arg(server.serverPort())
                        .arg(forkmesh::mainnode::kRoomPath));
    }

    void sendAbnormalClose()
    {
        if (upgraded)
            upgraded->abort();
    }

    QTcpServer server;
    int connectionCount = 0;
    QList<QTcpSocket *> sockets;
    QTcpSocket *upgraded = nullptr;
    QList<QByteArray> requests;

private:
    void acceptHandshake(QTcpSocket *socket)
    {
        const QByteArray request = socket->readAll();
        if (!request.contains("\r\n\r\n"))
            return;
        requests.append(request);
        const QByteArray marker = "Sec-WebSocket-Key:";
        const int keyStart = request.indexOf(marker);
        if (keyStart < 0)
            return;
        const int valueStart = keyStart + marker.size();
        const int lineEnd = request.indexOf("\r\n", valueStart);
        const QByteArray key = request.mid(valueStart, lineEnd - valueStart).trimmed();
        const QByteArray accept = QCryptographicHash::hash(
            key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
            QCryptographicHash::Sha1).toBase64();
        socket->write(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n");
        socket->flush();
        upgraded = socket;
    }
};

bool connected(const ServerNode &node)
{
    const QList<QJsonObject> rows = node.networkDiagnostics();
    return !rows.isEmpty() && rows.first().value("connected").toBool();
}

qint64 backpressureDrops(const ServerNode &node)
{
    const QList<QJsonObject> rows = node.networkDiagnostics();
    return rows.isEmpty()
               ? -1
               : qint64(rows.first().value("backpressureDrops").toDouble());
}

void testSavedRoomMigration()
{
    QString url =
        QStringLiteral("wss://forkmesh.com") +
        forkmesh::mainnode::kLegacyRoomPath;
    QString room = forkmesh::mainnode::kLegacyDefaultRoomName;
    check(forkmesh::mainnode::migrateSavedDefaultRoom(&url, &room),
          "legacy shared-room settings are migrated");
    check(url == forkmesh::mainnode::kDefaultServerUrl,
          "legacy canonical URL moves to world-general");
    check(room == forkmesh::mainnode::kDefaultRoomName,
          "legacy room name moves to world-general");

    url = QStringLiteral("wss://relay.example.test:9443") +
          forkmesh::mainnode::kLegacyRoomPath +
          QStringLiteral("?instance=community");
    room = QStringLiteral("general");
    check(forkmesh::mainnode::migrateSavedDefaultRoom(&url, &room),
          "independent relay using the standard retired path is migrated");
    check(url.startsWith(QStringLiteral("wss://relay.example.test:9443")) &&
              url.contains(forkmesh::mainnode::kRoomPath) &&
              url.endsWith(QStringLiteral("?instance=community")),
          "migration preserves custom host, port, and query");

    const QString privateUrl =
        QStringLiteral("wss://relay.example.test/api/repo/alice/secret/"
                       "rooms/general/ws");
    url = privateUrl;
    room = QStringLiteral("general");
    check(!forkmesh::mainnode::migrateSavedDefaultRoom(&url, &room),
          "private repository room is not treated as the shared default");
    check(url == privateUrl && room == QStringLiteral("general"),
          "private repository settings remain byte-for-byte unchanged");

    const QString customRoomUrl =
        QStringLiteral("wss://forkmesh.com") +
        forkmesh::mainnode::kLegacyRoomPath;
    url = customRoomUrl;
    room = QStringLiteral("team-private");
    check(!forkmesh::mainnode::migrateSavedDefaultRoom(&url, &room),
          "custom room metadata is preserved even on the old path");
    check(url == customRoomUrl && room == QStringLiteral("team-private"),
          "custom room URL and name remain unchanged");
}

void testDeterministicRecovery()
{
    WebSocketHarness harness;
    check(harness.listen(), "local WebSocket harness listens");
    if (!harness.server.isListening())
        return;

    ServerNode node(QStringLiteral("Alice"), QStringLiteral("alice-laptop"),
                    QStringLiteral("alice"), QStringLiteral("stable-node-id"),
                    harness.url(), forkmesh::mainnode::kDefaultRoomName,
                    QString(), QStringLiteral("test-room-passphrase"));
    node.setTransportLimitsForTests(60, 20, 2048);
    node.setNetworkAvailable(false);
    check(node.start(), "ServerNode starts against the test endpoint");
    waitUntil([] { return false; }, 100);
    check(harness.connectionCount == 0,
          "startup does not dial while the operating system is offline");
    node.setNetworkAvailable(true);

    check(waitUntil([&] {
              return harness.connectionCount == 2 && connected(node);
          }, 1200),
          "a blackholed CONNECTING attempt times out and reconnects");
    check(!harness.requests.isEmpty() &&
              harness.requests.last().startsWith(
                  "GET " + forkmesh::mainnode::kRoomPath.toUtf8() + " "),
          "Qt requests the world-general WebSocket endpoint");
    waitUntil([] { return false; }, 120);
    check(harness.connectionCount == 2,
          "one stalled attempt produces exactly one successful reconnect");

    const qint64 dropsBefore = backpressureDrops(node);
    node.sendChat(QStringLiteral("#general"), QString(16000, QLatin1Char('x')));
    check(waitUntil([&] {
              return backpressureDrops(node) > dropsBefore;
          }, 200),
          "oversized pending send is dropped by the socket backpressure budget");

    harness.sendAbnormalClose();
    check(waitUntil([&] {
              return harness.connectionCount == 3 && connected(node);
          }, 800),
          "an abnormal WebSocket close reconnects");
    waitUntil([] { return false; }, 120);
    check(harness.connectionCount == 3,
          "an abnormal close schedules exactly one reconnect");

    node.setNetworkAvailable(false);
    check(waitUntil([&] { return !connected(node); }, 200),
          "network loss tears down the live socket");
    const int offlineConnections = harness.connectionCount;
    waitUntil([] { return false; }, 120);
    check(harness.connectionCount == offlineConnections,
          "no reconnect is attempted while the network is unavailable");

    node.setNetworkAvailable(true);
    check(waitUntil([&] {
              return harness.connectionCount == offlineConnections + 1 &&
                     connected(node);
          }, 500),
          "network restoration performs one immediate reconnect");
    waitUntil([] { return false; }, 120);
    check(harness.connectionCount == offlineConnections + 1,
          "network restoration does not create duplicate connections");

    node.shutdown();
    const int stoppedConnections = harness.connectionCount;
    waitUntil([] { return false; }, 120);
    check(harness.connectionCount == stoppedConnections,
          "intentional shutdown suppresses reconnects");
}

} // namespace

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ForkMeshTests"));
    app.setApplicationName(QStringLiteral("chat-transport"));

    testSavedRoomMigration();
    testDeterministicRecovery();
    if (failures == 0)
        std::puts("All chat transport tests passed.");
    return failures == 0 ? 0 : 1;
}
