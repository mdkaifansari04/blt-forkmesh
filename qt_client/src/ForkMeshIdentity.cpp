#include "ForkMeshIdentity.h"

#include "CoveCrypto.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

namespace {

QString base64Url(const QByteArray &data)
{
    return QString::fromLatin1(
        data.toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals));
}

} // namespace

ForkMeshIdentity::~ForkMeshIdentity()
{
    if (m_key)
        EVP_PKEY_free(m_key);
}

QString ForkMeshIdentity::defaultKeyDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/identity";
}

bool ForkMeshIdentity::load()
{
    if (isValid())
        return true;

    const QString dir = defaultKeyDir();
    if (!QDir().mkpath(dir)) {
        m_error = "Could not create identity key directory: " + dir;
        return false;
    }

    const QString keyPath = dir + "/ed25519.pem";
    if (!QFile::exists(keyPath) && !generate(keyPath))
        return false;
    m_keyDir = dir;
    return readKey(keyPath) && refreshPublicKey();
}

bool ForkMeshIdentity::generate(const QString &keyPath)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) {
        m_error = "OpenSSL does not support Ed25519 identity keys.";
        return false;
    }

    EVP_PKEY *key = nullptr;
    const bool ok = EVP_PKEY_keygen_init(ctx) == 1 &&
                    EVP_PKEY_keygen(ctx, &key) == 1;
    EVP_PKEY_CTX_free(ctx);
    if (!ok || !key) {
        if (key)
            EVP_PKEY_free(key);
        m_error = "Could not generate the Ed25519 identity key.";
        return false;
    }

    const QByteArray pathBytes = QFile::encodeName(keyPath);
    BIO *bio = BIO_new_file(pathBytes.constData(), "w");
    if (!bio) {
        EVP_PKEY_free(key);
        m_error = "Could not open the identity key for writing: " + keyPath;
        return false;
    }

    const int written =
        PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(bio);
    EVP_PKEY_free(key);
    if (written != 1) {
        QFile::remove(keyPath);
        m_error = "Could not write the Ed25519 identity key.";
        return false;
    }
    QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool ForkMeshIdentity::readKey(const QString &keyPath)
{
    if (m_key) {
        EVP_PKEY_free(m_key);
        m_key = nullptr;
    }

    const QByteArray pathBytes = QFile::encodeName(keyPath);
    BIO *bio = BIO_new_file(pathBytes.constData(), "r");
    if (!bio) {
        m_error = "Could not read the identity key from " + keyPath;
        return false;
    }

    m_key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!m_key) {
        m_error = "Stored identity key is invalid. Delete " + keyPath +
                  " and restart to regenerate it.";
        return false;
    }
    if (EVP_PKEY_base_id(m_key) != EVP_PKEY_ED25519) {
        m_error = "Stored identity key is not an Ed25519 key.";
        return false;
    }
    return true;
}

bool ForkMeshIdentity::refreshPublicKey()
{
    size_t len = 0;
    if (EVP_PKEY_get_raw_public_key(m_key, nullptr, &len) != 1 || len == 0) {
        m_error = "Could not read the Ed25519 public key.";
        return false;
    }

    QByteArray raw(int(len), Qt::Uninitialized);
    if (EVP_PKEY_get_raw_public_key(
            m_key, reinterpret_cast<unsigned char *>(raw.data()), &len) != 1) {
        m_error = "Could not export the Ed25519 public key.";
        return false;
    }
    raw.resize(int(len));
    m_publicKey = base64Url(raw);
    return true;
}

bool ForkMeshIdentity::hasAnnouncedWelcome() const
{
    return !m_keyDir.isEmpty() &&
           QFile::exists(m_keyDir + "/welcome_announced");
}

void ForkMeshIdentity::markWelcomeAnnounced() const
{
    if (m_keyDir.isEmpty())
        return;
    QFile f(m_keyDir + "/welcome_announced");
    f.open(QIODevice::WriteOnly);
}

QString ForkMeshIdentity::shortPublicKey() const
{
    if (m_publicKey.size() <= 16)
        return m_publicKey;
    return m_publicKey.left(8) + "..." + m_publicKey.right(8);
}

QJsonObject ForkMeshIdentity::profileObject(const QString &name,
                                            const QString &handle,
                                            const QString &solanaAddress,
                                            const QString &bio,
                                            const QString &website) const
{
    return {{"name", name.trimmed()},
            {"handle", handle.trimmed()},
            {"bio", bio.trimmed()},
            {"website", website.trimmed()},
            {"solana", solanaAddress.trimmed()},
            {"pubkey", m_publicKey},
            {"updatedAt", QString::number(QDateTime::currentMSecsSinceEpoch())}};
}

