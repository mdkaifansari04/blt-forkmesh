#pragma once

#include <QString>
#include <QUrl>

// The one public room shared by the browser World and installed Qt clients.
// Keep its endpoint and saved-settings migration independent of MainWindow's
// widget helpers so the protocol contract can be exercised by a small,
// headless test target.
namespace forkmesh::mainnode {

inline const QString kDefaultHost = QStringLiteral("forkmesh.com");
inline const QString kDefaultRoomName = QStringLiteral("world-general");
inline const QString kLegacyDefaultRoomName = QStringLiteral("general");
inline const QString kRoomPath =
    QStringLiteral("/api/repo/mainnode/forkmesh/rooms/world-general/ws");
inline const QString kLegacyRoomPath =
    QStringLiteral("/api/repo/mainnode/forkmesh/rooms/general/ws");
inline const QString kLocalServerUrl =
    QStringLiteral("ws://127.0.0.1:8787") + kRoomPath;
inline const QString kDefaultServerUrl =
    QStringLiteral("wss://") + kDefaultHost + kRoomPath;

// Upgrade only ForkMesh's exact retired shared-room endpoint. A user-defined
// repository/private-room path is deliberately left byte-for-byte unchanged.
// The host may be forkmesh.com, localhost, or an independently operated relay:
// all current Workers expose the same network-wide mainnode room path.
inline bool migrateSavedDefaultRoom(QString *serverUrl, QString *roomName)
{
    if (!serverUrl || !roomName)
        return false;

    QUrl url(serverUrl->trimmed());
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QLatin1String("ws") &&
         url.scheme() != QLatin1String("wss")))
        return false;

    const QString room = roomName->trimmed();
    const bool knownDefaultRoom =
        room.isEmpty() ||
        room.compare(kLegacyDefaultRoomName, Qt::CaseInsensitive) == 0 ||
        room.compare(kDefaultRoomName, Qt::CaseInsensitive) == 0;
    const bool knownDefaultPath =
        url.path() == kLegacyRoomPath || url.path() == kRoomPath;
    if (!knownDefaultRoom || !knownDefaultPath)
        return false;

    bool changed = false;
    if (url.path() != kRoomPath) {
        url.setPath(kRoomPath);
        *serverUrl = url.toString(QUrl::FullyEncoded);
        changed = true;
    }
    if (*roomName != kDefaultRoomName) {
        *roomName = kDefaultRoomName;
        changed = true;
    }
    return changed;
}

} // namespace forkmesh::mainnode
