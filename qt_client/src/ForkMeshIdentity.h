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

    // Whether this identity has already posted its one-time #welcome greeting.
    // Backed by a sentinel file next to the key itself (not QSettings, which
    // can live in a separate, less-persistent config location on some
    // deployments) so the flag can never outlive — or be outlived by — the
    // identity it describes.
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
    // Sign arbitrary bytes (e.g. a canonical account/auth string). Returns a
    // base64url Ed25519 signature, matching signJson's encoding.
    QString signData(const QByteArray &payload) const;

    // Verify a detached Ed25519 signature against any node's public key. Both
    // are base64url (no padding), matching publicKey()/signData(). Used to
    // authenticate signed actions from other nodes (e.g. admin moderation).
    static bool verifySignature(const QString &publicKeyB64url,
                                const QString &signatureB64url,
                                const QByteArray &payload);

    // --- Backup, export & rotation (issue #368) ---------------------------
    // Serialize this identity's private key to a passphrase-encrypted,
    // self-describing keyfile (age-style JSON envelope: a memory-hard KDF's
    // parameters plus AES-256-GCM ciphertext of the raw 32-byte Ed25519 seed).
    // Safe to store or print anywhere — only the passphrase unlocks it. Returns
    // an empty string if the identity is not loaded or the passphrase is empty.
    QString exportEncryptedKeyfile(const QString &passphrase) const;

    // Read the public key recorded (in the clear) in a keyfile without needing
    // the passphrase, e.g. to warn before overwriting a different identity.
    // Returns {} if the text is not a ForkMesh keyfile.
    static QString keyfilePublicKey(const QString &keyfileText);

    // Decrypt a keyfile and install it as this node's identity, replacing the
    // on-disk ed25519.pem and reloading. Returns false (with errorString set)
    // on a wrong passphrase or a malformed keyfile.
    bool importEncryptedKeyfile(const QString &keyfileText,
                                const QString &passphrase);

    // First-run backup nag: has the user saved/acknowledged a backup of this
    // key yet? Backed by a sentinel file next to the key, same rationale as
    // hasAnnouncedWelcome().
    bool hasBackedUp() const;
    void markBackedUp() const;

    // Rotation: the current ("old") key signs a successor public key, producing
    // a record the mainnode, peers and worker honor to move an account/name to
    // the new key. Returns {} if the identity is not loaded.
    QJsonObject signRotation(const QString &newPublicKeyB64url) const;
    // Verify a rotation record's self-consistency: that the old key really did
    // sign the successor. Callers still decide whether the old key is the one
    // currently bound to the account being rotated.
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
    QString m_keyDir; // directory holding ed25519.pem, set by load()
};