QJsonObject ForkMeshIdentity::signedProfile(const QString &name,
                                            const QString &handle,
                                            const QString &solanaAddress,
                                            const QString &bio,
                                            const QString &website) const
{
    const QJsonObject profile =
        profileObject(name, handle, solanaAddress, bio, website);
    return {{"kind", "forkmesh.identity"},
            {"profile", profile},
            {"signature", signJson(profile)}};
}

QString ForkMeshIdentity::signJson(const QJsonObject &object) const
{
    return signData(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString ForkMeshIdentity::signData(const QByteArray &payload) const
{
    if (!m_key)
        return {};

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return {};

    size_t sigLen = 0;
    bool ok = EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, m_key) == 1 &&
              EVP_DigestSign(ctx, nullptr, &sigLen,
                             reinterpret_cast<const unsigned char *>(payload.constData()),
                             payload.size()) == 1 &&
              sigLen > 0;
    QByteArray signature(int(sigLen), Qt::Uninitialized);
    if (ok) {
        ok = EVP_DigestSign(
                 ctx, reinterpret_cast<unsigned char *>(signature.data()), &sigLen,
                 reinterpret_cast<const unsigned char *>(payload.constData()),
                 payload.size()) == 1;
    }
    EVP_MD_CTX_free(ctx);
    if (!ok)
        return {};

    signature.resize(int(sigLen));
    return base64Url(signature);
}

bool ForkMeshIdentity::verifySignature(const QString &publicKeyB64url,
                                       const QString &signatureB64url,
                                       const QByteArray &payload)
{
    const QByteArray rawKey = QByteArray::fromBase64(
        publicKeyB64url.toLatin1(), QByteArray::Base64UrlEncoding);
    const QByteArray sig = QByteArray::fromBase64(
        signatureB64url.toLatin1(), QByteArray::Base64UrlEncoding);
    // Ed25519 keys are 32 bytes, signatures 64 bytes.
    if (rawKey.size() != 32 || sig.size() != 64)
        return false;

    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(rawKey.constData()),
        size_t(rawKey.size()));
    if (!key)
        return false;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx &&
        EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        ok = EVP_DigestVerify(
                 ctx, reinterpret_cast<const unsigned char *>(sig.constData()),
                 size_t(sig.size()),
                 reinterpret_cast<const unsigned char *>(payload.constData()),
                 size_t(payload.size())) == 1;
    }
    if (ctx)
        EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

// --- Backup, export & rotation (issue #368) -------------------------------

QByteArray ForkMeshIdentity::rawPrivateSeed() const
{
    if (!m_key)
        return {};
    size_t len = 0;
    if (EVP_PKEY_get_raw_private_key(m_key, nullptr, &len) != 1 || len == 0)
        return {};
    QByteArray raw(int(len), Qt::Uninitialized);
    if (EVP_PKEY_get_raw_private_key(
            m_key, reinterpret_cast<unsigned char *>(raw.data()), &len) != 1)
        return {};
    raw.resize(int(len));
    return raw;
}

QString ForkMeshIdentity::exportEncryptedKeyfile(const QString &passphrase) const
{
    if (!m_key || passphrase.isEmpty())
        return {};
    const QByteArray seed = rawPrivateSeed();
    if (seed.size() != 32) // Ed25519 raw private seed
        return {};

    const QByteArray salt = CoveCrypto::randomSalt();
    const int rounds = CoveCrypto::defaultRounds();
    CoveCrypto crypto(passphrase, salt, rounds);
    if (!crypto.isValid())
        return {};
    const QJsonObject cipher = crypto.encrypt(seed);
    if (cipher.isEmpty())
        return {};

    const QJsonObject envelope{
        {"kind", "forkmesh.identity.keyfile"},
        {"v", 1},
        {"kdf", "pbkdf2-hmac-sha256"},
        {"salt", QString::fromLatin1(salt.toBase64())},
        {"rounds", rounds},
        {"pubkey", m_publicKey}, // clear-text: identifies, never unlocks
        {"createdAt", QString::number(QDateTime::currentMSecsSinceEpoch())},
        {"cipher", cipher}};
    return QString::fromUtf8(
        QJsonDocument(envelope).toJson(QJsonDocument::Indented));
}

