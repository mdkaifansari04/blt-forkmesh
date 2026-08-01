#pragma once

#include "MirrorCrypto.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>







class PrivateMirrorStore
{
public:
    struct Metadata {
        QString opaqueId;
        QString ownerKeyId;
        quint64 keyEpoch = 0;
        qint64 createdAtMs = 0;
        qint64 updatedAtMs = 0;



        QString replicaFileSha256;
        QString ciphertextSha256;
        QStringList recipientKeyIds;
    };





    static QString createReplica(
        const QString &storageRoot, const QByteArray &archive,
        const QList<QJsonObject> &recipientPublicBundles,
        QString *error = nullptr);



    static QByteArray openReplica(
        const QString &storageRoot, const QString &opaqueId,
        const MirrorCrypto::Identity &identity,
        Metadata *metadata = nullptr, QString *error = nullptr);





    static bool replaceReplica(
        const QString &storageRoot, const QString &opaqueId,
        const QByteArray &archive,
        const MirrorCrypto::Identity &currentOwner,
        const QList<QJsonObject> &recipientPublicBundles,
        QString *error = nullptr);




    static bool rotateRecipients(
        const QString &storageRoot, const QString &opaqueId,
        const MirrorCrypto::Identity &currentOwner,
        const QList<QJsonObject> &newRecipientPublicBundles,
        QString *error = nullptr);



    static bool inspectReplica(
        const QString &storageRoot, const QString &opaqueId,
        Metadata *metadata, QString *error = nullptr);





    static QString importReplica(
        const QString &storageRoot, const QByteArray &serializedReplica,
        const QString &expectedFileSha256,
        Metadata *metadata = nullptr, QString *error = nullptr);

    static bool isOpaqueId(const QString &value);

private:
    static QString replicaPath(const QString &storageRoot,
                               const QString &opaqueId);
};
