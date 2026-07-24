#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTemporaryDir;

// A bare public repository decrypted into an owner-only temporary directory.
// The gateway and the Qt client keep this object alive only while they need to
// serve/read the repository; destruction recursively removes the plaintext.
class PublicMirrorMaterialization
{
public:
    ~PublicMirrorMaterialization();

    PublicMirrorMaterialization(const PublicMirrorMaterialization &) = delete;
    PublicMirrorMaterialization &
    operator=(const PublicMirrorMaterialization &) = delete;

    QString repositoryPath() const { return m_repositoryPath; }
    bool isValid() const;

private:
    friend class PublicMirrorRuntime;
    PublicMirrorMaterialization(std::unique_ptr<QTemporaryDir> directory,
                                QString repositoryPath);

    std::unique_ptr<QTemporaryDir> m_directory;
    QString m_repositoryPath;
};

// Public repositories are public in the authorization sense, but a mirror
// operator's durable disk still stores only an authenticated age ciphertext.
// Decryption identities live in an AES-256-GCM vault bound to the local device
// identity. The official age and age-keygen programs perform all age crypto;
// ForkMesh never implements a look-alike wire format.
class PublicMirrorRuntime
{
public:
    struct Tools {
        Tools();

        QString age;
        QString ageKeygen;
        QString tar;
        QString git;
    };

    struct Metadata {
        QString archiveId;
        QString ciphertextSha256;
        QString keyReference;
        QString recipient;
        QString expectedRefsSha256;
        qint64 createdAtMs = 0;
        qint64 updatedAtMs = 0;

        bool isValid() const;
    };

    struct SyncResult {
        Metadata metadata;
        std::unique_ptr<PublicMirrorMaterialization> materialization;
        bool created = false;

        bool isValid() const
        {
            return metadata.isValid() && materialization &&
                   materialization->isValid();
        }
    };

    // Clone a local working tree/bare repository into a temporary canonical
    // repository.git, stream its tar representation into age, and atomically
    // replace the durable ciphertext. An existing archive keeps its per-repo
    // identity and random opaque id.
    static SyncResult syncRepository(
        const QString &repositoryPath, const QString &archiveRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr);

    // The remote form accepts only credential-free HTTPS URLs and deliberately
    // restricted `-c http.extraHeader=Authorization: Basic ...` arguments.
    // Ambient Git credential helpers, URL rewrites, hooks, redirects, and
    // non-HTTPS protocols are disabled.
    static SyncResult syncSource(
        const QString &source, const QStringList &gitPrefixArgs,
        const QString &archiveRoot, const QString &vaultPath,
        const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr);

    static Metadata readMetadata(const QString &archiveRoot,
                                 const QString &archiveId,
                                 QString *error = nullptr);

    // Decrypt and safely extract exactly one repository.git into a mode-0700
    // temporary directory. No plaintext tar is written to durable storage.
    static std::unique_ptr<PublicMirrorMaterialization> materialize(
        const QString &archiveRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &archiveId,
        const Tools &tools = Tools(), QString *error = nullptr);

    // Entry point used by mirror_gateway.py's external JSON command. Every
    // request field is cross-checked against the local metadata and ciphertext
    // before extraction into the gateway-created owner-only destination.
    static QJsonObject materializeGatewayRequest(
        const QJsonObject &request, const QString &archiveRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const Tools &tools = Tools(), QString *error = nullptr);

    static bool toolingAvailable(const Tools &tools = Tools(),
                                 QString *error = nullptr);
    static bool isArchiveId(const QString &value);
    // Hash "<object> <refname>" rows by refname, matching the Python gateway's
    // canonical heads+tags fingerprint regardless of Git's output order.
    static QString refsSha256FromForEachRef(const QByteArray &output);
    static QString ciphertextPath(const QString &archiveRoot,
                                  const QString &archiveId);
    static QString keyReference(const QString &archiveId);

    // Remove a legacy durable bare mirror only when it resolves beneath the
    // explicitly managed root and after the replacement ciphertext has been
    // authenticated and reopened.
    static bool removeManagedPlaintextMirror(const QString &mirrorPath,
                                             const QString &managedMirrorRoot,
                                             QString *error = nullptr);
};
