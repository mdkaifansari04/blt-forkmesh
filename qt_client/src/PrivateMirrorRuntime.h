#pragma once

#include "MirrorCrypto.h"
#include "PrivateMirrorStore.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

class QTemporaryDir;




class PrivateMirrorMaterialization
{
public:
    ~PrivateMirrorMaterialization();

    PrivateMirrorMaterialization(const PrivateMirrorMaterialization &) = delete;
    PrivateMirrorMaterialization &
    operator=(const PrivateMirrorMaterialization &) = delete;

    QString repositoryPath() const { return m_repositoryPath; }
    bool isValid() const;

private:
    friend class PrivateMirrorRuntime;
    PrivateMirrorMaterialization(std::unique_ptr<QTemporaryDir> directory,
                                 QString repositoryPath);

    std::unique_ptr<QTemporaryDir> m_directory;
    QString m_repositoryPath;
};










class PrivateMirrorRuntime
{
public:
    struct SyncResult {
        QString opaqueId;
        PrivateMirrorStore::Metadata metadata;
        bool created = false;

        bool isValid() const
        {
            return PrivateMirrorStore::isOpaqueId(opaqueId) &&
                   metadata.opaqueId == opaqueId && metadata.keyEpoch > 0;
        }
    };




    static bool loadOrCreateIdentity(const QString &vaultPath,
                                     const QByteArray &vaultSecret,
                                     MirrorCrypto::Identity *identity,
                                     QString *error = nullptr);




    static SyncResult syncRepository(
        const QString &repositoryPath, const QString &replicaRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const QString &existingOpaqueId = QString(),
        const QList<QJsonObject> &additionalRecipientBundles = {},
        QString *error = nullptr);





    static SyncResult syncSource(
        const QString &source, const QStringList &gitPrefixArgs,
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret,
        const QString &existingOpaqueId = QString(),
        const QList<QJsonObject> &additionalRecipientBundles = {},
        QString *error = nullptr);



    static bool resealRecipients(
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &opaqueId,
        const QList<QJsonObject> &remainingRecipientBundles,
        PrivateMirrorStore::Metadata *metadata = nullptr,
        QString *error = nullptr);



    static std::unique_ptr<PrivateMirrorMaterialization> materialize(
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &opaqueId,
        QString *error = nullptr);




    static constexpr qint64 maximumSerializedReplicaBytes()
    {
        return 256LL * 1024 * 1024;
    }




    static bool removeManagedPlaintextMirror(const QString &mirrorPath,
                                             const QString &managedMirrorRoot,
                                             QString *error = nullptr);





    static bool repositoryControlChannelAllowed(bool isPrivate)
    {
        return !isPrivate;
    }
};
