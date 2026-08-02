#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QTemporaryDir;




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

    // Optional one-line notes about the stage a seal is in ("Cloning the
    // source…", "Encrypting the mirror…"). Sealing a large repository is
    // minutes of silence otherwise. Invoked on whichever thread runs the sync,
    // so a GUI caller must marshal the string across itself.
    using Progress = std::function<void(const QString &)>;

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





    static SyncResult syncRepository(
        const QString &repositoryPath, const QString &archiveRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr,
        const Progress &progress = Progress());

    // A service-managed headless checkout may contain local agent branches.
    // Seal only its fetched origin branch view so those in-flight refs remain
    // private to the working checkout and cannot invalidate the source pin.
    static SyncResult syncManagedCheckout(
        const QString &repositoryPath, const QString &archiveRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr,
        const Progress &progress = Progress());

    // The remote form accepts only credential-free HTTPS URLs and deliberately
    // restricted `-c http.extraHeader=Authorization: Basic ...` arguments.
    // Ambient Git credential helpers, URL rewrites, hooks, redirects, and
    // non-HTTPS protocols are disabled.
    static SyncResult syncSource(
        const QString &source, const QStringList &gitPrefixArgs,
        const QString &archiveRoot, const QString &vaultPath,
        const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr,
        const Progress &progress = Progress());

    static Metadata readMetadata(const QString &archiveRoot,
                                 const QString &archiveId,
                                 QString *error = nullptr);



    static std::unique_ptr<PublicMirrorMaterialization> materialize(
        const QString &archiveRoot, const QString &vaultPath,
        const QByteArray &vaultSecret, const QString &archiveId,
        const Tools &tools = Tools(), QString *error = nullptr);




    static QJsonObject materializeGatewayRequest(
        const QJsonObject &request, const QString &archiveRoot,
        const QString &vaultPath, const QByteArray &vaultSecret,
        const Tools &tools = Tools(), QString *error = nullptr);

    static bool toolingAvailable(const Tools &tools = Tools(),
                                 QString *error = nullptr);
    static bool isArchiveId(const QString &value);


    static QString refsSha256FromForEachRef(const QByteArray &output);
    static QString ciphertextPath(const QString &archiveRoot,
                                  const QString &archiveId);
    static QString keyReference(const QString &archiveId);

    // A supervised node keeps TMPDIR on persistent local disk so large mirror
    // materializations do not exhaust a RAM-backed /tmp. Crashes cannot run
    // QTemporaryDir/Python cleanup, so remove only exact ForkMesh-owned
    // temporary-directory shapes after the single-instance lock is held.
    static int cleanupStaleTemporaryDirectories(
        const QString &temporaryRoot, QString *error = nullptr);

    // Remove a legacy durable bare mirror only when it resolves beneath the
    // explicitly managed root and after the replacement ciphertext has been
    // authenticated and reopened.
    static bool removeManagedPlaintextMirror(const QString &mirrorPath,
                                             const QString &managedMirrorRoot,
                                             QString *error = nullptr);
};
