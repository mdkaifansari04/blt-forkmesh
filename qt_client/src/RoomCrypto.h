#pragma once

#include <QJsonObject>
#include <QString>

class RoomCrypto
{
public:
    RoomCrypto() = default;
    // Passphrase-free: derive the room key from a baked-in app key + the room
    // name, so every ForkMesh node joins the same shared rooms with no user
    // passphrase. The relay still only ever sees AES-256-GCM ciphertext.
    explicit RoomCrypto(const QString &roomName);
    // Legacy: derive the key from a user passphrase (kept for compatibility).
    RoomCrypto(const QString &roomName, const QString &passphrase);

    bool isValid() const { return !m_key.isEmpty(); }
    QString errorString() const { return m_error; }

    QJsonObject encryptObject(const QJsonObject &plain) const;
    QJsonObject decryptObject(const QJsonObject &envelope) const;

private:
    QByteArray m_key;
    QString m_error;
};
