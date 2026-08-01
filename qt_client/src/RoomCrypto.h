#pragma once

#include <QJsonObject>
#include <QString>

class RoomCrypto
{
public:
    RoomCrypto() = default;



    explicit RoomCrypto(const QString &roomName);

    RoomCrypto(const QString &roomName, const QString &passphrase);

    bool isValid() const { return !m_key.isEmpty(); }
    QString errorString() const { return m_error; }

    QJsonObject encryptObject(const QJsonObject &plain) const;
    QJsonObject decryptObject(const QJsonObject &envelope) const;

private:
    QByteArray m_key;
    QString m_error;
};
