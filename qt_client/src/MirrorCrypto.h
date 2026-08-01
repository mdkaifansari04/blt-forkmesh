#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>















class MirrorCrypto
{
public:



    struct Identity {
        QByteArray x25519Pub;
        QByteArray x25519Priv;
        QByteArray mlkemPub;
        QByteArray mlkemPriv;

        bool isValid() const;


        QJsonObject publicBundle() const;



        QString keyId() const;
    };


    static Identity generateIdentity();


    static QString publicKeyId(const QJsonObject &publicBundle);







    static QJsonObject sealArchive(const QByteArray &plaintext,
                                   const QList<QJsonObject> &recipientBundles,
                                   QString *error = nullptr);





    static QByteArray openArchive(const QJsonObject &envelope,
                                  const Identity &me,
                                  QString *error = nullptr);













    static QJsonObject sealOwnerPayload(const QByteArray &plaintext,
                                        const QJsonObject &recipientBundle,
                                        QString *error = nullptr);




    static QByteArray openOwnerPayload(const QJsonObject &envelope,
                                       const Identity &me,
                                       QString *error = nullptr);


    static QString ownerPayloadKeyId(const QJsonObject &envelope);
};
