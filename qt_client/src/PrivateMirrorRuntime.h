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

// A short-lived, owner-only bare repository materialized from an opaque private
// replica. Destroying this object recursively removes the temporary directory;
// callers cannot detach it into persistent storage.
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

// Runtime boundary used by the Qt control node for private repositories.
//
// - A hybrid MirrorCrypto identity lives only in an AES-256-GCM encrypted,
//   owner-readable vault whose unlock secret is supplied by the local device
//   identity.
// - Git data is bundled in memory and persisted only through
//   PrivateMirrorStore's opaque .fm-private ciphertext.
// - Every sync replaces the archive under a new content key and key epoch.
// - Authorized plaintext materialization is recipient-only and temporary.
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

    // Load the encrypted private-mirror identity vault, creating it when it
    // does not exist. vaultSecret must be at least 256 bits of device-bound
    // entropy; it is never persisted.
    static bool loadOrCreateIdentity(const QString &vaultPath,
                                     const QByteArray &vaultSecret,
                                     MirrorCrypto::Identity *identity,
                                     QString *error = nullptr);

    // Bundle a local working tree or bare repository and create/update its
    // opaque encrypted replica. The owner is always a recipient. Each update
    // rotates the archive content key and authenticated epoch.
    static SyncResult syncRepository(
        const QString &repositoryPath, const QString &replicaRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const QString &existingOpaqueId = QString(),
        const QList<QJsonObject> &additionalRecipientBundles = {},
        QString *error = nullptr);

    // Clone a remote source into a mode-0700 temporary directory, then seal it.
    // gitPrefixArgs is deliberately restricted to repeated "-c key=value"
    // pairs, allowing the existing short-lived HTTPS Authorization header
    // without putting any secret into a path, log, or persistent config.
    static SyncResult syncSource(
        const QString &source, const QStringList &gitPrefixArgs,
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret,
        const QString &existingOpaqueId = QString(),
        const QList<QJsonObject> &additionalRecipientBundles = {},
        QString *error = nullptr);

    // Re-seal the current archive to the exact remaining recipient set. The
    // local owner is injected automatically and can never be revoked.
    static bool resealRecipients(
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &opaqueId,
        const QList<QJsonObject> &remainingRecipientBundles,
        PrivateMirrorStore::Metadata *metadata = nullptr,
        QString *error = nullptr);

    // Decrypt and clone a replica into a mode-0700 temporary directory. The
    // plaintext bundle is deleted immediately after git imports it.
    static std::unique_ptr<PrivateMirrorMaterialization> materialize(
        const QString &replicaRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &opaqueId,
        QString *error = nullptr);

    // Bound both the HTTPS receive buffer and the local importer. The direct
    // gateway may advertise a larger infrastructure limit, but this desktop
    // process deliberately accepts no more than 256 MiB in one opaque replica.
    static constexpr qint64 maximumSerializedReplicaBytes()
    {
        return 256LL * 1024 * 1024;
    }

    // Remove a legacy plaintext bare mirror only when it resolves beneath the
    // explicitly managed mirror root and has a .git suffix. This is called only
    // after a replacement encrypted replica has been authenticated.
    static bool removeManagedPlaintextMirror(const QString &mirrorPath,
                                             const QString &managedMirrorRoot,
                                             QString *error = nullptr);

    // A named repository control channel carries presence and minimal
    // "something changed" topics only—never repository bytes. Private
    // repositories omit even that named channel so their identity cannot leak
    // through socket presence or probing.
    static bool repositoryControlChannelAllowed(bool isPrivate)
    {
        return !isPrivate;
    }
};
