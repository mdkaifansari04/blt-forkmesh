#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

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
        const Tools &tools = Tools(), QString *error = nullptr);





    static SyncResult syncSource(
        const QString &source, const QStringList &gitPrefixArgs,
        const QString &archiveRoot, const QString &vaultPath,
        const QByteArray &vaultSecret,
        const QString &existingArchiveId = QString(),
        const Tools &tools = Tools(), QString *error = nullptr);

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




    static bool removeManagedPlaintextMirror(const QString &mirrorPath,
                                             const QString &managedMirrorRoot,
                                             QString *error = nullptr);
};
