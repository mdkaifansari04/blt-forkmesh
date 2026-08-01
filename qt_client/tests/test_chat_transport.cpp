#include "MainnodeRoom.h"
#include "OfficeChannelMirror.h"
#include "RoomCrypto.h"
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
    bool connectionSignalState = false;
    QObject::connect(&node, &ServerNode::connectionChanged, &node,
                     [&connectionSignalState](bool connectedNow) {
                         connectionSignalState = connectedNow;
                     });
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
    check(connectionSignalState,
          "transport users are notified when encrypted sending is ready");
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
    check(!connectionSignalState,
          "transport users are notified when encrypted sending stops");
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





void testOfficeChannelMirror()
{
    using namespace forkmesh::office;

    check(conversationForChannel(QStringLiteral("design")) ==
              QStringLiteral("#office/design"),
          "office rooms are namespaced so they can't merge with a mesh room");
    check(conversationForChannel(QStringLiteral("  ")).isEmpty(),
          "a nameless office room has no conversation");
    check(isOfficeConversation(QStringLiteral("#office/design")) &&
              !isOfficeConversation(QStringLiteral("#general")),
          "only office mirrors are recognized as office conversations");

    check(channelListProof(QStringLiteral("ada"), QStringLiteral("17")) ==
              QByteArray("forkmesh-chat-channels-v1\nada\n17"),
          "channel-list proof matches CHAT_CHANNEL_LIST_PROOF");
    check(channelHistoryProof(QStringLiteral("ada"), QString(32, 'a'),
                              QStringLiteral("17")) ==
              QByteArray("forkmesh-chat-channel-history-v1\nada\n") +
                  QByteArray(32, 'a') + QByteArray("\n17"),
          "channel-history proof matches CHAT_CHANNEL_HISTORY_PROOF");
    check(channelAccessProof(QStringLiteral("ada"), QString(32, 'a'),
                             QStringLiteral("17")) ==
              QByteArray("forkmesh-chat-channel-access-v1\nada\n") +
                  QByteArray(32, 'a') + QByteArray("\n17"),
          "channel-access proof matches CHAT_CHANNEL_ACCESS_PROOF");

    const QString room = QStringLiteral("chat-channel:") + QString(32, 'b') +
                         QStringLiteral(":v1");
    const RoomCrypto crypto(room, QStringLiteral("room-passphrase"));
    check(crypto.isValid(), "office room key derives from room + passphrase");

    QJsonObject spoken{{"type", "chat"},
                       {"id", "office-message-1"},
                       {"senderId", "office-participant"},
                       {"sender", "ada"},
                       {"accountKind", "user"},
                       {"channel", "#design"},
                       {"text", "standing by the whiteboard"},
                       {"ts", double(QDateTime::currentMSecsSinceEpoch())}};
    spoken.insert(QStringLiteral("meetingProof"),
                  QJsonObject{{"participantId", "office-participant"}});
    const QJsonObject envelope = crypto.encryptObject(spoken);
    const QJsonObject plain = crypto.decryptObject(envelope);

    ChatMessage message;
    check(chatMessageFromPlain(plain, QStringLiteral("#office/design"),
                               QStringLiteral("this-node"), &message),
          "an office chat frame becomes a desktop chat message");
    check(message.text == QStringLiteral("standing by the whiteboard") &&
              message.senderName == QStringLiteral("ada") &&
              message.conversation == QStringLiteral("#office/design") &&
              !message.self,
          "the mirrored row keeps the office author, text, and room");

    ChatMessage self;
    check(chatMessageFromPlain(plain, QStringLiteral("#office/design"),
                               QStringLiteral("office-participant"), &self) &&
              self.self,
          "our own office message is marked as ours");

    ChatMessage ignored;
    QJsonObject presence = spoken;
    presence.insert(QStringLiteral("type"), QStringLiteral("hello"));
    check(!chatMessageFromPlain(presence, QStringLiteral("#office/design"),
                                QString(), &ignored),
          "office presence frames are not chat rows");
    QJsonObject empty = spoken;
    empty.insert(QStringLiteral("text"), QString());
    check(!chatMessageFromPlain(empty, QStringLiteral("#office/design"),
                                QString(), &ignored),
          "an empty office frame with no attachment is dropped");
    check(!chatMessageFromPlain(QJsonObject(), QStringLiteral("#office/design"),
                                QString(), &ignored),
          "a frame that failed to decrypt is dropped");
}

}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ForkMeshTests"));
    app.setApplicationName(QStringLiteral("chat-transport"));

    testSavedRoomMigration();
    testDeterministicRecovery();
    testOfficeChannelMirror();
    if (failures == 0)
        std::puts("All chat transport tests passed.");
    return failures == 0 ? 0 : 1;
}
