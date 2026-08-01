#pragma once

#include <QJsonObject>
#include <QString>

typedef struct evp_pkey_st EVP_PKEY;




class ForkMeshIdentity
{
public:
    ForkMeshIdentity() = default;
    ~ForkMeshIdentity();

    ForkMeshIdentity(const ForkMeshIdentity &) = delete;
    ForkMeshIdentity &operator=(const ForkMeshIdentity &) = delete;

    bool load();
    bool isValid() const { return m_key != nullptr && !m_publicKey.isEmpty(); }

    QString errorString() const { return m_error; }
    QString publicKey() const { return m_publicKey; }
    QString shortPublicKey() const;






    bool hasAnnouncedWelcome() const;
    void markWelcomeAnnounced() const;

    QJsonObject profileObject(const QString &name, const QString &handle,
                              const QString &solanaAddress,
                              const QString &bio = QString(),
                              const QString &website = QString()) const;
    QJsonObject signedProfile(const QString &name, const QString &handle,
                              const QString &solanaAddress,
                              const QString &bio = QString(),
                              const QString &website = QString()) const;
    QString signJson(const QJsonObject &object) const;


    QString signData(const QByteArray &payload) const;




    static bool verifySignature(const QString &publicKeyB64url,
                                const QString &signatureB64url,
                                const QByteArray &payload);
    static QByteArray deviceBindCanonical(const QString &accountName,
                                          const QString &publicKeyB64url,
                                          const QString &timestamp);







    QString exportEncryptedKeyfile(const QString &passphrase) const;




    static QString keyfilePublicKey(const QString &keyfileText);




    bool importEncryptedKeyfile(const QString &keyfileText,
                                const QString &passphrase);




    bool hasBackedUp() const;
    void markBackedUp() const;




    QJsonObject signRotation(const QString &newPublicKeyB64url) const;



    static bool verifyRotation(const QJsonObject &record);

private:
    static QString defaultKeyDir();
    bool generate(const QString &keyPath);
    bool readKey(const QString &keyPath);
    bool refreshPublicKey();
    QByteArray rawPrivateSeed() const;
    bool installRawSeed(const QByteArray &seed);

    EVP_PKEY *m_key = nullptr;
    QString m_publicKey;
    QString m_error;
    QString m_keyDir;
};
