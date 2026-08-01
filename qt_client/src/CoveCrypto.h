#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>









class CoveCrypto
{
public:


    static int defaultRounds();

    static QByteArray randomSalt();

    static QByteArray randomKey();

    static QByteArray randomBytes(int count);


    CoveCrypto(const QString &password, const QByteArray &salt, int rounds);


    static CoveCrypto withKey(const QByteArray &key);

    bool isValid() const { return !m_key.isEmpty(); }
    QString errorString() const { return m_error; }



    QJsonObject encrypt(const QByteArray &plaintext) const;



    QByteArray decrypt(const QJsonObject &cipher) const;

private:
    CoveCrypto() = default;

    QByteArray m_key;
    QString m_error;
};