QString ForkMeshIdentity::keyfilePublicKey(const QString &keyfileText)
{
    const QJsonObject env =
        QJsonDocument::fromJson(keyfileText.toUtf8()).object();
    if (env.value("kind").toString() != "forkmesh.identity.keyfile")
        return {};
    return env.value("pubkey").toString();
}

bool ForkMeshIdentity::installRawSeed(const QByteArray &seed)
{
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(seed.constData()),
        size_t(seed.size()));
    if (!key) {
        m_error = "Could not construct an Ed25519 key from the keyfile.";
        return false;
    }

    const QString dir = defaultKeyDir();
    if (!QDir().mkpath(dir)) {
        EVP_PKEY_free(key);
        m_error = "Could not create identity key directory: " + dir;
        return false;
    }
    const QString keyPath = dir + "/ed25519.pem";
    const QByteArray pathBytes = QFile::encodeName(keyPath);
    BIO *bio = BIO_new_file(pathBytes.constData(), "w");
    if (!bio) {
        EVP_PKEY_free(key);
        m_error = "Could not open the identity key for writing: " + keyPath;
        return false;
    }
    const int written =
        PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(bio);
    EVP_PKEY_free(key);
    if (written != 1) {
        m_error = "Could not write the imported identity key.";
        return false;
    }
    QFile::setPermissions(keyPath, QFile::ReadOwner | QFile::WriteOwner);
    m_keyDir = dir;
    // A restored key is, by definition, one the user already holds a backup of.
    markBackedUp();
    return readKey(keyPath) && refreshPublicKey();
}

bool ForkMeshIdentity::importEncryptedKeyfile(const QString &keyfileText,
                                              const QString &passphrase)
{
    const QJsonObject env =
        QJsonDocument::fromJson(keyfileText.toUtf8()).object();
    if (env.value("kind").toString() != "forkmesh.identity.keyfile") {
        m_error = "This file is not a ForkMesh identity keyfile.";
        return false;
    }
    const QByteArray salt =
        QByteArray::fromBase64(env.value("salt").toString().toLatin1());
    const int rounds = env.value("rounds").toInt();
    if (salt.isEmpty() || rounds <= 0) {
        m_error = "The keyfile is missing its key-derivation parameters.";
        return false;
    }
    CoveCrypto crypto(passphrase, salt, rounds);
    if (!crypto.isValid()) {
        m_error = crypto.errorString();
        return false;
    }
    const QByteArray seed = crypto.decrypt(env.value("cipher").toObject());
    if (seed.size() != 32) {
        m_error = "Wrong passphrase, or the keyfile is corrupt.";
        return false;
    }
    return installRawSeed(seed);
}

bool ForkMeshIdentity::hasBackedUp() const
{
    return !m_keyDir.isEmpty() && QFile::exists(m_keyDir + "/backed_up");
}

void ForkMeshIdentity::markBackedUp() const
{
    if (m_keyDir.isEmpty())
        return;
    QFile f(m_keyDir + "/backed_up");
    f.open(QIODevice::WriteOnly);
}

QJsonObject ForkMeshIdentity::signRotation(const QString &newPublicKeyB64url) const
{
    if (!m_key || m_publicKey.isEmpty() || newPublicKeyB64url.isEmpty())
        return {};
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray canonical = ("forkmesh-rotate-v1\n" + m_publicKey + "\n" +
                                  newPublicKeyB64url + "\n" + ts)
                                     .toUtf8();
    const QString sig = signData(canonical);
    if (sig.isEmpty())
        return {};
    return {{"kind", "forkmesh.rotate"},
            {"oldPubkey", m_publicKey},
            {"newPubkey", newPublicKeyB64url},
            {"ts", ts},
            {"signature", sig}};
}

bool ForkMeshIdentity::verifyRotation(const QJsonObject &record)
{
    if (record.value("kind").toString() != "forkmesh.rotate")
        return false;
    const QString oldPub = record.value("oldPubkey").toString();
    const QString newPub = record.value("newPubkey").toString();
    const QString ts = record.value("ts").toString();
    const QString sig = record.value("signature").toString();
    if (oldPub.isEmpty() || newPub.isEmpty() || ts.isEmpty() || sig.isEmpty())
        return false;
    const QByteArray canonical =
        ("forkmesh-rotate-v1\n" + oldPub + "\n" + newPub + "\n" + ts).toUtf8();
    return verifySignature(oldPub, sig, canonical);
}
