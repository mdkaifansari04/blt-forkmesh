#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class ForkMeshIdentity;

// One document inside a cove (a note, a password list, a plan). The body is only
// ever present in memory after the cove is unlocked; on disk it lives inside the
// AES-256-GCM ciphertext.
struct CoveDocument {
    QString id;
    QString name;
    QString mime = "text/markdown";
    QString body;
    qint64 updatedAtMs = 0;

    QJsonObject toJson() const;
    static CoveDocument fromJson(const QJsonObject &obj);
};

// One entry in a cove's access log: who opened (or edited) it, and when. Kept
// inside the encrypted payload so the creator sees the trail once it syncs.
struct CoveAccessEntry {
    QString who;    // opener pubkey (base64url)
    QString name;   // opener display name
    QString node;   // opener node id (short)
    qint64 ts = 0;
    QString action = "open"; // open | edit

    QJsonObject toJson() const;
    static CoveAccessEntry fromJson(const QJsonObject &obj);
};

// A cove: plaintext envelope metadata (readable while locked) plus the decrypted
// payload (documents + access log), present only after a successful unlock().
struct Cove {
    QString id;
    QString name;        // human-readable; kept inside the ciphertext, not the repo
    QString slug;        // obscure file stem — reveals nothing about the cove
    QString relPath;     // .forkmesh/coves/<slug>.cove (repo-relative)
    QString creator;     // creator pubkey (base64url) — routes open-notifications
    qint64 createdAtMs = 0;
    bool notifyOnOpen = false;
    QByteArray salt;     // KDF salt (raw bytes)
    int rounds = 0;      // KDF rounds
    QJsonObject cipher;  // {nonce,tag,body} base64

    // Populated by unlock():
    bool unlocked = false;
    QList<CoveDocument> documents;
    QList<CoveAccessEntry> accessLog;

    bool createdByMe(const ForkMeshIdentity *identity) const;
};

// Repo-scoped vault of encrypted coves backed by .forkmesh/coves/*.cove. For
// repos with a local working tree the store reads/writes/commits files directly;
// for mirror-only repos it lists + reads envelopes read-only via `git show`.
class CoveStore
{
public:
    CoveStore(QString workTreePath, QString mirrorPath,
              const ForkMeshIdentity *identity, QString authorName = QString());

    // True when this node can create/save coves (has a real working tree + key).
    bool canWrite() const;

    // List cove envelopes (metadata only; nothing is decrypted).
    QList<Cove> listCoves(QString *error = nullptr) const;
    // Load a single cove envelope by repo-relative path (metadata only).
    bool loadEnvelope(const QString &relPath, Cove &out, QString *error = nullptr) const;

    // Decrypt a cove with a password. On success fills documents/accessLog and
    // sets unlocked. Returns false (cove untouched) when the password is wrong.
    static bool unlock(Cove &cove, const QString &password);

    // Create a new cove, encrypt it, write the .cove file and commit it.
    bool createCove(const QString &name, const QString &password, bool notifyOnOpen,
                    const QList<CoveDocument> &documents, Cove *out,
                    QString *error = nullptr);
    // Re-encrypt an unlocked cove's documents + access log under the password,
    // write the file and commit. The cove must already be unlocked.
    bool save(const Cove &cove, const QString &password, QString *error = nullptr);

    // Append an access entry to an unlocked cove in memory (caller persists via
    // save() when it has write access; the local self-log is separate).
    static void appendAccess(Cove &cove, const CoveAccessEntry &entry);

    static QString covesDirRel() { return QStringLiteral(".forkmesh/coves"); }

private:
    QString covesDir() const; // <workTree>/.forkmesh/coves
    QByteArray readCoveBytes(const QString &relPath, bool *ok) const;
    bool commit(const QString &message, const QString &relPath, QString *error) const;
    QString mirrorRef() const;
    QString uniqueSlug() const; // obscure, name-free file stem

    QString m_workTree;
    QString m_mirror;
    const ForkMeshIdentity *m_identity;
    QString m_authorName;
};
