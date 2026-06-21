#pragma once

#include <QJsonObject>
#include <QString>

typedef struct evp_pkey_st EVP_PKEY;

// Long-lived ForkMesh identity key. This is separate from the LAN TLS
// certificate: the Ed25519 key signs profile and repository metadata while TLS
// keeps peer links encrypted.
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

    QJsonObject profileObject(const QString &name, const QString &handle,
                              const QString &solanaAddress,
                              const QString &bio = QString(),
                              const QString &website = QString()) const;
    QJsonObject signedProfile(const QString &name, const QString &handle,
                              const QString &solanaAddress,
                              const QString &bio = QString(),
                              const QString &website = QString()) const;
    QString signJson(const QJsonObject &object) const;
    // Sign arbitrary bytes (e.g. a canonical account/auth string). Returns a
    // base64url Ed25519 signature, matching signJson's encoding.
    QString signData(const QByteArray &payload) const;

private:
    bool generate(const QString &keyPath);
    bool readKey(const QString &keyPath);
    bool refreshPublicKey();

    EVP_PKEY *m_key = nullptr;
    QString m_publicKey;
    QString m_error;
};
