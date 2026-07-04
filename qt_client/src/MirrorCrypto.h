#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// End-to-end encryption for private repositories mirrored as opaque blobs
// (issue #362, roadmap phase 3). The owner node encrypts the whole-mirror
// archive client-side with a random AES-256-GCM content key; mirrors store only
// the ciphertext and the public catalog shows a handle + size. Each collaborator
// receives the content key wrapped to their identity keys, so the relay and
// every mirror stay zero-knowledge.
//
// The wrap uses a HYBRID KEM — X25519 + ML-KEM-768 — combined with HKDF-SHA256.
// X25519 gives classical security today; ML-KEM-768 (FIPS 203, shipped by
// OpenSSL 3.5) means the stored ciphertext is not harvest-now-decrypt-later
// bait for a future quantum adversary. Breaking a wrap requires breaking BOTH.
//
// v1 scope: whole-mirror encrypted archives (simple and correct). Content-defined
// chunking for incremental sync is a follow-up.
class MirrorCrypto
{
public:
    // A collaborator's hybrid identity: an X25519 keypair and an ML-KEM-768
    // keypair. The public halves are published (via the identity/catalog); the
    // private halves never leave the node.
    struct Identity {
        QByteArray x25519Pub;  // 32 bytes
        QByteArray x25519Priv; // 32 bytes
        QByteArray mlkemPub;   // 1184 bytes
        QByteArray mlkemPriv;  // 2400 bytes

        bool isValid() const;
        // The public-only bundle to hand out to owners who wrap content keys to
        // this identity: {"v":1,"x25519":b64,"mlkem768":b64}.
        QJsonObject publicBundle() const;
        // A short, stable id for this identity derived from its public keys, so a
        // recipient can find its own wrap entry in an envelope without trial
        // decryption. Matches publicKeyId(publicBundle()).
        QString keyId() const;
    };

    // Generate a fresh hybrid identity. Returns an invalid Identity on failure.
    static Identity generateIdentity();

    // The key id for a public bundle (base64url SHA-256 of its raw public keys).
    static QString publicKeyId(const QJsonObject &publicBundle);

    // Seal opaque archive bytes to a set of recipient public bundles. The result
    // is an envelope JSON:
    //   {"kind":"forkmesh.mirror","v":1,"alg":"x25519+mlkem768/aes256gcm",
    //    "nonce","tag","body",           // archive under the content key
    //    "recipients":[ {kid,x25519,mlkem768,nonce,tag,key}, ... ]}
    // Returns {} on failure (and sets *error when non-null).
    static QJsonObject sealArchive(const QByteArray &plaintext,
                                   const QList<QJsonObject> &recipientBundles,
                                   QString *error = nullptr);

    // Open an envelope with the recipient's identity: recover the content key
    // from this identity's wrap entry and decrypt the archive. Returns the
    // plaintext bytes, or {} when this identity is not a recipient / the envelope
    // is malformed / a tag mismatches (and sets *error when non-null).
    static QByteArray openArchive(const QJsonObject &envelope,
                                  const Identity &me,
                                  QString *error = nullptr);
};
