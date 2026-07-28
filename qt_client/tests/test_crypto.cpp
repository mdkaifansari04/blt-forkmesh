#include "../src/ActionFile.h"
#include "../src/ActionStore.h"
#include "../src/AccountCapability.h"
#include "../src/AgentJail.h"
#include "../src/AgentStore.h"
#include "../src/BackgroundActivity.h"
#include "../src/BackoffNetworkAccessManager.h"
#include "../src/ChatHistoryLimits.h"
#include "../src/ChatVisitorPresence.h"
#include "../src/CommitCommentStore.h"
#include "../src/CoveCrypto.h"
#include "../src/CoveStore.h"
#include "../src/DiscussionInboxBackoff.h"
#include "../src/DiscussionStore.h"
#include "../src/ForkMeshIdentity.h"
#include "../src/IssueBurnup.h"
#include "../src/IssueStore.h"
#include "../src/MirrorCrypto.h"
#include "../src/PrivateMirrorStore.h"
#include "../src/NetworkBackoff.h"
#include "../src/ProjectStore.h"
#include "../src/PullAiReview.h"
#include "../src/PullReviewModel.h"
#include "../src/PullStore.h"
#include "../src/ReferenceLinks.h"
#include "../src/RepoContributionSnapshot.h"
#include "../src/RepoContributionSnapshotInternal.h"
#include "../src/RepoSecurity.h"
#include "../src/RoomCrypto.h"
#include "../src/StrictGitReader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <openssl/evp.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (condition) {
        qInfo("PASS: %s", what);
    } else {
        qCritical("FAIL: %s", what);
        ++failures;
    }
}

class StubNetworkReply : public QNetworkReply
{
public:
    StubNetworkReply(const QNetworkRequest &request,
                     QNetworkAccessManager::Operation op, int status,
                     const QByteArray &body, QObject *parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status >= 400)
            setError(QNetworkReply::ContentOperationNotPermittedError,
                     QStringLiteral("HTTP %1").arg(status));
        setOpenMode(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }

    void abort() override {}

    qint64 bytesAvailable() const override
    {
        return (m_body.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (m_offset >= m_body.size())
            return -1;
        const qint64 n = qMin(maxSize, qint64(m_body.size() - m_offset));
        memcpy(data, m_body.constData() + m_offset, size_t(n));
        m_offset += n;
        return n;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class StubBackoffNetworkAccessManager : public BackoffNetworkAccessManager
{
public:
    QList<int> responseStatuses;
    QStringList seenPaths;

protected:
    QNetworkReply *createNetworkRequest(Operation op,
                                        const QNetworkRequest &request,
                                        QIODevice *) override
    {
        seenPaths.append(request.url().path());
        const int status =
            responseStatuses.isEmpty() ? 200 : responseStatuses.takeFirst();
        const QByteArray body =
            status == 429 ? QByteArray("{\"error\":\"rate_limited\"}") : QByteArray();
        return new StubNetworkReply(request, op, status, body, this);
    }
};

// Verify a base64url Ed25519 signature against a base64url raw public key, the
// same encoding ForkMeshIdentity uses, so the test mirrors the worker's
// ed25519_verify rather than trusting the signer's own code path.
bool verifyEd25519(const QString &pubB64Url, const QString &sigB64Url,
                   const QByteArray &message)
{
    const QByteArray rawKey = QByteArray::fromBase64(
        pubB64Url.toLatin1(), QByteArray::Base64UrlEncoding);
    const QByteArray sig = QByteArray::fromBase64(
        sigB64Url.toLatin1(), QByteArray::Base64UrlEncoding);
    if (rawKey.size() != 32 || sig.size() != 64)
        return false;
    EVP_PKEY *key = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char *>(rawKey.constData()), rawKey.size());
    if (!key)
        return false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = ctx &&
              EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
              EVP_DigestVerify(
                  ctx, reinterpret_cast<const unsigned char *>(sig.constData()),
                  sig.size(),
                  reinterpret_cast<const unsigned char *>(message.constData()),
                  message.size()) == 1;
    if (ctx)
        EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

bool runTestGit(const QString &dir, const QStringList &args,
                QByteArray *output = nullptr,
                const QProcessEnvironment *environment = nullptr)
{
    QProcess process;
    if (environment)
        process.setProcessEnvironment(*environment);
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), dir} + args);
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool runTestGitInput(const QString &dir, const QStringList &args,
                     const QByteArray &input)
{
    QProcess process;
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), dir} + args);
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool writeTestFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(contents) == contents.size();
}

bool commitTestTree(const QString &dir, const QString &message,
                    const QString &timestamp, const QString &name,
                    const QString &email,
                    const QString &committerTimestamp = QString())
{
    if (!runTestGit(dir, {QStringLiteral("add"), QStringLiteral("-A")}))
        return false;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_AUTHOR_NAME"), name);
    environment.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), email);
    environment.insert(QStringLiteral("GIT_AUTHOR_DATE"), timestamp);
    environment.insert(QStringLiteral("GIT_COMMITTER_NAME"), name);
    environment.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), email);
    environment.insert(QStringLiteral("GIT_COMMITTER_DATE"),
                       committerTimestamp.isEmpty() ? timestamp
                                                    : committerTimestamp);
    return runTestGit(dir,
                      {QStringLiteral("commit"), QStringLiteral("-q"),
                       QStringLiteral("-m"), message},
                      nullptr, &environment);
}

QString testGitHead(const QString &dir)
{
    QByteArray output;
    if (!runTestGit(dir,
                    {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                    &output))
        return {};
    return QString::fromUtf8(output).trimmed();
}

QJsonArray contributionRow(const RepoContributionSnapshot &snapshot,
                           const QString &date, const QString &actor)
{
    for (const QJsonValue &value : snapshot.payload.value("days").toArray()) {
        const QJsonArray row = value.toArray();
        if (row.size() == 6 && row.at(0).toString() == date &&
            row.at(1).toString() == actor)
            return row;
    }
    return {};
}

qint64 utcMs(const QString &iso)
{
    return QDateTime::fromString(iso, Qt::ISODate).toMSecsSinceEpoch();
}

QByteArray legacyPullCanonical(const PullRequest &pull)
{
    const QChar nul(QChar::Null);
    const QString content = pull.title + nul + pull.base + nul + pull.head +
                            nul + pull.patch;
    const QByteArray contentHash =
        QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256)
            .toHex();
    return QByteArrayLiteral("forkmesh-pull-event-v1\n") +
           pull.author.toUtf8() + QByteArrayLiteral("\n") +
           QByteArray::number(pull.ts) + QByteArrayLiteral("\n") +
           contentHash;
}

} // namespace

int main(int argc, char *argv[])
{
    // A small machine-readable mode used by the cross-runtime CTest.  It emits
    // the exact random envelope produced by Qt; the Python side passes that
    // object verbatim through the Worker's real validator.
    if (argc == 2 &&
        QByteArray(argv[1]) == QByteArrayLiteral("--emit-owner-envelope")) {
        const MirrorCrypto::Identity owner =
            MirrorCrypto::generateIdentity();
        const QByteArray plaintext =
            QByteArrayLiteral("agent E2EE cross-runtime contract payload");
        const QJsonObject envelope = MirrorCrypto::sealOwnerPayload(
            plaintext, owner.publicBundle());
        if (!owner.isValid() || envelope.isEmpty())
            return 2;
        const QByteArray output =
            QJsonDocument(QJsonObject{
                              {"envelope", envelope},
                              {"keyId", owner.keyId()},
                              {"plaintext",
                               QString::fromLatin1(plaintext.toBase64())},
                          })
                .toJson(QJsonDocument::Compact);
        if (std::fwrite(output.constData(), 1, size_t(output.size()), stdout) !=
            size_t(output.size()))
            return 3;
        return 0;
    }

    QTemporaryDir dataDir;
    if (!dataDir.isValid()) {
        qCritical("FAIL: could not create temporary data directory");
        return 1;
    }
    qputenv("XDG_DATA_HOME", dataDir.path().toUtf8());

    QCoreApplication app(argc, argv);
    app.setApplicationName("ForkMeshCryptoTest");
    app.setOrganizationName("ForkMesh");

    {
        const QString linked = ReferenceLinks::linkifyMarkdownReferences(
            "See #154, PR #7, pull request #8, abcdef1, and "
            "forkmesh://pull/owner/repo/7#open.");
        check(linked.contains("[#154](fm-issue:154)"),
              "plain # references render as issue links");
        check(linked.contains("[PR #7](fm-pull:7)"),
              "explicit PR references render as pull links");
        check(linked.contains("[pull request #8](fm-pull:8)"),
              "explicit pull request references render as pull links");
        check(linked.contains("[abcdef1](fm-commit:abcdef1)"),
              "pasted commit hashes render as commit links");
        check(linked.contains("[forkmesh://pull/owner/repo/7#open]"
                              "(forkmesh://pull/owner/repo/7#open)"),
              "pasted ForkMesh deep links render as clickable links");
    }

    {
        const QString input =
            "Already [#154](https://example.test/issues/154), `#155`, "
            "https://example.test/#156\n```\n#157 abcdef1\n```";
        const QString linked = ReferenceLinks::linkifyMarkdownReferences(input);
        check(linked == input,
              "reference linker skips existing links, URLs, inline code, and code blocks");
    }

    {
        // Bounded chat history (issue #428): a mirror node must not retain
        // unlimited chat frames — or huge file payloads — in RAM.
        QList<QJsonObject> history;
        QStringList evicted;
        const int overflow = ChatHistoryLimits::kMaxEntriesPerChannel + 25;
        for (int i = 0; i < overflow; ++i) {
            QJsonObject msg{{"id", QStringLiteral("m%1").arg(i)},
                            {"channel", QStringLiteral("#general")},
                            {"text", QStringLiteral("hello %1").arg(i)}};
            evicted += ChatHistoryLimits::appendBounded(history, msg);
        }
        check(history.size() == ChatHistoryLimits::kMaxEntriesPerChannel,
              "chat history keeps at most kMaxEntriesPerChannel entries");
        check(evicted.size() == 25 && evicted.first() == "m0",
              "chat history evicts oldest entries first and reports their ids");
        check(history.last().value("id").toString() ==
                  QStringLiteral("m%1").arg(overflow - 1),
              "chat history keeps the newest entry after eviction");

        // A handful of large file messages must not pin unbounded RAM: the
        // char budget evicts older entries even when the count cap is far off.
        QList<QJsonObject> fileHistory;
        const QString bigPayload(ChatHistoryLimits::kMaxCharsPerChannel / 2, 'A');
        QStringList fileEvicted;
        for (int i = 0; i < 6; ++i) {
            QJsonObject msg{{"id", QStringLiteral("f%1").arg(i)},
                            {"channel", QStringLiteral("#general")},
                            {"fileName", QStringLiteral("blob.bin")},
                            {"file", bigPayload}};
            fileEvicted += ChatHistoryLimits::appendBounded(fileHistory, msg);
        }
        qsizetype totalChars = 0;
        for (const QJsonObject &entry : std::as_const(fileHistory))
            totalChars += ChatHistoryLimits::entryCost(entry);
        check(totalChars <= ChatHistoryLimits::kMaxCharsPerChannel,
              "chat history stays within the per-channel char budget");
        check(!fileEvicted.isEmpty() && fileEvicted.first() == "f0",
              "char budget evicts oldest file messages first");

        // One message bigger than the whole budget is kept, but without its
        // file body, so a single huge transfer can't pin the budget's worth.
        QList<QJsonObject> oversized;
        QJsonObject huge{{"id", QStringLiteral("huge")},
                         {"channel", QStringLiteral("#general")},
                         {"fileName", QStringLiteral("huge.bin")},
                         {"file", QString(ChatHistoryLimits::kMaxCharsPerChannel + 1,
                                          'B')}};
        ChatHistoryLimits::appendBounded(oversized, huge);
        check(oversized.size() == 1 && !oversized.first().contains("file") &&
                  oversized.first().value("fileName").toString() == "huge.bin",
              "oversized file payloads are stripped from stored history");
    }

    {
        // Transient visitors (adhoc #404): a browser guest is forgotten after
        // ten idle minutes, while nodes and accounts keep their offline row.
        using namespace ChatVisitorPresence;
        check(isTransientVisitor(QStringLiteral("guest"), QStringLiteral("jett")),
              "an advertised guest account kind marks a transient visitor");
        check(isTransientVisitor(QString(), QStringLiteral("Guest 1667")) &&
                  isTransientVisitor(QString(), QStringLiteral("World Guest f49ab8")) &&
                  isTransientVisitor(QString(),
                                     QString::fromUtf8("World visitor \xC2\xB7 jett")),
              "guest and World-visitor names mark a transient visitor");
        check(!isTransientVisitor(QStringLiteral("node"), QStringLiteral("mirror-1")) &&
                  !isTransientVisitor(QStringLiteral("user"), QStringLiteral("jett")) &&
                  !isTransientVisitor(QString(), QStringLiteral("guesthouse")),
              "nodes, accounts, and guest-lookalike names are not visitors");

        const qint64 now = 1700000000000LL;
        check(!visitorIsIdle(now - kVisitorIdleMs + 1000, now),
              "a visitor seen inside the idle window is kept");
        check(visitorIsIdle(now - kVisitorIdleMs - 1000, now),
              "a visitor silent past the idle window is forgotten");
        check(!visitorIsIdle(0, now),
              "a visitor with no sighting yet is left alone");
    }

    {
        const QString input =
            "Already [#154][issue], ``#155 abcdef1``, and a definition.\n"
            "\n"
            "[issue]: forkmesh://issue/owner/repo/154";
        const QString linked = ReferenceLinks::linkifyMarkdownReferences(input);
        check(linked == input,
              "reference linker skips reference-style links and multi-backtick code");
    }

    ForkMeshIdentity identity;
    const bool identityLoaded = identity.load();
    if (!identityLoaded)
        qCritical("Identity load error: %s", qPrintable(identity.errorString()));
    check(identityLoaded, "Ed25519 identity loads or generates");
    check(identity.isValid(), "Ed25519 identity is valid");
    check(!identity.publicKey().isEmpty(), "public key is exported");

    const QJsonObject signedProfile =
        identity.signedProfile("node-a", "mainnode:node-a",
                               "bitcoincash:qp...");
    check(signedProfile.value("kind").toString() == "forkmesh.identity",
          "identity profile has the expected kind");
    check(signedProfile.value("profile").toObject().value("pubkey").toString() ==
              identity.publicKey(),
          "signed profile includes the identity public key");
    check(!signedProfile.value("signature").toString().isEmpty(),
          "signed profile includes a signature");

    // Admin-moderation deletes are signed with signData() and authenticated by
    // every peer via ForkMeshIdentity::verifySignature against the signer's key.
    {
        const QByteArray canonical =
            QByteArrayLiteral("forkmesh-admin-delete-v1\n#general\nmsg-1\n") +
            identity.publicKey().toUtf8() + QByteArrayLiteral("\n1700000000000");
        const QString sig = identity.signData(canonical);
        check(!sig.isEmpty(), "admin-delete canonical signs");
        check(ForkMeshIdentity::verifySignature(identity.publicKey(), sig, canonical),
              "verifySignature accepts a valid admin-delete signature");
        check(!ForkMeshIdentity::verifySignature(identity.publicKey(), sig,
                                                 canonical + "x"),
              "verifySignature rejects a tampered payload");
        check(!ForkMeshIdentity::verifySignature(identity.publicKey(),
                                                 sig.left(sig.size() - 2) + "AA",
                                                 canonical),
              "verifySignature rejects a tampered signature");
    }

    // The #welcome greeting must survive a restart (a fresh ForkMeshIdentity
    // reloading the same on-disk key), which was the point of colocating the
    // flag with the identity key instead of QSettings (adhoc #109).
    {
        identity.markWelcomeAnnounced();
        ForkMeshIdentity reloaded;
        check(reloaded.load(), "identity reloads from the same on-disk key");
        check(reloaded.hasAnnouncedWelcome(),
              "welcome-announced flag survives a fresh identity load");
    }

    // --- Identity key backup, export & rotation (issue #368) -------------
    // Losing the machine must not mean losing the identity: the private key can
    // be exported to a passphrase-encrypted keyfile and imported on a new node,
    // reproducing the exact same public key (hence every signature binding).
    {
        const QString keyfile = identity.exportEncryptedKeyfile("correct horse");
        check(!keyfile.isEmpty(), "identity exports to an encrypted keyfile");
        check(keyfile.contains("forkmesh.identity.keyfile"),
              "keyfile carries the age-style envelope kind");
        check(ForkMeshIdentity::keyfilePublicKey(keyfile) == identity.publicKey(),
              "keyfilePublicKey reads the pubkey without the passphrase");

        // Wrong passphrase must fail (GCM tag mismatch), never yield a key.
        ForkMeshIdentity wrongPass;
        check(!wrongPass.importEncryptedKeyfile(keyfile, "WRONG passphrase"),
              "importing with the wrong passphrase fails");

        // Correct passphrase restores the same key: identical public key and a
        // signature that verifies against the original identity's public key.
        ForkMeshIdentity restored;
        check(restored.importEncryptedKeyfile(keyfile, "correct horse"),
              "importing with the correct passphrase restores the identity");
        check(restored.publicKey() == identity.publicKey(),
              "restored identity reproduces the original public key");
        const QByteArray probe = QByteArrayLiteral("forkmesh-backup-probe");
        const QString probeSig = restored.signData(probe);
        check(ForkMeshIdentity::verifySignature(identity.publicKey(), probeSig, probe),
              "the restored key produces signatures valid under the original pubkey");
        check(restored.hasBackedUp(),
              "a restored key is marked as already backed up");

        check(ForkMeshIdentity::keyfilePublicKey("not a keyfile").isEmpty(),
              "keyfilePublicKey rejects non-keyfile text");

        // Rotation: the old key signs a successor pubkey. The record verifies
        // against the old key and is bound to that exact successor.
        const QString successor =
            QStringLiteral("Zm9ya21lc2gtc3VjY2Vzc29yLWtleS0zMi1ieXRlcw");
        const QJsonObject rotate = identity.signRotation(successor);
        check(rotate.value("kind").toString() == "forkmesh.rotate",
              "rotation record has the expected kind");
        check(rotate.value("oldPubkey").toString() == identity.publicKey() &&
                  rotate.value("newPubkey").toString() == successor,
              "rotation record binds the old and successor keys");
        check(ForkMeshIdentity::verifyRotation(rotate),
              "a well-formed rotation record verifies against the old key");
        QJsonObject tampered = rotate;
        tampered["newPubkey"] = QStringLiteral("attacker-key");
        check(!ForkMeshIdentity::verifyRotation(tampered),
              "a rotation record retargeted to a different successor is rejected");
    }

    RoomCrypto crypto("repo:mainnode/forkmesh:room:general",
                      "correct horse battery staple");
    check(crypto.isValid(), "mainnode room crypto key derives");
    const QJsonObject secret{{"type", "chat"}, {"text", "encrypted hello"}};
    const QJsonObject encrypted = crypto.encryptObject(secret);
    check(encrypted.value("kind").toString() == "cipher" &&
              !encrypted.value("body").toString().isEmpty(),
          "room crypto encrypts to an opaque envelope");
    check(crypto.decryptObject(encrypted).value("text").toString() ==
              "encrypted hello",
          "room crypto decrypts a valid envelope");

    RoomCrypto wrongCrypto("repo:mainnode/forkmesh:room:general",
                           "wrong passphrase");
    check(wrongCrypto.decryptObject(encrypted).isEmpty(),
          "room crypto rejects the wrong passphrase");

    // --- Passphrase-free shared rooms (baked-in app key) -----------------
    // Two independent instances built only from the room name must interoperate,
    // so every node joins the same shared room with no passphrase.
    RoomCrypto sharedA("general");
    RoomCrypto sharedB("general");
    check(sharedA.isValid(), "passphrase-free room key derives");
    const QJsonObject sharedMsg{{"type", "chat"}, {"text", "hello shared"}};
    const QJsonObject sharedEnv = sharedA.encryptObject(sharedMsg);
    check(sharedEnv.value("kind").toString() == "cipher",
          "shared room encrypts to an opaque envelope");
    check(sharedB.decryptObject(sharedEnv).value("text").toString() == "hello shared",
          "a second node decrypts the shared room with the baked-in key");
    check(RoomCrypto("random").decryptObject(sharedEnv).isEmpty(),
          "a different room name does not decrypt the envelope");

    // --- Cove vault crypto (password + per-cove salt) --------------------
    // A 256-bit AES key derived from a shared password; the GCM tag is the
    // password check. Two crypto objects built from the same password + salt
    // must interoperate, a wrong password must fail to decrypt, and the same
    // password with a different salt must derive a different key.
    {
        const QByteArray coveSalt = CoveCrypto::randomSalt();
        check(coveSalt.size() == 16, "cove salt is 16 random bytes");
        CoveCrypto coveA("team-shared-password", coveSalt, CoveCrypto::defaultRounds());
        check(coveA.isValid(), "cove crypto key derives from password + salt");
        const QByteArray secret = QByteArrayLiteral("prod db password: hunter2");
        const QJsonObject sealed = coveA.encrypt(secret);
        check(sealed.contains("nonce") && sealed.contains("tag") &&
                  !sealed.value("body").toString().isEmpty(),
              "cove crypto encrypts to a nonce/tag/body envelope");
        CoveCrypto coveB("team-shared-password", coveSalt, CoveCrypto::defaultRounds());
        check(coveB.decrypt(sealed) == secret,
              "a teammate with the same password decrypts the cove payload");
        CoveCrypto coveWrong("WRONG-password", coveSalt, CoveCrypto::defaultRounds());
        check(coveWrong.decrypt(sealed).isEmpty(),
              "the wrong password fails the GCM tag and decrypts to nothing");
        CoveCrypto coveOtherSalt("team-shared-password", CoveCrypto::randomSalt(),
                                 CoveCrypto::defaultRounds());
        check(coveOtherSalt.decrypt(sealed).isEmpty(),
              "the same password with a different salt cannot decrypt the cove");
    }

    // --- Private-mirror crypto (hybrid X25519 + ML-KEM-768) --------------
    // Issue #362: a private repo is mirrored as one opaque encrypted archive.
    // The owner encrypts the archive under a random content key and wraps that
    // key to each collaborator's hybrid identity. Only a wrapped recipient can
    // recover the archive; a non-recipient, a tampered ciphertext, or one KEM
    // half swapped between wraps must all fail closed.
    {
        MirrorCrypto::Identity owner = MirrorCrypto::generateIdentity();
        MirrorCrypto::Identity alice = MirrorCrypto::generateIdentity();
        MirrorCrypto::Identity mallory = MirrorCrypto::generateIdentity();
        check(owner.isValid() && alice.isValid() && mallory.isValid(),
              "hybrid identities generate with X25519 + ML-KEM-768 keypairs");
        check(owner.x25519Pub.size() == 32 && owner.mlkemPub.size() == 1184 &&
                  owner.mlkemPriv.size() == 2400,
              "identity key sizes match X25519 and ML-KEM-768");
        check(MirrorCrypto::publicKeyId(owner.publicBundle()) == owner.keyId() &&
                  owner.keyId() != alice.keyId(),
              "public bundle yields a stable, identity-specific key id");

        const QByteArray privateAgentPayload =
            QJsonDocument(QJsonObject{
                              {"id", 42},
                              {"prompt", "private owner-only steering prompt"},
                              {"transcript", "private transcript tail"},
                          })
                .toJson(QJsonDocument::Compact);
        QString ownerSealError;
        const QJsonObject ownerEnvelope = MirrorCrypto::sealOwnerPayload(
            privateAgentPayload, owner.publicBundle(), &ownerSealError);
        const QJsonArray ownerRecipients =
            ownerEnvelope.value("recipients").toArray();
        const QJsonObject ownerWrap =
            ownerRecipients.isEmpty() ? QJsonObject{}
                                      : ownerRecipients.first().toObject();
        auto fromUrl = [](const QJsonValue &value) {
            return QByteArray::fromBase64(
                value.toString().toLatin1(),
                QByteArray::Base64UrlEncoding |
                    QByteArray::AbortOnBase64DecodingErrors);
        };
        check(!ownerEnvelope.isEmpty() && ownerSealError.isEmpty() &&
                  ownerEnvelope.value("kind").toString() ==
                      "forkmesh.owner-sealed" &&
                  ownerEnvelope.value("alg").toString() ==
                      "x25519+mlkem768/aes256gcm" &&
                  !ownerEnvelope.contains("kid") &&
                  ownerRecipients.size() == 1 &&
                  ownerWrap.value("kid").toString() == owner.keyId(),
              "owner control payload uses the single-recipient Worker contract");
        check(fromUrl(ownerEnvelope.value("nonce")).size() == 12 &&
                  fromUrl(ownerEnvelope.value("tag")).size() == 16 &&
                  fromUrl(ownerWrap.value("x25519")).size() == 32 &&
                  fromUrl(ownerWrap.value("mlkem768")).size() == 1088 &&
                  fromUrl(ownerWrap.value("nonce")).size() == 12 &&
                  fromUrl(ownerWrap.value("tag")).size() == 16 &&
                  fromUrl(ownerWrap.value("key")).size() == 32,
              "owner envelope carries complete bounded X25519, ML-KEM and GCM framing");
        check(MirrorCrypto::ownerPayloadKeyId(ownerEnvelope) == owner.keyId() &&
                  MirrorCrypto::openOwnerPayload(
                      ownerEnvelope, owner, &ownerSealError) ==
                      privateAgentPayload &&
                  ownerSealError.isEmpty(),
              "owner payload seal/open round-trips on the desktop identity");
        check(MirrorCrypto::openOwnerPayload(
                  ownerEnvelope, alice, &ownerSealError).isEmpty() &&
                  !ownerSealError.isEmpty(),
              "another local identity cannot open owner-only agent data");
        QJsonObject tamperedOwnerEnvelope = ownerEnvelope;
        QByteArray ownerBody =
            fromUrl(ownerEnvelope.value("body"));
        ownerBody[0] = char(ownerBody.at(0) ^ 0x01);
        tamperedOwnerEnvelope["body"] =
            QString::fromLatin1(
                ownerBody.toBase64(
                    QByteArray::Base64UrlEncoding |
                    QByteArray::OmitTrailingEquals));
        check(MirrorCrypto::openOwnerPayload(
                  tamperedOwnerEnvelope, owner).isEmpty(),
              "tampered owner-only agent ciphertext fails authentication");

        const QByteArray archive =
            QByteArrayLiteral("PACK\x00\x02") + QByteArray(50000, '\x7f') +
            QByteArrayLiteral("private repo pack bytes");
        QString sealErr;
        const QJsonObject envelope = MirrorCrypto::sealArchive(
            archive, {owner.publicBundle(), alice.publicBundle()}, &sealErr);
        check(!envelope.isEmpty() && sealErr.isEmpty() &&
                  envelope.value("alg").toString() == "x25519+mlkem768/aes256gcm",
              "sealArchive produces a hybrid-KEM envelope for two recipients");
        check(envelope.value("recipients").toArray().size() == 2,
              "the envelope wraps the content key once per recipient");
        check(!envelope.value("body").toString().toUtf8().contains(
                  QByteArrayLiteral("private repo pack bytes").toBase64()),
              "the archive body is ciphertext, not the plaintext pack");

        QString openErr;
        check(MirrorCrypto::openArchive(envelope, owner, &openErr) == archive &&
                  openErr.isEmpty(),
              "the owner decrypts the whole-mirror archive from its wrap");
        check(MirrorCrypto::openArchive(envelope, alice) == archive,
              "a wrapped collaborator decrypts the same archive");
        check(MirrorCrypto::openArchive(envelope, mallory, &openErr).isEmpty() &&
                  !openErr.isEmpty(),
              "a non-recipient identity cannot recover the archive");

        // Flip one byte of the archive ciphertext: GCM must reject it.
        QJsonObject tamperedBody = envelope;
        QByteArray body = QByteArray::fromBase64(
            envelope.value("body").toString().toLatin1());
        body[10] = char(body.at(10) ^ 0x01);
        tamperedBody["body"] = QString::fromLatin1(body.toBase64());
        check(MirrorCrypto::openArchive(tamperedBody, alice).isEmpty(),
              "a tampered archive body fails the GCM tag");

        // Swap the ML-KEM ciphertext of alice's wrap for mallory's: the HKDF
        // info binds both KEM outputs, so the mismatched KEK must fail closed.
        const QJsonObject solo = MirrorCrypto::sealArchive(
            archive, {mallory.publicBundle()}, nullptr);
        QJsonArray recips = envelope.value("recipients").toArray();
        QJsonObject aliceWrap;
        for (const QJsonValue &rv : recips)
            if (rv.toObject().value("kid").toString() == alice.keyId())
                aliceWrap = rv.toObject();
        aliceWrap["mlkem768"] = solo.value("recipients").toArray()
                                    .at(0).toObject().value("mlkem768");
        QJsonArray swapped;
        for (const QJsonValue &rv : recips) {
            if (rv.toObject().value("kid").toString() == alice.keyId())
                swapped.append(aliceWrap);
            else
                swapped.append(rv);
        }
        QJsonObject swappedEnv = envelope;
        swappedEnv["recipients"] = swapped;
        check(MirrorCrypto::openArchive(swappedEnv, alice).isEmpty(),
              "swapping one hybrid-KEM half between wraps fails to decrypt");

        check(MirrorCrypto::sealArchive(archive, {}, &sealErr).isEmpty() &&
                  !sealErr.isEmpty(),
              "sealing to zero recipients is rejected");
        check(MirrorCrypto::sealArchive(
                  archive,
                  {owner.publicBundle(), owner.publicBundle()},
                  &sealErr).isEmpty(),
              "sealing rejects duplicate private-mirror recipients");
        QJsonObject malformedBundle = owner.publicBundle();
        malformedBundle["v"] = 9;
        check(MirrorCrypto::publicKeyId(malformedBundle).isEmpty(),
              "malformed recipient bundles do not produce a key id");
    }

    // --- Durable opaque private-mirror replicas -------------------------
    // Ciphertext is stored under a random id with owner-only permissions. A
    // recipient removal rotates the content key and prevents that identity
    // from opening the new epoch; private identity halves are never persisted
    // alongside the replica.
    {
        QTemporaryDir store;
        MirrorCrypto::Identity owner = MirrorCrypto::generateIdentity();
        MirrorCrypto::Identity alice = MirrorCrypto::generateIdentity();
        MirrorCrypto::Identity bob = MirrorCrypto::generateIdentity();
        const QByteArray archive =
            QByteArrayLiteral("super-private-project-name\0PACK") +
            QByteArray(8192, '\x4a');
        QString error;
        const QString opaqueId = PrivateMirrorStore::createReplica(
            store.path(), archive,
            {owner.publicBundle(), alice.publicBundle()}, &error);
        check(PrivateMirrorStore::isOpaqueId(opaqueId) && error.isEmpty(),
              "private replica is created under an unguessable opaque id");

        const QStringList files =
            QDir(store.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
        check(files == QStringList{opaqueId + ".fm-private"} &&
                  !files.value(0).contains("private-project"),
              "private replica filename discloses no owner or repository name");
        QFile encryptedFile(
            QDir(store.path()).filePath(opaqueId + ".fm-private"));
        check(encryptedFile.open(QIODevice::ReadOnly),
              "encrypted private replica can be read as ciphertext");
        QByteArray storedBytes = encryptedFile.readAll();
        encryptedFile.close();
        check(!storedBytes.contains("super-private-project-name") &&
                  !storedBytes.contains(owner.x25519Priv.toBase64()) &&
                  !storedBytes.contains(owner.mlkemPriv.toBase64()),
              "replica storage contains no plaintext name or private identity keys");
        const QFileDevice::Permissions permissions =
            QFileInfo(encryptedFile).permissions();
        check(!(permissions &
                (QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                 QFileDevice::ExeGroup | QFileDevice::ReadOther |
                 QFileDevice::WriteOther | QFileDevice::ExeOther)),
              "private replica file permissions exclude group and other users");

        PrivateMirrorStore::Metadata before;
        QStringList expectedRecipients{alice.keyId(), owner.keyId()};
        expectedRecipients.sort();
        check(PrivateMirrorStore::inspectReplica(
                  store.path(), opaqueId, &before, &error) &&
                  before.keyEpoch == 1 &&
                  before.recipientKeyIds == expectedRecipients,
              "private replica metadata authenticates epoch and recipients");
        check(PrivateMirrorStore::openReplica(
                  store.path(), opaqueId, owner, nullptr, &error) == archive &&
                  PrivateMirrorStore::openReplica(
                      store.path(), opaqueId, alice, nullptr, &error) == archive,
              "only configured recipients decrypt the persisted private replica");

        check(PrivateMirrorStore::rotateRecipients(
                  store.path(), opaqueId, owner,
                  {owner.publicBundle(), bob.publicBundle()}, &error),
              "recipient revocation atomically rotates the private replica key");
        PrivateMirrorStore::Metadata after;
        check(PrivateMirrorStore::inspectReplica(
                  store.path(), opaqueId, &after, &error) &&
                  after.keyEpoch == 2 &&
                  after.ciphertextSha256 != before.ciphertextSha256,
              "private replica rotation advances its authenticated key epoch");
        check(PrivateMirrorStore::openReplica(
                  store.path(), opaqueId, alice, nullptr, &error).isEmpty() &&
                  !error.isEmpty(),
              "a revoked recipient cannot decrypt the rotated replica");
        check(PrivateMirrorStore::openReplica(
                  store.path(), opaqueId, bob, nullptr, &error) == archive,
              "a newly authorized recipient decrypts the rotated replica");
        check(PrivateMirrorStore::openReplica(
                  store.path(), "../not-an-id", owner, nullptr, &error).isEmpty(),
              "private replica lookup rejects path traversal");

        QFile corrupt(
            QDir(store.path()).filePath(opaqueId + ".fm-private"));
        check(corrupt.open(QIODevice::ReadWrite),
              "private replica opens for local corruption test");
        QJsonObject stored =
            QJsonDocument::fromJson(corrupt.readAll()).object();
        QJsonObject envelope = stored.value("envelope").toObject();
        QByteArray body =
            QByteArray::fromBase64(envelope.value("body").toString().toLatin1());
        body[0] = char(body.at(0) ^ 0x20);
        envelope["body"] = QString::fromLatin1(body.toBase64());
        stored["envelope"] = envelope;
        corrupt.resize(0);
        corrupt.write(QJsonDocument(stored).toJson(QJsonDocument::Compact));
        corrupt.close();
        check(!PrivateMirrorStore::inspectReplica(
                  store.path(), opaqueId, &after, &error),
              "ciphertext corruption is rejected before private-replica decryption");
    }

    const QString deviceProofKey = QStringLiteral(
        "ERERERERERERERERERERERERERERERERERERERERERE");
    const QString deviceProofTimestamp = QStringLiteral("1783000000000");
    const QByteArray deviceProofCanonical =
        ForkMeshIdentity::deviceBindCanonical(
            QStringLiteral(" Alice-Node "), deviceProofKey,
            deviceProofTimestamp);
    check(deviceProofCanonical ==
              QByteArrayLiteral("forkmesh-device-bind-v1\n"
                                "alice-node\n"
                                "ERERERERERERERERERERERERERERERERERERERERERE\n"
                                "1783000000000"),
          "device-bind canonical matches the cross-language vector");
    const QString deviceProofSignature =
        identity.signData(ForkMeshIdentity::deviceBindCanonical(
            QStringLiteral("Alice-Node"), identity.publicKey(),
            deviceProofTimestamp));
    check(ForkMeshIdentity::verifySignature(
              identity.publicKey(), deviceProofSignature,
              ForkMeshIdentity::deviceBindCanonical(
                  QStringLiteral("alice-node"), identity.publicKey(),
                  deviceProofTimestamp)) &&
              !ForkMeshIdentity::verifySignature(
                  identity.publicKey(), deviceProofSignature,
                  ForkMeshIdentity::deviceBindCanonical(
                      QStringLiteral("other-node"), identity.publicKey(),
                      deviceProofTimestamp)),
          "device-bind proof signs the exact normalized account canonical");

    check(AccountCapability::allowsPasswordOnlyFallback(
              QStringLiteral("pubkey_mismatch")) &&
              AccountCapability::allowsPasswordOnlyFallback(
                  QStringLiteral("device_proof_required")) &&
              AccountCapability::allowsPasswordOnlyFallback(
                  QStringLiteral("device_key_conflict")) &&
              !AccountCapability::allowsPasswordOnlyFallback(
                  QStringLiteral("invalid_credentials")),
          "only device-binding errors allow safe password-only login fallback");
    check(AccountCapability::ownerSigningAllowed(
              true, true, QStringLiteral("Alice-Node")) &&
              !AccountCapability::ownerSigningAllowed(
                  true, true, QString()) &&
              !AccountCapability::ownerSigningAllowed(
                  true, false, QStringLiteral("Alice-Node")) &&
              !AccountCapability::ownerSigningAllowed(
                  false, true, QStringLiteral("Alice-Node")) &&
              AccountCapability::ownerSigningAllowed(
                  true, true, QStringLiteral("Alice-Node"),
                  QStringLiteral(" alice-node ")) &&
              !AccountCapability::ownerSigningAllowed(
                  true, true, QStringLiteral("alice-node"),
                  QStringLiteral("other-node")),
          "owner signing requires an active key-capable desktop session");
    check(AccountCapability::persistedMarkerMatches(
              QStringLiteral("Alice-Node"), QStringLiteral("primary-key"),
              QStringLiteral(" alice-node "), QStringLiteral("primary-key")) &&
              !AccountCapability::persistedMarkerMatches(
                  QStringLiteral("alice-node"), QStringLiteral("primary-key"),
                  QStringLiteral("other-node"), QStringLiteral("primary-key")) &&
              !AccountCapability::persistedMarkerMatches(
                  QStringLiteral("alice-node"), QStringLiteral("primary-key"),
                  QStringLiteral("alice-node"), QStringLiteral("rotated-key")) &&
              !AccountCapability::persistedMarkerMatches(
                  QStringLiteral("alice-node"), QStringLiteral(" primary-key "),
                  QStringLiteral("alice-node"), QStringLiteral("primary-key")) &&
              !AccountCapability::persistedMarkerMatches(
                  QString(), QString(), QStringLiteral("alice-node"),
                  QStringLiteral("primary-key")),
          "desktop capability persistence is scoped to the exact account and key");

    // --- Issue event signing ---------------------------------------------
    // Pin the canonical byte format so the C++ client, the Python seed
    // generator, and the worker's ed25519_verify all agree.
    IssueEvent vector;
    vector.type = "open";
    vector.author = "TESTPUB";
    vector.ts = 1000;
    vector.title = "Hello";
    vector.body = "World";
    vector.attachments = {"attachments/x.png"};
    const QByteArray expected =
        "forkmesh-issue-event-v1\nopen\n1\nTESTPUB\n1000\n"
        "7b66aa5f19006b73598497bef2f5d6ed94700c48d479279df3ff5eb147fb0513";
    check(IssueStore::canonicalString(1, vector) == expected,
          "issue-event canonical string matches the cross-language vector");

    // "title" event (issue rename): content is just the title. Pin it so the
    // client and the worker's issue_event_content stay byte-identical.
    IssueEvent titleVec;
    titleVec.type = "title";
    titleVec.author = "TESTPUB";
    titleVec.ts = 2000;
    titleVec.title = "Renamed issue";
    const QByteArray expectedTitle =
        "forkmesh-issue-event-v1\ntitle\n3\nTESTPUB\n2000\n"
        "37f2b6516c608aafe45958eabd5bb4c20b9c6765d1eb39a5c27b4588e566ec97";
    check(IssueStore::canonicalString(3, titleVec) == expectedTitle,
          "title-event canonical string matches the cross-language vector");

    // "delete" event (used to delete a comment, or "self" to tombstone the
    // whole issue): content is just the target event id. Pin it so the client
    // and the worker's issue_event_content stay byte-identical.
    IssueEvent deleteVec;
    deleteVec.type = "delete";
    deleteVec.author = "TESTPUB";
    deleteVec.ts = 3000;
    deleteVec.target = "comment-abc123";
    const QByteArray expectedDelete =
        "forkmesh-issue-event-v1\ndelete\n7\nTESTPUB\n3000\n"
        "3f7946e4dbf24af4c78058a30f4221dcf4379c5b62dbf1a3f5f17e8039b6ea95";
    check(IssueStore::canonicalString(7, deleteVec) == expectedDelete,
          "delete-event canonical string matches the cross-language vector");

    // --- PR conversation event signing -----------------------------------
    // Pin the canonical byte format so the C++ client and the worker's
    // verify_pull_comment_event stay byte-identical. The number is bound.
    PullEvent pullComment;
    pullComment.type = "comment";
    pullComment.author = "TESTPUB";
    pullComment.ts = 1000;
    pullComment.body = "Looks good";
    const QByteArray expectedPullComment =
        "forkmesh-pull-comment-v1\ncomment\n5\nTESTPUB\n1000\n"
        "5fc87d339144090b0ad2e192e6a6fe58e98d3a5a062467c7e62049c7d8c3db01";
    check(PullStore::canonicalString(5, pullComment) == expectedPullComment,
          "pull-comment canonical string matches the cross-language vector");

    PullEvent pullReview;
    pullReview.type = "review";
    pullReview.author = "TESTPUB";
    pullReview.ts = 2000;
    pullReview.state = "approved";
    pullReview.body = "LGTM";
    const QByteArray expectedPullReview =
        "forkmesh-pull-comment-v1\nreview\n5\nTESTPUB\n2000\n"
        "b863bbc11dd8fea92da94a7da47f815aceeaa9418483992d0a952273894a0731";
    check(PullStore::canonicalString(5, pullReview) == expectedPullReview,
          "pull-review canonical string matches the cross-language vector");

    PullEvent pullLine;
    pullLine.type = "line-comment";
    pullLine.author = "TESTPUB";
    pullLine.ts = 2500;
    pullLine.path = "src/x.cpp";
    pullLine.side = "new";
    pullLine.line = 42;
    pullLine.body = "needs a guard";
    const QByteArray expectedPullLine =
        "forkmesh-pull-comment-v1\nline-comment\n5\nTESTPUB\n2500\n"
        "a2574c2b392fd0db6b7e4b0d025d4094bb410691dca7686ddf095d63881f4985";
    check(PullStore::canonicalString(5, pullLine) == expectedPullLine,
          "pull line-comment canonical string matches the cross-language vector");

    PullEvent pullThread;
    pullThread.type = "thread-comment";
    pullThread.author = "TESTPUB";
    pullThread.ts = 2600;
    pullThread.threadId = "thread-1";
    pullThread.path = "src/x.cpp";
    pullThread.side = "new";
    pullThread.lineStart = 42;
    pullThread.lineEnd = 44;
    pullThread.body = "Use guard";
    pullThread.suggestionPatch = "@@ -1 +1 @@\n-old\n+new\n";
    const QByteArray expectedPullThread =
        "forkmesh-pull-comment-v1\nthread-comment\n5\nTESTPUB\n2600\n"
        "adbf1313d68a9d32f69734e23b1b7713d6c30dcd77b628513f8b04b5a459fe81";
    check(PullStore::canonicalString(5, pullThread) == expectedPullThread,
          "pull thread-comment canonical string matches the cross-language vector");

    PullEvent pullReply;
    pullReply.type = "thread-reply";
    pullReply.author = "TESTPUB";
    pullReply.ts = 2700;
    pullReply.threadId = "thread-1";
    pullReply.parentId = "event-1";
    pullReply.body = "I pushed a fix";
    const QByteArray expectedPullReply =
        "forkmesh-pull-comment-v1\nthread-reply\n5\nTESTPUB\n2700\n"
        "cd9fc0caa43648948f6a976d570191b9a4d85bee1d984a7aacc483a7487196dd";
    check(PullStore::canonicalString(5, pullReply) == expectedPullReply,
          "pull thread-reply canonical string matches the cross-language vector");

    PullEvent pullThreadState;
    pullThreadState.type = "thread-state";
    pullThreadState.author = "TESTPUB";
    pullThreadState.ts = 2800;
    pullThreadState.threadId = "thread-1";
    pullThreadState.state = "resolved";
    pullThreadState.body = "resolved after update";
    const QByteArray expectedPullThreadState =
        "forkmesh-pull-comment-v1\nthread-state\n5\nTESTPUB\n2800\n"
        "b652054bd993fa340a565f6ec3e0c88dc728c8b69d7fdf723acc6bdccc1c07df";
    check(PullStore::canonicalString(5, pullThreadState) == expectedPullThreadState,
          "pull thread-state canonical string matches the cross-language vector");

    PullEvent pullSuggestionState;
    pullSuggestionState.type = "suggestion-state";
    pullSuggestionState.author = "TESTPUB";
    pullSuggestionState.ts = 2900;
    pullSuggestionState.threadId = "thread-1";
    pullSuggestionState.state = "applied";
    pullSuggestionState.appliedCommit = "abc123def456";
    pullSuggestionState.body = "applied in follow-up";
    const QByteArray expectedPullSuggestionState =
        "forkmesh-pull-comment-v1\nsuggestion-state\n5\nTESTPUB\n2900\n"
        "07ce71665f21bcb839faeb956910a9ca369d79167e6f6e52f60c4d01344f3c52";
    check(PullStore::canonicalString(5, pullSuggestionState) ==
              expectedPullSuggestionState,
          "pull suggestion-state canonical string matches the cross-language vector");

    // --- Discussion event signing ---------------------------------------
    // Pin the canonical byte format so the C++ client and worker verifier
    // stay byte-identical. The discussion number is bound.
    DiscussionEvent openDiscussion;
    openDiscussion.type = "open";
    openDiscussion.author = "TESTPUB";
    openDiscussion.ts = 1000;
    openDiscussion.title = "Welcome";
    openDiscussion.category = "Announcements";
    openDiscussion.body = "Hello discussion";
    const QByteArray expectedDiscussionOpen =
        "forkmesh-discussion-event-v1\nopen\n1\nTESTPUB\n1000\n"
        "8e31495ce2e5559ce11564b67a45710aac9654c0f70b0fcfd0ab08dc51c83ab2";
    check(DiscussionStore::canonicalString(1, openDiscussion) ==
              expectedDiscussionOpen,
          "discussion open canonical string matches the worker vector");
    const QByteArray expectedDiscussionInboxOpen =
        "forkmesh-discussion-event-v1\nopen\n0\nTESTPUB\n1000\n"
        "8e31495ce2e5559ce11564b67a45710aac9654c0f70b0fcfd0ab08dc51c83ab2";
    check(DiscussionStore::canonicalString(0, openDiscussion) ==
              expectedDiscussionInboxOpen,
          "discussion inbox-open canonical string matches the worker vector");

    DiscussionEvent discussionComment;
    discussionComment.type = "comment";
    discussionComment.author = "TESTPUB";
    discussionComment.ts = 2000;
    discussionComment.body = "Reply body";
    const QByteArray expectedDiscussionComment =
        "forkmesh-discussion-event-v1\ncomment\n1\nTESTPUB\n2000\n"
        "b87e74db2baf019fb26d1a764aa329723024c6be7f13e5a92a60690b301bc3e9";
    check(DiscussionStore::canonicalString(1, discussionComment) ==
              expectedDiscussionComment,
          "discussion comment canonical string matches the worker vector");

    DiscussionInboxBackoff inboxBackoff;
    const QString discussionInboxKey =
        "https://forkmesh.com/api/repo/alice/project/discussions";
    check(!inboxBackoff.shouldBackOff(discussionInboxKey, 1000),
          "discussion inbox sync starts without a capability backoff");
    inboxBackoff.markUnsupported(discussionInboxKey, 1000);
    check(inboxBackoff.shouldBackOff(discussionInboxKey, 1000),
          "discussion inbox 404 backs off repeated sync attempts");
    check(inboxBackoff.shouldBackOff(discussionInboxKey, 1000 + 599999),
          "discussion inbox backoff lasts for the cooldown window");
    check(!inboxBackoff.shouldBackOff(discussionInboxKey, 1000 + 600000),
          "discussion inbox backoff expires after the cooldown window");
    inboxBackoff.markUnsupported(discussionInboxKey, 2000);
    inboxBackoff.clear(discussionInboxKey);
    check(!inboxBackoff.shouldBackOff(discussionInboxKey, 2000),
          "successful discussion inbox sync clears the unsupported backoff");

    // --- Exponential poll backoff ----------------------------------------
    // NetworkBackoff spaces out retries after a run of failures. Delay for the
    // nth failure is min(base*2^(n-1), cap) plus <base/8 jitter, so we assert on
    // bounds that hold for any jitter value: still blocked strictly before the
    // base delay, and definitely ready once base + its jitter span has elapsed.
    NetworkBackoff pollBackoff;
    const QString pollKey = "https://forkmesh.com/api/repo/alice/project/pulls";
    check(pollBackoff.ready(pollKey, 0),
          "a fresh poll channel is ready with no backoff");
    pollBackoff.noteFailure(pollKey, 0, 1000, 8000); // 1st failure: ~1000ms
    check(!pollBackoff.ready(pollKey, 999),
          "poll backoff blocks a retry before the base delay elapses");
    check(pollBackoff.ready(pollKey, 1125),
          "poll backoff clears once the base delay (+jitter span) elapses");
    pollBackoff.noteFailure(pollKey, 0, 1000, 8000); // 2nd failure: ~2000ms
    check(!pollBackoff.ready(pollKey, 1999),
          "a second consecutive failure at least doubles the backoff");
    pollBackoff.noteFailure(pollKey, 0, 1000, 8000); // 3rd failure: ~4000ms
    check(!pollBackoff.ready(pollKey, 3999),
          "the backoff keeps growing exponentially with each failure");
    // Independent channels don't inherit each other's backoff.
    check(pollBackoff.ready("https://forkmesh.com/api/accounts/heartbeat", 0),
          "poll backoff is tracked per channel");
    // Many failures stay bounded by the cap, never exploding past it.
    for (int i = 0; i < 20; ++i)
        pollBackoff.noteFailure(pollKey, 0, 1000, 8000);
    check(pollBackoff.ready(pollKey, 9000),
          "poll backoff is capped and does not grow without bound");
    pollBackoff.noteSuccess(pollKey);
    check(pollBackoff.ready(pollKey, 0),
          "a successful poll clears the exponential backoff");

    // --- BackoffNetworkAccessManager: host-wide 429 gate (adhoc #78) -----
    // An in-process reply stub stands in for the relay so createRequest's
    // routing/gating logic runs end-to-end without needing a loopback listener
    // (some CI/sandbox profiles deny bind()). /api/* paths are gated per-host,
    // a 429 starts a cooldown during which further /api/* requests never reach
    // the network handoff, and non-/api/ paths always bypass the gate.
    {
        StubBackoffNetworkAccessManager manager;
        const QString base = QStringLiteral("http://relay.test");
        const auto runRequest = [&](const QString &path) {
            QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(base + path)));
            bool done = false;
            QObject::connect(reply, &QNetworkReply::finished, [&done] { done = true; });
            QElapsedTimer timer;
            timer.start();
            while (!done && timer.elapsed() < 3000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QString err = reply->errorString();
            reply->deleteLater();
            return err;
        };

        manager.responseStatuses.append(429);
        runRequest(QStringLiteral("/api/version"));
        check(manager.seenPaths == QStringList{QStringLiteral("/api/version")},
              "first /api/ request reaches the network handoff");

        manager.seenPaths.clear();
        const QString suppressedErr = runRequest(QStringLiteral("/api/version"));
        check(manager.seenPaths.isEmpty(),
              "a second /api/ request during cooldown never reaches the network handoff");
        check(suppressedErr.contains(QStringLiteral("rate-limited")),
              "the suppressed reply reports a rate-limited error");

        manager.seenPaths.clear();
        runRequest(QStringLiteral("/static/app.js"));
        check(!manager.seenPaths.isEmpty(),
              "non-/api/ paths bypass the backoff gate even during cooldown");

        manager.setFirewallEnabled(true);
        manager.seenPaths.clear();
        const QString deniedErr = runRequest(QStringLiteral("/static/firewall-block"));
        check(manager.seenPaths.isEmpty(),
              "firewall-enabled manager blocks non-whitelisted requests locally");
        check(deniedErr.contains(QStringLiteral("firewall"), Qt::CaseInsensitive),
              "the firewall-denied reply reports a firewall block");

        int prompts = 0;
        manager.setFirewallPrompt(
            [&](const QString &, const QUrl &url, QString *ruleOut) {
                ++prompts;
                if (ruleOut)
                    *ruleOut = BackoffNetworkAccessManager::firewallRuleForUrl(url, true);
                return true;
            });
        manager.seenPaths.clear();
        runRequest(QStringLiteral("/static/firewall-allow"));
        check(prompts == 1 && !manager.seenPaths.isEmpty(),
              "allowing from the firewall prompt lets the request reach the network");

        manager.seenPaths.clear();
        runRequest(QStringLiteral("/static/firewall-allow-again"));
        check(prompts == 1 && !manager.seenPaths.isEmpty(),
              "the accepted firewall rule whitelists subsequent requests");
    }

    // --- Commit comment signing ------------------------------------------
    CommitComment commitVec;
    commitVec.author = "TESTPUB";
    commitVec.ts = 3000;
    commitVec.body = "Nice";
    const QByteArray expectedCommit =
        "forkmesh-commit-comment-v1\nabc123\nTESTPUB\n3000\n"
        "fdc96ffbf256523aec8846ae56321053c7ab751c99eb766e6bb4a7d362a4f060";
    check(CommitCommentStore::canonicalString("abc123", commitVec) == expectedCommit,
          "commit-comment canonical string matches the cross-language vector");

    // A burn-up series must reconstruct historical state, including a close
    // and a later reopening, rather than repeating today's status backwards.
    Issue firstIssue;
    firstIssue.createdAt = 1000;
    IssueEvent firstClosed;
    firstClosed.type = "status";
    firstClosed.status = "closed";
    firstClosed.ts = 3000;
    firstIssue.events.append(firstClosed);
    Issue secondIssue;
    secondIssue.createdAt = 2000;
    IssueEvent secondClosed = firstClosed;
    secondClosed.ts = 4000;
    IssueEvent secondReopened;
    secondReopened.type = "status";
    secondReopened.status = "open";
    secondReopened.ts = 5000;
    secondIssue.events = {secondReopened, secondClosed}; // intentionally unsorted
    const QList<IssueBurnupPoint> burnup =
        buildIssueBurnupSeries({firstIssue, secondIssue}, 1000, 5000, 4);
    check(burnup.size() == 5 && burnup.at(0).openCount == 1 &&
              burnup.at(0).closedCount == 0 && burnup.at(2).openCount == 1 &&
              burnup.at(2).closedCount == 1 && burnup.at(3).openCount == 0 &&
              burnup.at(3).closedCount == 2 && burnup.at(4).openCount == 1 &&
              burnup.at(4).closedCount == 1,
          "issue burn-up reconstructs close and reopen history");
    check(firstIssueHistoryTimestamp({firstIssue, secondIssue}, 9999) == 1000,
          "issue burn-up finds the all-time starting point");

    // Sign a real event with the node identity and verify it independently.
    IssueStore store(QString(), QString(), &identity, "tester");
    IssueEvent open;
    open.type = "open";
    open.title = "First issue";
    open.body = "Body text";
    IssueEvent signed_ = store.makeSignedEvent(7, open);
    check(signed_.author == identity.publicKey(),
          "signed event is stamped with the node public key");
    check(!signed_.sig.isEmpty(), "signed event carries a signature");
    check(verifyEd25519(signed_.author, signed_.sig,
                        IssueStore::canonicalString(7, signed_)),
          "issue-event signature verifies against the public key");
    IssueEvent tampered = signed_;
    tampered.body = "Body text!";
    check(!verifyEd25519(tampered.author, tampered.sig,
                         IssueStore::canonicalString(7, tampered)),
          "tampered issue-event signature is rejected");

    // --- Repository contribution snapshot bounds --------------------------
    {
        using namespace RepoContributionSnapshotInternal;

        DayMap exactDays;
        DayCounts commitOnly;
        commitOnly.commits = 1;
        for (int i = 0; i < kMaxDayRows; ++i) {
            exactDays.insert(dayActorKey(QStringLiteral("2026-07-13"),
                                         QString::number(i)),
                             commitOnly);
        }
        const RemovedCoverage exactDayCoverage = trimDayLimit(&exactDays);
        check(exactDays.size() == kMaxDayRows &&
                  !exactDayCoverage.commits &&
                  !exactDayCoverage.collaboration,
              "snapshot accepts exactly 2048 day rows");

        DayMap overflowingDays = exactDays;
        overflowingDays.insert(dayActorKey(QStringLiteral("2026-07-12"),
                                            QStringLiteral("oldest")),
                                commitOnly);
        const RemovedCoverage overflowingDayCoverage =
            trimDayLimit(&overflowingDays);
        check(overflowingDays.size() == kMaxDayRows &&
                  overflowingDayCoverage.commits &&
                  !overflowingDayCoverage.collaboration &&
                  !overflowingDays.contains(dayActorKey(
                      QStringLiteral("2026-07-12"),
                      QStringLiteral("oldest"))),
              "snapshot trims the oldest commit-only row at 2049 rows");

        DayMap mixedDays;
        DayCounts collaborationOnly;
        collaborationOnly.reviews = 1;
        mixedDays.insert(dayActorKey(QStringLiteral("2026-07-12"),
                                     QStringLiteral("commit")),
                         commitOnly);
        mixedDays.insert(dayActorKey(QStringLiteral("2026-07-12"),
                                     QStringLiteral("review")),
                         collaborationOnly);
        const RemovedCoverage mixedCoverage = removeOldestDate(&mixedDays);
        check(mixedDays.isEmpty() && mixedCoverage.commits &&
                  mixedCoverage.collaboration,
              "snapshot reports both coverage categories for mixed removed rows");

        ExtensionMap exactExtensions;
        for (int i = 0; i < kMaxExtensions; ++i) {
            ExtensionCounts counts;
            counts.bytes = i + 1;
            counts.files = 1;
            exactExtensions.insert(QStringLiteral("ext%1").arg(i, 3, 10,
                                                                QLatin1Char('0')),
                                   counts);
        }
        check(!trimExtensionLimit(&exactExtensions) &&
                  exactExtensions.size() == kMaxExtensions,
              "snapshot accepts exactly 256 extensions");

        ExtensionCounts smallestExtension;
        smallestExtension.files = 1;
        exactExtensions.insert(QStringLiteral("drop"), smallestExtension);
        check(trimExtensionLimit(&exactExtensions) &&
                  exactExtensions.size() == kMaxExtensions &&
                  !exactExtensions.contains(QStringLiteral("drop")),
              "snapshot deterministically trims the smallest of 257 extensions");

        int boundedCount = kMaxCount - 1;
        check(incrementBounded(&boundedCount) && boundedCount == kMaxCount &&
                  !incrementBounded(&boundedCount) && boundedCount == kMaxCount,
              "snapshot count cap includes one million and rejects overflow");

        const QByteArray exactEncodedBoundary(49152, 'x');
        const QByteArray overEncodedBoundary(49153, 'x');
        check(encodedSize(exactEncodedBoundary) == kMaxPayloadEncoded &&
                  encodedPayloadFits(exactEncodedBoundary) &&
                  encodedSize(overEncodedBoundary) > kMaxPayloadEncoded &&
                  !encodedPayloadFits(overEncodedBoundary),
              "snapshot encoded payload bound includes exactly 64 KiB");

        QByteArray boundedOutput = QByteArrayLiteral("ab");
        const bool exactOutputAccepted = appendBounded(
            &boundedOutput, QByteArrayLiteral("cd"), 4);
        const bool overflowOutputAccepted = appendBounded(
            &boundedOutput, QByteArrayLiteral("e"), 4);
        check(exactOutputAccepted && !overflowOutputAccepted &&
                  boundedOutput == QByteArrayLiteral("abcd"),
              "snapshot bounded append accepts the cap and rejects overflow");

        qsizetype strictRemaining = 6;
        QByteArray strictFirst;
        QByteArray strictSecond;
        const bool strictExactAccepted =
            StrictGitReadInternal::appendBounded(
                &strictFirst, QByteArrayLiteral("abcd"), 4,
                &strictRemaining);
        const bool strictPerCommandOverflow =
            StrictGitReadInternal::appendBounded(
                &strictFirst, QByteArrayLiteral("e"), 4,
                &strictRemaining);
        const bool strictTotalExactAccepted =
            StrictGitReadInternal::appendBounded(
                &strictSecond, QByteArrayLiteral("xy"), 4,
                &strictRemaining);
        const bool strictTotalOverflow =
            StrictGitReadInternal::appendBounded(
                &strictSecond, QByteArrayLiteral("z"), 4,
                &strictRemaining);
        check(strictExactAccepted && !strictPerCommandOverflow &&
                  strictTotalExactAccepted && !strictTotalOverflow &&
                  strictFirst == QByteArrayLiteral("abcd") &&
                  strictSecond == QByteArrayLiteral("xy") &&
                  strictRemaining == 0,
              "strict Git budget accepts exact caps and rejects cumulative overflow");
    }

    // --- Signed repository contribution snapshot --------------------------
    {
        const QString originalAppName = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(originalAppName + "SnapshotReviewer");
        ForkMeshIdentity reviewerIdentity;
        const bool reviewerLoaded = reviewerIdentity.load();
        QCoreApplication::setApplicationName(originalAppName);
        check(reviewerLoaded && reviewerIdentity.isValid() &&
                  reviewerIdentity.publicKey() != identity.publicKey(),
              "snapshot test creates a distinct reviewer identity");

        QTemporaryDir snapshotRepo;
        check(snapshotRepo.isValid(), "snapshot test repository is valid");
        const QString dir = snapshotRepo.path();
        const QString configuredCommitAuthorName = QStringLiteral("Owner Name");
        bool setup = snapshotRepo.isValid() &&
                     runTestGit(dir, {QStringLiteral("init"), QStringLiteral("-q"),
                                      QStringLiteral("-b"), QStringLiteral("main")}) &&
                     runTestGit(dir, {QStringLiteral("config"),
                                      QStringLiteral("user.name"),
                                      configuredCommitAuthorName}) &&
                     runTestGit(dir, {QStringLiteral("config"),
                                      QStringLiteral("user.email"),
                                      QStringLiteral("owner@example.test")});

        setup = setup && writeTestFile(dir + "/history.tmp", "a") &&
                commitTestTree(dir, "outside retention", "2021-07-12T23:59:59Z",
                               "Owner Name", "owner@example.test");
        setup = setup && writeTestFile(dir + "/history.tmp", "bb") &&
                commitTestTree(dir, "retention boundary", "2021-07-13T00:00:00Z",
                               "Owner Name", "owner@example.test");
        setup = setup && writeTestFile(dir + "/history.tmp", "ccc") &&
                commitTestTree(dir, "in-range author date",
                               "2026-07-09T09:00:00Z", "Owner Name",
                               "owner@example.test", "2021-07-01T09:00:00Z");
        setup = setup && writeTestFile(dir + "/src/main.cpp", "abcdefghij") &&
                writeTestFile(dir + "/include/api.hpp", "1234567") &&
                writeTestFile(dir + "/README.md", "hello") &&
                writeTestFile(dir + "/src/binary.cpp",
                              QByteArray("text\0binary", 11)) &&
                writeTestFile(
                    dir + "/src/generated.cpp",
                    "// Code generated by the snapshot fixture. DO NOT EDIT.\n"
                    "generated bytes\n") &&
                writeTestFile(dir + "/assets.bin", "binary") &&
                writeTestFile(dir + "/.forkmesh/private.json", "123456789") &&
                commitTestTree(dir, "supported files", "2026-07-10T09:00:00Z",
                               "Owner Name", "owner@example.test");
        setup = setup && writeTestFile(dir + "/src/main.cpp", "ABCDEFGHIJ") &&
                commitTestTree(dir, "other author", "2026-07-11T09:00:00Z",
                               configuredCommitAuthorName, "other@example.test");
        setup = setup && writeTestFile(dir + "/src/main.cpp", "abcdefghij") &&
                commitTestTree(dir, "future commit", "2026-07-14T09:00:00Z",
                               "Owner Name", "owner@example.test");
        check(setup, "snapshot test Git history is created");

        if (setup && reviewerLoaded) {
            // Store commits are deliberately authored by the nonmatching Git
            // identity so metadata writes do not become owner commit credit.
            check(runTestGit(dir, {QStringLiteral("config"),
                                   QStringLiteral("user.name"),
                                   QStringLiteral("Metadata Writer")}) &&
                      runTestGit(dir, {QStringLiteral("config"),
                                       QStringLiteral("user.email"),
                                       QStringLiteral("metadata@example.test")}),
                  "snapshot test switches metadata commit identity");

            IssueStore issueWriter(dir, QString(), &identity, "owner");
            IssueStore reviewerIssueSigner(QString(), QString(), &reviewerIdentity,
                                           "reviewer");
            QString storeError;
            IssueEvent validOpen;
            validOpen.type = "open";
            validOpen.id = "open-1";
            validOpen.title = "Valid signed issue";
            validOpen.body = "body";
            validOpen.ts = utcMs("2026-07-12T23:30:00Z");
            validOpen = reviewerIssueSigner.makeSignedEvent(1, validOpen);
            check(issueWriter.applyRemoteEvent(1, validOpen, validOpen.title,
                                               &storeError),
                  "snapshot test stores a valid signed issue open");

            IssueEvent invalidOpen;
            invalidOpen.type = "open";
            invalidOpen.id = "open-2";
            invalidOpen.title = "Tampered issue";
            invalidOpen.body = "before";
            invalidOpen.ts = utcMs("2026-07-12T23:45:00Z");
            invalidOpen = reviewerIssueSigner.makeSignedEvent(2, invalidOpen);
            invalidOpen.body = "after";
            check(issueWriter.applyRemoteEvent(2, invalidOpen, invalidOpen.title,
                                               &storeError),
                  "snapshot test stores an unverifiable issue fixture");

            IssueEvent issueComment;
            issueComment.type = "comment";
            issueComment.body = "not a contribution";
            issueComment.ts = utcMs("2026-07-13T01:00:00Z");
            issueComment = reviewerIssueSigner.makeSignedEvent(1, issueComment);
            check(issueWriter.applyRemoteEvent(1, issueComment, validOpen.title,
                                               &storeError),
                  "snapshot test stores a signed non-open issue event");

            PullStore pullWriter(dir, QString(), &identity, "owner");
            PullStore ownerPullSigner(QString(), QString(), &identity, "owner");
            PullStore reviewerPullSigner(QString(), QString(), &reviewerIdentity,
                                         "reviewer");
            PullRequest validPull;
            validPull.title = "Valid pull";
            validPull.description = "description";
            validPull.base = "main";
            validPull.head = "feature";
            validPull.patch =
                "diff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -0,0 +1 @@\n+x\n";
            validPull.ts = utcMs("2026-07-13T00:15:00Z");
            validPull = ownerPullSigner.makeSignedPull(validPull);
            check(pullWriter.applyRemotePull(validPull, &storeError),
                  "snapshot test stores a valid signed pull");

            PullRequest invalidPull = validPull;
            invalidPull.title = "Tampered pull";
            invalidPull.ts = utcMs("2026-07-13T00:30:00Z");
            check(pullWriter.applyRemotePull(invalidPull, &storeError),
                  "snapshot test stores an unverifiable pull fixture");

            PullRequest legacyPull;
            legacyPull.title = "Legacy signed pull";
            legacyPull.description = "legacy description";
            legacyPull.base = "main";
            legacyPull.head = "legacy-feature";
            legacyPull.patch =
                "diff --git a/b b/b\n--- a/b\n+++ b/b\n@@ -0,0 +1 @@\n+y\n";
            legacyPull.ts = utcMs("2026-07-12T22:00:00Z");
            legacyPull.author = reviewerIdentity.publicKey();
            legacyPull.authorName = "reviewer";
            legacyPull.sig =
                reviewerIdentity.signData(legacyPullCanonical(legacyPull));
            check(verifyEd25519(legacyPull.author, legacyPull.sig,
                                legacyPullCanonical(legacyPull)),
                  "snapshot fixture verifies a legacy four-field pull signature");
            check(pullWriter.applyRemotePull(legacyPull, &storeError),
                  "snapshot test stores a legacy signed pull");

            PullEvent validReview;
            validReview.type = "review";
            validReview.state = "approved";
            validReview.body = "looks good";
            validReview.ts = utcMs("2026-07-13T10:00:00Z");
            validReview = reviewerPullSigner.makeSignedEvent(1, validReview);
            check(pullWriter.applyRemoteEvent(1, validReview, &storeError) &&
                      pullWriter.applyRemoteEvent(1, validReview, &storeError),
                  "snapshot test stores a replayed signed review");

            PullEvent aliasedSignatureReview = validReview;
            const QByteArray signatureAlphabet = QByteArrayLiteral(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
            QByteArray aliasedSignature = validReview.sig.toLatin1();
            const int signatureFinalSextet =
                signatureAlphabet.indexOf(aliasedSignature.back());
            if (signatureFinalSextet >= 0)
                aliasedSignature.back() =
                    signatureAlphabet.at(signatureFinalSextet | 1);
            aliasedSignatureReview.sig = QString::fromLatin1(aliasedSignature);
            check(aliasedSignatureReview.sig.size() == 86 &&
                      aliasedSignatureReview.sig != validReview.sig &&
                      verifyEd25519(aliasedSignatureReview.author,
                                    aliasedSignatureReview.sig,
                                    PullStore::canonicalString(
                                        1, aliasedSignatureReview)),
                  "snapshot fixture verifies a noncanonical signature alias");
            check(pullWriter.applyRemoteEvent(1, aliasedSignatureReview,
                                              &storeError),
                  "snapshot test stores a noncanonical signature replay");

            PullEvent nonCanonicalReview;
            nonCanonicalReview.type = "review";
            nonCanonicalReview.id = "noncanonical-review";
            nonCanonicalReview.state = "approved";
            nonCanonicalReview.body =
                "cryptographically valid noncanonical actor";
            nonCanonicalReview.ts = utcMs("2026-07-13T10:02:00Z");
            QByteArray nonCanonicalActor =
                reviewerIdentity.publicKey().toLatin1();
            const QByteArray base64UrlAlphabet = QByteArrayLiteral(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
            const int finalSextet =
                base64UrlAlphabet.indexOf(nonCanonicalActor.back());
            if (finalSextet >= 0)
                nonCanonicalActor.back() = base64UrlAlphabet.at(finalSextet | 1);
            nonCanonicalReview.author = QString::fromLatin1(nonCanonicalActor);
            nonCanonicalReview.sig = reviewerIdentity.signData(
                PullStore::canonicalString(1, nonCanonicalReview));
            check(nonCanonicalReview.author.size() == 43 &&
                      nonCanonicalReview.author != reviewerIdentity.publicKey() &&
                      verifyEd25519(nonCanonicalReview.author,
                                    nonCanonicalReview.sig,
                                    PullStore::canonicalString(
                                        1, nonCanonicalReview)),
                  "snapshot fixture accepts a signed noncanonical actor spelling");
            check(pullWriter.applyRemoteEvent(1, nonCanonicalReview,
                                              &storeError),
                  "snapshot test stores a signed noncanonical review actor");

            PullEvent pullComment;
            pullComment.type = "comment";
            pullComment.body = "not a review";
            pullComment.ts = utcMs("2026-07-13T10:05:00Z");
            pullComment = reviewerPullSigner.makeSignedEvent(1, pullComment);
            check(pullWriter.applyRemoteEvent(1, pullComment, &storeError),
                  "snapshot test stores a signed pull comment");

            PullEvent invalidReview = validReview;
            invalidReview.body = "tampered review";
            invalidReview.ts = utcMs("2026-07-13T10:10:00Z");
            check(pullWriter.applyRemoteEvent(1, invalidReview, &storeError),
                  "snapshot test stores an unverifiable review fixture");

            check(runTestGit(dir, {QStringLiteral("config"),
                                   QStringLiteral("user.name"),
                                   QStringLiteral("Owner Name")}) &&
                      runTestGit(dir, {QStringLiteral("config"),
                                       QStringLiteral("user.email"),
                                       QStringLiteral("OWNER@EXAMPLE.TEST")}),
                  "snapshot test restores configured owner identity");

            RepoContributionSnapshotInput input;
            input.workTreePath = dir;
            input.branch = "main";
            input.head = testGitHead(dir);
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            input.retentionYears = 5;

            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            const RepoContributionSnapshot repeated =
                buildRepoContributionSnapshot(input);
            check(snapshot.complete && snapshot.error.isEmpty(),
                  "repository contribution snapshot builds successfully");
            check(repeated.complete &&
                      repeated.compactPayload == snapshot.compactPayload,
                  "repository contribution compact bytes are deterministic");

            QStringList payloadKeys = snapshot.payload.keys();
            payloadKeys.sort();
            QStringList expectedPayloadKeys{
                "branch", "capturedAt", "coverage", "days", "extensions",
                "fileCount", "from", "head", "through", "version"};
            expectedPayloadKeys.sort();
            QStringList coverageKeys =
                snapshot.payload.value("coverage").toObject().keys();
            coverageKeys.sort();
            check(payloadKeys == expectedPayloadKeys &&
                      coverageKeys ==
                          (QStringList{"collaboration", "commits", "languages"}),
                  "snapshot payload has the exact version 1 keys");
            check(snapshot.payload.value("version").toInt() == 1 &&
                      snapshot.payload.value("capturedAt").toVariant().toLongLong() ==
                          input.capturedAtMs &&
                      snapshot.payload.value("head").toString() == input.head &&
                      snapshot.payload.value("branch").toString() == "main" &&
                      snapshot.payload.value("from").toString() == "2021-07-13" &&
                      snapshot.payload.value("through").toString() == "2026-07-13",
                  "snapshot identity and five-year UTC bounds are exact");

            RepoContributionSnapshotInput leapInput = input;
            leapInput.capturedAtMs = utcMs("2024-02-29T12:00:00Z");
            const RepoContributionSnapshot leapSnapshot =
                buildRepoContributionSnapshot(leapInput);
            check(leapSnapshot.complete &&
                      leapSnapshot.payload.value("from").toString() == "2019-03-01" &&
                      leapSnapshot.payload.value("through").toString() == "2024-02-29",
                  "snapshot leap-day boundary stays within five calendar years");

            int commitTotal = 0;
            int issueTotal = 0;
            int pullTotal = 0;
            int reviewTotal = 0;
            QString previousRowKey;
            for (const QJsonValue &value :
                 snapshot.payload.value("days").toArray()) {
                const QJsonArray row = value.toArray();
                if (row.size() != 6)
                    continue;
                const QString rowKey = row.at(0).toString() + "\n" +
                                       row.at(1).toString();
                check(previousRowKey.isEmpty() || previousRowKey < rowKey,
                      "snapshot day rows are sorted and unique");
                previousRowKey = rowKey;
                commitTotal += row.at(2).toInt();
                issueTotal += row.at(3).toInt();
                pullTotal += row.at(4).toInt();
                reviewTotal += row.at(5).toInt();
            }
            check(commitTotal == 3 && issueTotal == 1 && pullTotal == 2 &&
                      reviewTotal == 1,
                  "snapshot counts only matching commits and verified event types");
            check(contributionRow(snapshot, "2021-07-13", identity.publicKey())
                          .at(2)
                          .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-10", identity.publicKey())
                              .at(2)
                              .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-11", identity.publicKey())
                          .isEmpty(),
                  "snapshot includes the UTC retention boundary only for configured email");
            check(contributionRow(snapshot, "2026-07-12",
                                  reviewerIdentity.publicKey())
                          .at(3)
                          .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-12",
                                      reviewerIdentity.publicKey())
                              .at(4)
                              .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-13", identity.publicKey())
                              .at(4)
                              .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-13",
                                      reviewerIdentity.publicKey())
                              .at(5)
                              .toInt() == 1 &&
                      contributionRow(snapshot, "2026-07-13",
                                      nonCanonicalReview.author)
                          .isEmpty(),
                  "snapshot retains signed issue pull and review actor keys");

            const QJsonArray extensions =
                snapshot.payload.value("extensions").toArray();
            check(extensions.size() == 3 &&
                      extensions.at(0).toArray() ==
                          QJsonArray{"cpp", 10, 1} &&
                      extensions.at(1).toArray() ==
                          QJsonArray{"hpp", 7, 1} &&
                      extensions.at(2).toArray() ==
                          QJsonArray{"md", 5, 1},
                  "snapshot extension bytes are exact sorted and supported-only");
            check(snapshot.payload.value("fileCount").toInt() == 7,
                  "snapshot file count excludes internal metadata but keeps other files");
            check(!snapshot.compactPayload.contains("owner@example.test") &&
                      !snapshot.compactPayload.contains("Owner Name") &&
                      !snapshot.compactPayload.contains("Metadata Writer"),
                  "snapshot does not expose raw Git or display identities");

            const QJsonObject coverage =
                snapshot.payload.value("coverage").toObject();
            check(coverage.value("commits").toString() == "complete" &&
                      coverage.value("collaboration").toString() == "complete" &&
                      coverage.value("languages").toString() == "complete",
                  "snapshot reports complete coverage for fully readable sources");

            RepoContributionSnapshotInternal::LanguageInspectionLimits
                exactLanguageLimits;
            exactLanguageLimits.maxBlobs = 5;
            RepoContributionSnapshot exactLanguageSnapshot;
            {
                RepoContributionSnapshotInternal::
                    ScopedLanguageInspectionLimitsForTests scoped(
                        exactLanguageLimits);
                exactLanguageSnapshot = buildRepoContributionSnapshot(input);
            }
            check(exactLanguageSnapshot.complete &&
                      exactLanguageSnapshot.payload.value("coverage")
                              .toObject()
                              .value("languages")
                              .toString() == "complete" &&
                      exactLanguageSnapshot.payload.value("extensions") ==
                          snapshot.payload.value("extensions"),
                  "snapshot language inspection accepts the exact blob cap");

            RepoContributionSnapshotInternal::LanguageInspectionLimits
                overflowLanguageLimits = exactLanguageLimits;
            overflowLanguageLimits.maxBlobs = 4;
            RepoContributionSnapshot overflowLanguageSnapshot;
            {
                RepoContributionSnapshotInternal::
                    ScopedLanguageInspectionLimitsForTests scoped(
                        overflowLanguageLimits);
                overflowLanguageSnapshot =
                    buildRepoContributionSnapshot(input);
            }
            check(overflowLanguageSnapshot.complete &&
                      overflowLanguageSnapshot.payload.value("coverage")
                              .toObject()
                              .value("languages")
                              .toString() == "partial",
                  "snapshot marks incomplete language inspection partial");
            const QByteArray encoded = snapshot.compactPayload.toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
            check(encoded.size() <= 64 * 1024,
                  "snapshot transport stays within the encoded payload cap");

            const QByteArray digest = QCryptographicHash::hash(
                                          snapshot.compactPayload,
                                          QCryptographicHash::Sha256)
                                          .toHex();
            const QByteArray expectedCanonical =
                QByteArrayLiteral("forkmesh-profile-contribution-v1\nowner\nrepo\n") +
                QByteArrayLiteral("1783944000000\n") + digest;
            check(profileContributionCanonical(
                      "owner", "repo", "1783944000000", snapshot.compactPayload) ==
                      expectedCanonical,
                  "snapshot signature canonical hashes the exact compact bytes");

            const QString contributionUpdatedAt =
                QString::number(input.capturedAtMs);
            const QJsonObject contributionFields = signedContributionFields(
                snapshot, QStringLiteral("owner"), QStringLiteral("repo"),
                contributionUpdatedAt, identity);
            QStringList contributionKeys = contributionFields.keys();
            contributionKeys.sort();
            const QByteArray contributionEncoded =
                contributionFields.value("contributionPayload")
                    .toString()
                    .toLatin1();
            const QByteArray contributionDecoded = QByteArray::fromBase64(
                contributionEncoded, QByteArray::Base64UrlEncoding |
                                         QByteArray::AbortOnBase64DecodingErrors);
            const QString contributionSignature =
                contributionFields.value("contributionSig").toString();
            const QByteArray contributionCanonical =
                profileContributionCanonical(
                    QStringLiteral("owner"), QStringLiteral("repo"),
                    contributionUpdatedAt, snapshot.compactPayload);
            check(contributionKeys ==
                          (QStringList{QStringLiteral("contributionPayload"),
                                       QStringLiteral("contributionSig")}) &&
                      contributionDecoded == snapshot.compactPayload &&
                      !contributionEncoded.contains('=') &&
                      contributionEncoded.size() <= 64 * 1024 &&
                      ForkMeshIdentity::verifySignature(
                          identity.publicKey(), contributionSignature,
                          contributionCanonical),
                  "signed contribution fields carry the exact payload and a valid signature");
            check(!ForkMeshIdentity::verifySignature(
                      identity.publicKey(), contributionSignature,
                      profileContributionCanonical(
                          QStringLiteral("owner"), QStringLiteral("other"),
                          contributionUpdatedAt, snapshot.compactPayload)) &&
                      !ForkMeshIdentity::verifySignature(
                          identity.publicKey(), contributionSignature,
                          profileContributionCanonical(
                              QStringLiteral("owner"), QStringLiteral("repo"),
                              contributionUpdatedAt,
                              snapshot.compactPayload + 'x')),
                  "signed contribution signature rejects tampered canonical inputs");

            RepoContributionSnapshot invalidContribution = snapshot;
            invalidContribution.complete = false;
            check(signedContributionFields(
                      invalidContribution, QStringLiteral("owner"),
                      QStringLiteral("repo"), contributionUpdatedAt, identity)
                      .isEmpty(),
                  "signed contribution fields reject an incomplete snapshot");
            invalidContribution = snapshot;
            invalidContribution.error = QStringLiteral("snapshot error");
            check(signedContributionFields(
                      invalidContribution, QStringLiteral("owner"),
                      QStringLiteral("repo"), contributionUpdatedAt, identity)
                      .isEmpty(),
                  "signed contribution fields reject a snapshot error");
            invalidContribution = snapshot;
            invalidContribution.compactPayload.clear();
            check(signedContributionFields(
                      invalidContribution, QStringLiteral("owner"),
                      QStringLiteral("repo"), contributionUpdatedAt, identity)
                      .isEmpty(),
                  "signed contribution fields reject empty compact bytes");
            invalidContribution = snapshot;
            invalidContribution.compactPayload.append(' ');
            check(signedContributionFields(
                      invalidContribution, QStringLiteral("owner"),
                      QStringLiteral("repo"), contributionUpdatedAt, identity)
                      .isEmpty(),
                  "signed contribution fields reject mismatched compact bytes");
            check(signedContributionFields(
                      snapshot, QStringLiteral("owner"),
                      QStringLiteral("repo"),
                      QString::number(input.capturedAtMs + 1), identity)
                      .isEmpty() &&
                      signedContributionFields(
                          snapshot, QStringLiteral("owner"),
                          QStringLiteral("repo"),
                          QStringLiteral("01783944000000"), identity)
                          .isEmpty(),
                  "signed contribution fields require the exact canonical capture time");

            RepoContributionSnapshot oversizedContribution;
            oversizedContribution.complete = true;
            oversizedContribution.payload =
                QJsonObject{{QStringLiteral("capturedAt"),
                             double(input.capturedAtMs)},
                            {QStringLiteral("padding"),
                             QString(50 * 1024, QLatin1Char('x'))}};
            oversizedContribution.compactPayload =
                QJsonDocument(oversizedContribution.payload)
                    .toJson(QJsonDocument::Compact);
            check(oversizedContribution.compactPayload
                          .toBase64(QByteArray::Base64UrlEncoding |
                                    QByteArray::OmitTrailingEquals)
                          .size() >
                      64 * 1024 &&
                      signedContributionFields(
                          oversizedContribution, QStringLiteral("owner"),
                          QStringLiteral("repo"), contributionUpdatedAt,
                          identity)
                          .isEmpty(),
                  "signed contribution fields reject an oversized transport");
            ForkMeshIdentity unloadedIdentity;
            check(signedContributionFields(
                      snapshot, QStringLiteral("owner"),
                      QStringLiteral("repo"), contributionUpdatedAt,
                      unloadedIdentity)
                      .isEmpty() &&
                      signedContributionFields(
                          snapshot, QStringLiteral("bad owner"),
                          QStringLiteral("repo"), contributionUpdatedAt,
                          identity)
                          .isEmpty(),
                  "signed contribution fields reject invalid signing inputs");

            const QString contributionCacheKey =
                RepoContributionPublicationCache::key(
                    QStringLiteral("owner"), QStringLiteral("repo"),
                    input.head, input.branch, identity.publicKey(),
                    QStringLiteral("dependency-a"));
            check(contributionCacheKey.size() == 64 &&
                      contributionCacheKey ==
                          RepoContributionPublicationCache::key(
                              QStringLiteral("owner"), QStringLiteral("repo"),
                              input.head, input.branch, identity.publicKey(),
                              QStringLiteral("dependency-a")) &&
                      contributionCacheKey !=
                          RepoContributionPublicationCache::key(
                              QStringLiteral("owne"),
                              QStringLiteral("rrepo"), input.head,
                              input.branch, identity.publicKey(),
                              QStringLiteral("dependency-a")) &&
                      contributionCacheKey !=
                          RepoContributionPublicationCache::key(
                              QStringLiteral("owner"), QStringLiteral("repo"),
                              input.head, input.branch, identity.publicKey(),
                              QStringLiteral("dependency-b")) &&
                      RepoContributionPublicationCache::key(
                          QStringLiteral("a"), QStringLiteral("bc"),
                          QStringLiteral("d"), QStringLiteral("ef"),
                          QStringLiteral("g"), QStringLiteral("h")) !=
                          RepoContributionPublicationCache::key(
                              QStringLiteral("ab"), QStringLiteral("c"),
                              QStringLiteral("de"), QStringLiteral("f"),
                              QStringLiteral("g"), QStringLiteral("h")),
                  "contribution cache keys include stable dependency state");

            using CacheBegin =
                RepoContributionPublicationCache::BeginResult;
            RepoContributionPublicationCache contributionCache(2, 2);
            check(contributionCache.begin(contributionCacheKey, false) ==
                          CacheBegin::Started &&
                      contributionCache.begin(contributionCacheKey, true) ==
                          CacheBegin::Coalesced &&
                      contributionCache.inFlight(contributionCacheKey),
                  "contribution cache coalesces in-flight work and preserves dialog intent");
            const qint64 cacheTime = 1000000;
            check(contributionCache.finish(contributionCacheKey, snapshot,
                                           cacheTime) &&
                      !contributionCache.inFlight(contributionCacheKey),
                  "contribution cache returns preserved dialog intent on completion");
            check(contributionCache.lookup(
                      contributionCacheKey,
                      cacheTime + 6LL * 60 * 60 * 1000 - 1)
                          .has_value() &&
                      !contributionCache.lookup(
                           contributionCacheKey,
                           cacheTime + 6LL * 60 * 60 * 1000)
                           .has_value(),
                  "successful contribution snapshots use the six-hour refresh TTL");

            RepoContributionSnapshot cachedError;
            cachedError.error = QString(500, QLatin1Char('x'));
            const QString errorCacheKey =
                RepoContributionPublicationCache::key(
                    QStringLiteral("owner"), QStringLiteral("error"),
                    input.head, input.branch, identity.publicKey(),
                    QStringLiteral("dependency-a"));
            check(contributionCache.begin(errorCacheKey, false) ==
                          CacheBegin::Started &&
                      !contributionCache.finish(errorCacheKey, cachedError,
                                                cacheTime + 1),
                  "contribution cache records an error without dialog intent");
            const auto boundedCachedError = contributionCache.lookup(
                errorCacheKey, cacheTime + 5LL * 60 * 1000);
            check(boundedCachedError.has_value() &&
                      !boundedCachedError->complete &&
                      boundedCachedError->payload.isEmpty() &&
                      boundedCachedError->compactPayload.isEmpty() &&
                      !boundedCachedError->error.isEmpty() &&
                      boundedCachedError->error.size() <= 240 &&
                      !contributionCache.lookup(
                           errorCacheKey,
                           cacheTime + 1 + 5LL * 60 * 1000)
                           .has_value(),
                  "contribution errors are bounded and use the five-minute retry TTL");

            const QString newestCacheKey =
                RepoContributionPublicationCache::key(
                    QStringLiteral("owner"), QStringLiteral("newest"),
                    input.head, input.branch, identity.publicKey(),
                    QStringLiteral("dependency-a"));
            check(contributionCache.begin(newestCacheKey, false) ==
                          CacheBegin::Started &&
                      !contributionCache.finish(newestCacheKey, snapshot,
                                                cacheTime + 2) &&
                      !contributionCache.lookup(contributionCacheKey,
                                                cacheTime + 2)
                           .has_value() &&
                      contributionCache.lookup(errorCacheKey, cacheTime + 2)
                          .has_value() &&
                      contributionCache.lookup(newestCacheKey, cacheTime + 2)
                          .has_value(),
                  "contribution cache evicts the oldest entry at its bound");

            RepoContributionPublicationCache transitionCache(4, 2);
            check(transitionCache.begin(contributionCacheKey, false) ==
                          CacheBegin::Started &&
                      !transitionCache.finish(contributionCacheKey, snapshot,
                                              cacheTime) &&
                      transitionCache.lookup(contributionCacheKey, cacheTime)
                          .has_value(),
                  "public contribution snapshot is cached before a visibility transition");
            transitionCache.invalidate(contributionCacheKey);
            check(!transitionCache.lookup(contributionCacheKey, cacheTime)
                       .has_value(),
                  "private publication invalidates the prior public snapshot");
            check(transitionCache.begin(contributionCacheKey, true) ==
                          CacheBegin::Started,
                  "contribution scan starts before an in-flight invalidation");
            transitionCache.invalidate(contributionCacheKey);
            check(transitionCache.finish(contributionCacheKey, snapshot,
                                         cacheTime + 1) &&
                      !transitionCache.inFlight(contributionCacheKey) &&
                      !transitionCache.lookup(contributionCacheKey,
                                              cacheTime + 1)
                           .has_value(),
                  "invalidated in-flight contribution results are discarded");

            check(transitionCache.claimStaleRetry(
                      QStringLiteral("owner/repo"), contributionCacheKey) &&
                      !transitionCache.claimStaleRetry(
                          QStringLiteral("owner/repo"), contributionCacheKey) &&
                      transitionCache.claimStaleRetry(
                          QStringLiteral("owner/repo"), errorCacheKey),
                  "stale-update retry is allowed once for each snapshot key");
            transitionCache.clearStaleRetry(QStringLiteral("owner/repo"));
            check(transitionCache.claimStaleRetry(
                      QStringLiteral("owner/repo"), contributionCacheKey),
                  "successful or reset publication clears the stale retry guard");

            check(!repoContributionResponseNeedsRefresh(
                      QJsonObject{{QStringLiteral("contributionsAccepted"), true}},
                      true) &&
                      repoContributionResponseNeedsRefresh(
                          QJsonObject{{QStringLiteral("contributionsAccepted"),
                                       false},
                                      {QStringLiteral("contributionWarning"),
                                       QStringLiteral("invalid payload")}},
                          true) &&
                      !repoContributionResponseNeedsRefresh(
                          QJsonObject{{QStringLiteral("contributionsAccepted"),
                                       false}},
                          false) &&
                      !repoContributionResponseNeedsRefresh(QJsonObject{}, true),
                  "only an explicitly rejected submitted contribution requires a refresh");

            QFile publicationFile(
                QFileInfo(QString::fromUtf8(__FILE__))
                    .absoluteDir()
                    .filePath(QStringLiteral("../src/MainWindowRepos.cpp")));
            const bool publicationOpened =
                publicationFile.open(QIODevice::ReadOnly);
            const QString publicationSource =
                publicationOpened
                    ? QString::fromUtf8(publicationFile.readAll())
                    : QString();
            check(publicationOpened &&
                      !publicationSource.contains(
                          QStringLiteral(
                              "repoContributionDependencyFingerprint(")) &&
                      publicationSource.contains(
                          QStringLiteral(
                              "QFutureWatcher<RepoContributionPreparation>")) &&
                      publicationSource.contains(
                          QStringLiteral("QtConcurrent::run(")) &&
                      publicationSource.contains(
                          QStringLiteral(
                              "prepareRepoContributionSnapshot(")),
                  "catalog dependency discovery stays inside the bounded worker path");

            const int staleBranch = publicationSource.indexOf(
                QStringLiteral(
                    "responseCode == QLatin1String(\"stale_update\")"));
            const int successBranch = publicationSource.indexOf(
                QStringLiteral(
                    "if (error == QNetworkReply::NoError"),
                staleBranch);
            const QString staleHandling =
                staleBranch >= 0 && successBranch > staleBranch
                    ? publicationSource.mid(
                          staleBranch, successBranch - staleBranch)
                    : QString();
            check(staleHandling.contains(
                      QStringLiteral("claimStaleRetry(")) &&
                      staleHandling.contains(
                          QStringLiteral("publishQueuedUpdate();")),
                  "second stale contribution response preserves a queued repo update");

            const int rejectionBranch = publicationSource.indexOf(
                QStringLiteral(
                    "repoContributionResponseNeedsRefresh("),
                successBranch);
            const int acceptedBranch = publicationSource.indexOf(
                QStringLiteral(
                    "m_contributionPublicationCache.clearStaleRetry("),
                rejectionBranch);
            const QString rejectionHandling =
                rejectionBranch >= 0 && acceptedBranch > rejectionBranch
                    ? publicationSource.mid(
                          rejectionBranch,
                          acceptedBranch - rejectionBranch)
                    : QString();
            check(rejectionHandling.contains(
                      QStringLiteral(
                          "m_contributionPublicationCache.invalidate(")) &&
                      rejectionHandling.contains(
                          QStringLiteral("claimStaleRetry(")) &&
                      rejectionHandling.contains(
                          QStringLiteral("scheduleCatalogPublish(")) &&
                      rejectionHandling.contains(
                          QStringLiteral("publishQueuedUpdate();")),
                  "rejected submitted contributions invalidate and retry once without dropping queued work");

            // A persistently-overloaded relay (Cloudflare 1102 -> HTTP 503)
            // must not be re-POSTed at a fixed 60s cadence forever: the retry
            // delay escalates per consecutive failure and resets on success.
            check(publicationSource.contains(
                      QStringLiteral(
                          "catalogPublishRetryDelayMs(const QNetworkReply "
                          "*reply, int status,")) &&
                      publicationSource.contains(
                          QStringLiteral("int consecutiveFailures)")) &&
                      publicationSource.contains(QStringLiteral(
                          "kCatalogPublishRateLimitRetryMs << shift")),
                  "catalog publish retry delay grows exponentially with consecutive failures");
            check(publicationSource.contains(QStringLiteral(
                      "catalogPublishRetryDelayMs(reply, status, "
                      "priorFailures)")) &&
                      publicationSource.contains(QStringLiteral(
                          "m_catalogPublishConsecutiveFailures.insert(")) &&
                      publicationSource.contains(QStringLiteral(
                          "m_catalogPublishConsecutiveFailures.remove(publishKey)")),
                  "consecutive publish failures are counted, escalated, and cleared on success");

            RepoContributionPublicationCache scanCapacityCache(8, 2);
            check(scanCapacityCache.begin(contributionCacheKey, false) ==
                          CacheBegin::Started &&
                      scanCapacityCache.begin(contributionCacheKey, true) ==
                          CacheBegin::Coalesced &&
                      scanCapacityCache.begin(errorCacheKey, false) ==
                          CacheBegin::Started,
                  "contribution scan capacity accepts the exact global cap");
            check(scanCapacityCache.begin(newestCacheKey, true) ==
                          CacheBegin::CapacityExceeded &&
                      !scanCapacityCache.inFlight(newestCacheKey),
                  "contribution scan capacity rejects overflow without starting work");
            check(scanCapacityCache.finish(contributionCacheKey, snapshot,
                                           cacheTime) &&
                      scanCapacityCache.begin(newestCacheKey, true) ==
                          CacheBegin::Started &&
                      scanCapacityCache.inFlight(newestCacheKey),
                  "finishing a contribution scan releases one global slot");

            RepoContributionPublicationCache preparationCapacityCache(8, 1);
            check(preparationCapacityCache.begin(contributionCacheKey, true) ==
                          CacheBegin::Started,
                  "dependency preparation occupies one global scan slot");
            const auto preparationCompletion =
                preparationCapacityCache.complete(contributionCacheKey);
            check(preparationCompletion.requestDialog &&
                      !preparationCompletion.discarded &&
                      preparationCapacityCache.begin(errorCacheKey, false) ==
                          CacheBegin::Started,
                  "dependency-only completion preserves dialog intent and releases capacity");

            QTemporaryDir dependencyRepo;
            const bool dependencySetup =
                dependencyRepo.isValid() &&
                runTestGit(dependencyRepo.path(),
                           {QStringLiteral("init"), QStringLiteral("-q"),
                            QStringLiteral("-b"), QStringLiteral("main")}) &&
                runTestGit(dependencyRepo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.name"),
                            QStringLiteral("Dependency Owner")}) &&
                runTestGit(dependencyRepo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.email"),
                            QStringLiteral("first@example.test")}) &&
                writeTestFile(dependencyRepo.path() +
                                  QStringLiteral("/source.cpp"),
                              QByteArrayLiteral("x")) &&
                commitTestTree(dependencyRepo.path(),
                               QStringLiteral("dependency base"),
                               QStringLiteral("2026-07-13T09:00:00Z"),
                               QStringLiteral("Dependency Owner"),
                               QStringLiteral("first@example.test"));
            check(dependencySetup,
                  "contribution dependency fingerprint repository is created");
            if (dependencySetup) {
                RepoContributionSnapshotInput dependencyInput;
                dependencyInput.workTreePath = dependencyRepo.path();
                dependencyInput.branch = QStringLiteral("main");
                dependencyInput.head = testGitHead(dependencyRepo.path());
                dependencyInput.publishingKey = identity.publicKey();
                dependencyInput.capturedAtMs = input.capturedAtMs;
                const QString firstDependency =
                    repoContributionDependencyFingerprint(dependencyInput);
                const QString unchangedDependency =
                    repoContributionDependencyFingerprint(dependencyInput);
                const RepoContributionPreparation reusedPreparation =
                    prepareRepoContributionSnapshot(
                        dependencyInput, firstDependency,
                        /*cachedSnapshotAvailable=*/true);
                const bool identityChanged = runTestGit(
                    dependencyRepo.path(),
                    {QStringLiteral("config"), QStringLiteral("user.email"),
                     QStringLiteral("second@example.test")});
                const QString identityDependency =
                    repoContributionDependencyFingerprint(dependencyInput);
                const RepoContributionPreparation changedPreparation =
                    prepareRepoContributionSnapshot(
                        dependencyInput, firstDependency,
                        /*cachedSnapshotAvailable=*/true);
                const bool identityRestored = runTestGit(
                    dependencyRepo.path(),
                    {QStringLiteral("config"), QStringLiteral("user.email"),
                     QStringLiteral("first@example.test")});
                const QString restoredDependency =
                    repoContributionDependencyFingerprint(dependencyInput);
                const bool pullRefAdded = runTestGit(
                    dependencyRepo.path(),
                    {QStringLiteral("update-ref"),
                     QStringLiteral("refs/heads/forkmesh/pulls"),
                     dependencyInput.head});
                const QString pullDependency =
                    repoContributionDependencyFingerprint(dependencyInput);
                check(firstDependency.size() == 64 &&
                          firstDependency == unchangedDependency &&
                          reusedPreparation.dependencyFingerprint ==
                              firstDependency &&
                          !reusedPreparation.rebuiltSnapshot.has_value() &&
                          identityChanged &&
                          firstDependency != identityDependency &&
                          changedPreparation.dependencyFingerprint ==
                              identityDependency &&
                          changedPreparation.rebuiltSnapshot.has_value() &&
                          identityRestored &&
                          firstDependency == restoredDependency &&
                          pullRefAdded &&
                          firstDependency != pullDependency &&
                          !firstDependency.contains(
                              QStringLiteral("first@example.test")),
                      "dependency fingerprint tracks pull metadata and effective Git identity privately");
            }

            QTemporaryDir logoRepo;
            const QByteArray privateSourceMarker =
                QByteArrayLiteral("PRIVATE_SOURCE_BODY_MUST_NOT_BE_PUBLISHED");
            const bool logoRepoSetup =
                logoRepo.isValid() &&
                runTestGit(logoRepo.path(),
                           {QStringLiteral("init"), QStringLiteral("-q"),
                            QStringLiteral("-b"), QStringLiteral("main")}) &&
                runTestGit(logoRepo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.name"),
                            QStringLiteral("Logo Metadata Owner")}) &&
                runTestGit(logoRepo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.email"),
                            QStringLiteral("logo@example.test")}) &&
                writeTestFile(logoRepo.path() +
                                  QStringLiteral("/src/main.cpp"),
                              privateSourceMarker) &&
                writeTestFile(logoRepo.path() +
                                  QStringLiteral("/web/app.ts"),
                              QByteArrayLiteral("export const app = true;")) &&
                writeTestFile(logoRepo.path() +
                                  QStringLiteral("/CMakeLists.txt"),
                              QByteArrayLiteral("project(LogoFixture)")) &&
                writeTestFile(logoRepo.path() +
                                  QStringLiteral("/package.json"),
                              QByteArrayLiteral("{\"private\":true}")) &&
                writeTestFile(logoRepo.path() +
                                  QStringLiteral("/wrangler.toml"),
                              QByteArrayLiteral("name='logo-fixture'")) &&
                commitTestTree(logoRepo.path(),
                               QStringLiteral("logo metadata fixture"),
                               QStringLiteral("2026-07-20T09:00:00Z"),
                               QStringLiteral("Logo Metadata Owner"),
                               QStringLiteral("logo@example.test"));
            check(logoRepoSetup,
                  "native logo metadata fixture is a real committed repository");
            if (logoRepoSetup) {
                RepoContributionSnapshotInput logoSnapshotInput;
                logoSnapshotInput.workTreePath = logoRepo.path();
                logoSnapshotInput.branch = QStringLiteral("main");
                logoSnapshotInput.head = testGitHead(logoRepo.path());
                logoSnapshotInput.publishingKey = identity.publicKey();
                logoSnapshotInput.capturedAtMs =
                    utcMs("2026-07-21T12:00:00Z");
                const RepoContributionSnapshot logoSnapshot =
                    buildRepoContributionSnapshot(logoSnapshotInput);

                RepoLogoMetadataInput logoInput;
                logoInput.workTreePath = logoRepo.path();
                logoInput.head = logoSnapshotInput.head;
                logoInput.description =
                    QStringLiteral("A three-dimensional developer platform");
                logoInput.topics = {
                    QStringLiteral("developer-platform"),
                    QStringLiteral("threejs"),
                };
                logoInput.contributionPayload = logoSnapshot.payload;
                const QJsonObject publicLogoMetadata =
                    buildRepoLogoMetadata(logoInput);
                const QJsonObject publicLanguages =
                    publicLogoMetadata.value(QStringLiteral("languages"))
                        .toObject();
                const QJsonArray publicStructure =
                    publicLogoMetadata.value(QStringLiteral("fileStructure"))
                        .toArray();
                const QJsonArray publicFrameworks =
                    publicLogoMetadata.value(QStringLiteral("frameworks"))
                        .toArray();
                check(logoSnapshot.complete &&
                          publicLanguages.contains(QStringLiteral("C++")) &&
                          publicLanguages.contains(
                              QStringLiteral("TypeScript")) &&
                          publicStructure.contains(
                              QStringLiteral("src/")) &&
                          publicStructure.contains(
                              QStringLiteral("web/")) &&
                          publicFrameworks.contains(
                              QStringLiteral("CMake")) &&
                          publicFrameworks.contains(
                              QStringLiteral("Node.js")) &&
                          publicFrameworks.contains(
                              QStringLiteral("Cloudflare Workers")) &&
                          publicLogoMetadata
                                  .value(QStringLiteral("projectCategory"))
                                  .toString() ==
                              QStringLiteral("developer platform"),
                      "native public logo factors reuse a real contribution snapshot and bounded git metadata");

                logoInput.contributionPayload = QJsonObject();
                const QJsonObject privateLogoMetadata =
                    buildRepoLogoMetadata(logoInput);
                const QByteArray serializedPrivateFactors =
                    QJsonDocument(privateLogoMetadata)
                        .toJson(QJsonDocument::Compact);
                check(privateLogoMetadata
                              .value(QStringLiteral("languages"))
                              .toObject()
                              .contains(QStringLiteral("C++")) &&
                          privateLogoMetadata
                              .value(QStringLiteral("fileStructure"))
                              .toArray()
                              .contains(QStringLiteral("src/")) &&
                          !serializedPrivateFactors.contains(
                              privateSourceMarker),
                      "native private logo factors use git tree metadata without publishing source contents");
            }

            RepoContributionSnapshotInput mismatched = input;
            mismatched.branch = "missing-branch";
            const RepoContributionSnapshot rejected =
                buildRepoContributionSnapshot(mismatched);
            check(!rejected.complete && !rejected.error.isEmpty() &&
                      rejected.error.size() <= 240,
                  "snapshot rejects an unstable or mismatched repository ref");

            RepoContributionSnapshotInput paddedBranch = input;
            paddedBranch.branch = " main ";
            const RepoContributionSnapshot paddedBranchResult =
                buildRepoContributionSnapshot(paddedBranch);
            check(!paddedBranchResult.complete &&
                      !paddedBranchResult.error.isEmpty() &&
                      paddedBranchResult.error.size() <= 240,
                  "snapshot rejects a branch with surrounding whitespace");

            const QString longBranch(121, QLatin1Char('b'));
            const bool longBranchCreated =
                runTestGit(dir, {QStringLiteral("branch"), longBranch, input.head});
            check(longBranchCreated,
                  "snapshot fixture creates a real 121-character branch");
            if (longBranchCreated) {
                RepoContributionSnapshotInput longBranchInput = input;
                longBranchInput.branch = longBranch;
                const RepoContributionSnapshot longBranchResult =
                    buildRepoContributionSnapshot(longBranchInput);
                check(!longBranchResult.complete &&
                          !longBranchResult.error.isEmpty() &&
                          longBranchResult.error.size() <= 240,
                      "snapshot rejects a branch longer than 120 characters");
            }

            const QString supplementaryBranch =
                QStringLiteral("feature-") +
                QString::fromUtf8("\xF0\x9F\x9A\x80");
            const bool supplementaryBranchCreated = runTestGit(
                dir, {QStringLiteral("branch"), supplementaryBranch, input.head});
            check(supplementaryBranchCreated,
                  "snapshot fixture creates a supplementary-Unicode branch");
            if (supplementaryBranchCreated) {
                RepoContributionSnapshotInput supplementaryBranchInput = input;
                supplementaryBranchInput.branch = supplementaryBranch;
                const RepoContributionSnapshot supplementaryBranchResult =
                    buildRepoContributionSnapshot(supplementaryBranchInput);
                check(supplementaryBranchResult.complete &&
                          supplementaryBranchResult.payload.value("branch")
                                  .toString() == supplementaryBranch,
                      "snapshot accepts non-control supplementary Unicode");
            }

            const QString unsafeSeparator(QChar(0x00a0));
            RepoContributionSnapshotInput unsafeBranch = input;
            unsafeBranch.branch = unsafeSeparator + "main" + unsafeSeparator;
            const RepoContributionSnapshot unsafeBranchResult =
                buildRepoContributionSnapshot(unsafeBranch);
            check(!unsafeBranchResult.complete &&
                      !unsafeBranchResult.error.isEmpty() &&
                      unsafeBranchResult.error.size() <= 240,
                  "snapshot rejects Unicode separators in a branch");

            RepoContributionSnapshotInput paddedHead = input;
            paddedHead.head = " " + input.head + " ";
            const RepoContributionSnapshot paddedHeadResult =
                buildRepoContributionSnapshot(paddedHead);
            check(!paddedHeadResult.complete &&
                      !paddedHeadResult.error.isEmpty() &&
                      paddedHeadResult.error.size() <= 240,
                  "snapshot rejects a head with surrounding whitespace");

            RepoContributionSnapshotInput longHead = input;
            longHead.head = QString(65, QLatin1Char('a'));
            const RepoContributionSnapshot longHeadResult =
                buildRepoContributionSnapshot(longHead);
            check(!longHeadResult.complete && !longHeadResult.error.isEmpty() &&
                      longHeadResult.error.size() <= 240,
                  "snapshot rejects a head longer than 64 characters");

            std::mutex hookMutex;
            std::condition_variable hookCondition;
            bool hookInstalled = false;
            bool startHookOwner = false;
            std::atomic<int> hookCalls = 0;
            std::atomic<bool> hookRanOnOwner = false;
            RepoContributionSnapshot hookOwnerSnapshot;
            RepoContributionSnapshot otherThreadSnapshot;
            std::thread hookOwner([&] {
                const std::thread::id ownerId = std::this_thread::get_id();
                RepoContributionSnapshotInternal::
                    setBeforePullMetadataRecheckHookForTests([&] {
                        ++hookCalls;
                        hookRanOnOwner = std::this_thread::get_id() == ownerId;
                    });
                {
                    std::lock_guard<std::mutex> lock(hookMutex);
                    hookInstalled = true;
                }
                hookCondition.notify_one();
                {
                    std::unique_lock<std::mutex> lock(hookMutex);
                    hookCondition.wait(lock, [&] { return startHookOwner; });
                }
                hookOwnerSnapshot = buildRepoContributionSnapshot(input);
            });
            {
                std::unique_lock<std::mutex> lock(hookMutex);
                hookCondition.wait(lock, [&] { return hookInstalled; });
            }
            std::thread otherBuilder([&] {
                otherThreadSnapshot = buildRepoContributionSnapshot(input);
            });
            otherBuilder.join();
            {
                std::lock_guard<std::mutex> lock(hookMutex);
                startHookOwner = true;
            }
            hookCondition.notify_one();
            hookOwner.join();
            check(hookOwnerSnapshot.complete && otherThreadSnapshot.complete &&
                      hookCalls == 1 && hookRanOnOwner,
                  "snapshot hooks are isolated between concurrent builders");

            QTemporaryDir pullRaceMirrorRoot;
            const QString pullRaceMirror =
                pullRaceMirrorRoot.path() + QStringLiteral("/race.git");
            const bool pullRaceSetup =
                pullRaceMirrorRoot.isValid() &&
                runTestGit(pullRaceMirrorRoot.path(),
                           {QStringLiteral("clone"), QStringLiteral("-q"),
                            QStringLiteral("--bare"), dir, pullRaceMirror}) &&
                runTestGit(pullRaceMirror,
                           {QStringLiteral("config"),
                            QStringLiteral("user.name"),
                            QStringLiteral("Owner Name")}) &&
                runTestGit(pullRaceMirror,
                           {QStringLiteral("config"),
                            QStringLiteral("user.email"),
                            QStringLiteral("owner@example.test")});
            check(pullRaceSetup,
                  "pull-ref-race snapshot mirror is created");
            if (pullRaceSetup) {
                RepoContributionSnapshotInput raceInput = input;
                raceInput.workTreePath.clear();
                raceInput.mirrorPath = pullRaceMirror;
                bool raceHookRan = false;
                bool pullRefMoved = false;
                RepoContributionSnapshotInternal::
                    setBeforePullMetadataRecheckHookForTests([&] {
                        raceHookRan = true;
                        pullRefMoved = runTestGit(
                            pullRaceMirror,
                            {QStringLiteral("update-ref"),
                             QStringLiteral("refs/heads/forkmesh/pulls"),
                             raceInput.head});
                    });
                const RepoContributionSnapshot raced =
                    buildRepoContributionSnapshot(raceInput);
                RepoContributionSnapshotInternal::
                    setBeforePullMetadataRecheckHookForTests({});
                int racedCommits = 0;
                int racedIssues = 0;
                int racedPulls = 0;
                int racedReviews = 0;
                for (const QJsonValue &value :
                     raced.payload.value("days").toArray()) {
                    const QJsonArray row = value.toArray();
                    racedCommits += row.at(2).toInt();
                    racedIssues += row.at(3).toInt();
                    racedPulls += row.at(4).toInt();
                    racedReviews += row.at(5).toInt();
                }
                check(raceHookRan && pullRefMoved && raced.complete &&
                          raced.error.isEmpty() && racedCommits == 3 &&
                          racedIssues == 1 && racedPulls == 0 &&
                          racedReviews == 0 &&
                          raced.payload.value("coverage")
                                  .toObject()
                                  .value("collaboration")
                                  .toString() == "partial" &&
                          raced.payload.value("coverage")
                                  .toObject()
                                  .value("languages")
                                  .toString() == "complete",
                      "snapshot discards pull rows when metadata ref changes");
            }
        }

        QTemporaryDir corruptIssueRepo;
        bool corruptIssueSetup =
            corruptIssueRepo.isValid() &&
            runTestGit(corruptIssueRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(corruptIssueRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(corruptIssueRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(corruptIssueRepo.path() + "/source.cpp", "x") &&
            writeTestFile(corruptIssueRepo.path() +
                              "/.forkmesh/issues/1/issue-1.json",
                          "{not-json") &&
            commitTestTree(corruptIssueRepo.path(), "corrupt issue metadata",
                           "2026-07-12T09:00:00Z", "Owner Name",
                           "owner@example.test");
        check(corruptIssueSetup,
              "corrupt-issue snapshot repository is created");
        if (corruptIssueSetup) {
            RepoContributionSnapshotInput input;
            input.workTreePath = corruptIssueRepo.path();
            input.branch = "main";
            input.head = testGitHead(corruptIssueRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            check(snapshot.complete &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("collaboration")
                              .toString() == "partial",
                  "snapshot marks unreadable issue metadata partial");

            // adhoc #6: the Issues tab counts loadAll's rows while the Mirror
            // nodes tab counts countOpenIssues (which treats an unreadable blob
            // as open). A corrupt/unreadable issue-N.json must therefore survive
            // as an open placeholder in the permissive load, not vanish, or the
            // tab silently shrinks below the mirror's count.
            IssueStore corruptStore(QString(), corruptIssueRepo.path(), nullptr);
            QString corruptError;
            const QList<Issue> corruptIssues = corruptStore.loadAll(&corruptError);
            check(corruptIssues.size() == 1 &&
                      corruptIssues.first().number == 1 &&
                      corruptIssues.first().status == QStringLiteral("open"),
                  "unreadable issue blob survives as an open placeholder row");

            // Strict verification still drops the corrupt record and reports it
            // rather than fabricating a placeholder.
            QString corruptStrictError;
            const QList<Issue> corruptStrict =
                corruptStore.loadAllStrict(&corruptStrictError);
            check(corruptStrict.isEmpty() && !corruptStrictError.isEmpty(),
                  "strict issue load drops the corrupt record and reports it");

            // adhoc #7: the node's OWN writable checkout (canWrite() == true) took
            // a separate on-disk loop that silently dropped an unreadable
            // issue-N.json, so the Issues tab undercounted below the mirror's open
            // count for the source-of-truth node. It must keep the same open
            // placeholder the read-only mirror load does.
            IssueStore writableStore(corruptIssueRepo.path(), QString(), &identity,
                                     "owner");
            check(writableStore.canWrite(),
                  "writable issue store recognises the local checkout");
            QString writableError;
            const QList<Issue> writableIssues =
                writableStore.loadAll(&writableError);
            check(writableIssues.size() == 1 &&
                      writableIssues.first().number == 1 &&
                      writableIssues.first().status == QStringLiteral("open"),
                  "unreadable issue blob survives as open placeholder on the "
                  "writable path too");
        }

        auto semanticIssueSnapshot = [&](const QByteArray &issueJson,
                                         const char *setupLabel) {
            QTemporaryDir repo;
            const bool setup =
                repo.isValid() &&
                runTestGit(repo.path(),
                           {QStringLiteral("init"), QStringLiteral("-q"),
                            QStringLiteral("-b"), QStringLiteral("main")}) &&
                runTestGit(repo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.name"),
                            QStringLiteral("Owner Name")}) &&
                runTestGit(repo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.email"),
                            QStringLiteral("owner@example.test")}) &&
                writeTestFile(repo.path() + "/source.cpp", "x") &&
                writeTestFile(repo.path() +
                                  "/.forkmesh/issues/1/issue-1.json",
                              issueJson) &&
                commitTestTree(repo.path(), "semantic issue corruption",
                               "2026-07-12T09:00:00Z", "Owner Name",
                               "owner@example.test");
            check(setup, setupLabel);
            if (!setup)
                return RepoContributionSnapshot{};

            IssueStore permissiveStore(QString(), repo.path(), nullptr);
            QString permissiveError;
            permissiveStore.loadAll(&permissiveError);
            check(permissiveError.isEmpty(),
                  "normal issue loading remains permissive for semantic corruption");

            RepoContributionSnapshotInput input;
            input.workTreePath = repo.path();
            input.branch = "main";
            input.head = testGitHead(repo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            return buildRepoContributionSnapshot(input);
        };

        const RepoContributionSnapshot missingSchemaSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral("{\"number\":1,\"events\":[]}"),
                "missing-schema issue repository is created");
        check(missingSchemaSnapshot.complete &&
                  missingSchemaSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot marks missing issue schema partial");

        const RepoContributionSnapshot wrongSchemaSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"wrong\",\"number\":1,\"events\":[]}"),
                "wrong-schema issue repository is created");
        check(wrongSchemaSnapshot.complete &&
                  wrongSchemaSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot marks wrong issue schema partial");

        const RepoContributionSnapshot nonArrayEventsSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,\"events\":{}}"),
                "non-array-events issue repository is created");
        check(nonArrayEventsSnapshot.complete &&
                  nonArrayEventsSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot marks non-array issue events partial");

        const RepoContributionSnapshot nonObjectEventSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,\"events\":[7]}"),
                "non-object-event issue repository is created");
        check(nonObjectEventSnapshot.complete &&
                  nonObjectEventSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot marks non-object issue events partial");

        const RepoContributionSnapshot maxSafeTimestampSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"actor\","
                    "\"ts\":9007199254740991,\"sig\":\"signature\","
                    "\"title\":\"title\",\"body\":\"body\","
                    "\"attachments\":[]}]}"),
                "max-safe-timestamp issue repository is created");
        check(maxSafeTimestampSnapshot.complete &&
                  maxSafeTimestampSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "complete",
              "snapshot accepts the exact maximum JSON-safe issue timestamp");

        const RepoContributionSnapshot aboveSafeTimestampSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"actor\","
                    "\"ts\":9007199254740992,\"sig\":\"signature\","
                    "\"title\":\"title\",\"body\":\"body\","
                    "\"attachments\":[]}]}"),
                "above-safe-timestamp issue repository is created");
        check(aboveSafeTimestampSnapshot.complete &&
                  aboveSafeTimestampSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects an issue timestamp above the JSON-safe maximum");

        const RepoContributionSnapshot twoTo63TimestampSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"actor\","
                    "\"ts\":9223372036854775808,\"sig\":\"signature\","
                    "\"title\":\"title\",\"body\":\"body\","
                    "\"attachments\":[]}]}"),
                "two-to-63-timestamp issue repository is created");
        check(twoTo63TimestampSnapshot.complete &&
                  twoTo63TimestampSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects a two-to-the-63 issue timestamp");

        const RepoContributionSnapshot emptyIssueAuthorSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"\","
                    "\"ts\":1,\"sig\":\"signature\",\"title\":\"title\","
                    "\"body\":\"body\",\"attachments\":[]}]}"),
                "empty-author issue repository is created");
        check(emptyIssueAuthorSnapshot.complete &&
                  emptyIssueAuthorSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects an empty issue event author");

        const RepoContributionSnapshot emptyIssueSignatureSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"actor\","
                    "\"ts\":1,\"sig\":\"\",\"title\":\"title\","
                    "\"body\":\"body\",\"attachments\":[]}]}"),
                "empty-signature issue repository is created");
        check(emptyIssueSignatureSnapshot.complete &&
                  emptyIssueSignatureSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects an empty issue event signature");

        const RepoContributionSnapshot nonStringAttachmentSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"events\":[{\"type\":\"open\",\"author\":\"actor\","
                    "\"ts\":1,\"sig\":\"signature\",\"title\":\"title\","
                    "\"body\":\"body\",\"attachments\":[7]}]}"),
                "non-string-attachment issue repository is created");
        check(nonStringAttachmentSnapshot.complete &&
                  nonStringAttachmentSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects a non-string issue attachment");

        const RepoContributionSnapshot maxSafeTopLevelDatesSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"createdAt\":9007199254740991,"
                    "\"startDate\":9007199254740991,"
                    "\"endDate\":9007199254740991,\"events\":[]}"),
                "max-safe top-level issue dates repository is created");
        check(maxSafeTopLevelDatesSnapshot.complete &&
                  maxSafeTopLevelDatesSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "complete",
              "snapshot accepts max-safe top-level issue dates");

        const RepoContributionSnapshot aboveSafeCreatedAtSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"createdAt\":9007199254740992,\"events\":[]}"),
                "above-safe createdAt issue repository is created");
        check(aboveSafeCreatedAtSnapshot.complete &&
                  aboveSafeCreatedAtSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects top-level createdAt above max-safe");

        const RepoContributionSnapshot twoTo63StartDateSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"startDate\":9223372036854775808,\"events\":[]}"),
                "two-to-63 startDate issue repository is created");
        check(twoTo63StartDateSnapshot.complete &&
                  twoTo63StartDateSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects top-level startDate at two-to-the-63");

        const RepoContributionSnapshot negativeEndDateSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"endDate\":-1,\"events\":[]}"),
                "negative endDate issue repository is created");
        check(negativeEndDateSnapshot.complete &&
                  negativeEndDateSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects a negative top-level endDate");

        const RepoContributionSnapshot fractionalCreatedAtSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"createdAt\":1.5,\"events\":[]}"),
                "fractional createdAt issue repository is created");
        check(fractionalCreatedAtSnapshot.complete &&
                  fractionalCreatedAtSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects a fractional top-level createdAt");

        const RepoContributionSnapshot overflowingExponentSnapshot =
            semanticIssueSnapshot(
                QByteArrayLiteral(
                    "{\"schema\":\"forkmesh-issue-v1\",\"number\":1,"
                    "\"createdAt\":1e999,\"events\":[]}"),
                "overflowing-exponent issue repository is created");
        check(overflowingExponentSnapshot.complete &&
                  overflowingExponentSnapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "partial",
              "snapshot rejects a non-finite-like top-level issue date");

        QTemporaryDir strictBudgetRepo;
        auto budgetIssueJson = [](int number) {
            const QJsonObject openEvent{
                {QStringLiteral("type"), QStringLiteral("open")},
                {QStringLiteral("author"), QStringLiteral("actor")},
                {QStringLiteral("ts"), 1},
                {QStringLiteral("sig"), QStringLiteral("signature")},
                {QStringLiteral("title"),
                 QStringLiteral("budget issue %1").arg(number)},
                {QStringLiteral("body"), QString(128, QLatin1Char('x'))},
                {QStringLiteral("attachments"), QJsonArray{}}};
            return QJsonDocument(QJsonObject{
                                     {QStringLiteral("schema"),
                                      QStringLiteral("forkmesh-issue-v1")},
                                     {QStringLiteral("number"), number},
                                     {QStringLiteral("events"),
                                      QJsonArray{openEvent}}})
                .toJson(QJsonDocument::Compact);
        };
        const bool strictBudgetSetup =
            strictBudgetRepo.isValid() &&
            runTestGit(strictBudgetRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(strictBudgetRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(strictBudgetRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(strictBudgetRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("x")) &&
            writeTestFile(strictBudgetRepo.path() +
                              QStringLiteral(
                                  "/.forkmesh/issues/1/issue-1.json"),
                          budgetIssueJson(1)) &&
            writeTestFile(strictBudgetRepo.path() +
                              QStringLiteral(
                                  "/.forkmesh/issues/2/issue-2.json"),
                          budgetIssueJson(2)) &&
            commitTestTree(strictBudgetRepo.path(),
                           QStringLiteral("strict budget metadata"),
                           QStringLiteral("2026-07-12T09:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test"));
        check(strictBudgetSetup,
              "strict metadata budget repository is created");
        if (strictBudgetSetup) {
            IssueStore permissiveStore(QString(), strictBudgetRepo.path(),
                                       nullptr);
            QString permissiveError;
            check(permissiveStore.loadAll(&permissiveError).size() == 2 &&
                      permissiveError.isEmpty(),
                  "normal issue loading ignores strict read budgets");

            StrictGitReadInternal::Limits limits;
            limits.maxStdoutBytes = 4096;
            limits.maxStderrBytes = 256;
            limits.maxTotalBytes = 400;
            const StrictGitReadInternal::ScopedLimitsForTests scopedLimits(
                limits);
            RepoContributionSnapshotInput input;
            input.workTreePath = strictBudgetRepo.path();
            input.branch = QStringLiteral("main");
            input.head = testGitHead(strictBudgetRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs(QStringLiteral("2026-07-13T12:00:00Z"));
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            check(snapshot.complete &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("collaboration")
                              .toString() == "partial",
                  "snapshot marks cumulative strict metadata overflow partial");
        }

        QTemporaryDir writableIssueRepo;
        const bool writableIssueSetup =
            writableIssueRepo.isValid() &&
            runTestGit(writableIssueRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(writableIssueRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(writableIssueRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")});
        check(writableIssueSetup,
              "writable strict issue repository is created");
        if (writableIssueSetup) {
            IssueStore writableStore(writableIssueRepo.path(), QString(),
                                     &identity, QStringLiteral("owner"));
            QString writeError;
            const int firstIssue = writableStore.createIssue(
                QStringLiteral("First"), QStringLiteral("body"), {}, QString(),
                0, {}, {}, &writeError);
            const QString firstIssueRef = testGitHead(writableIssueRepo.path());
            const int secondIssue = writableStore.createIssue(
                QStringLiteral("Second"), QStringLiteral("body"), {}, QString(),
                0, {}, {}, &writeError);
            const bool semanticCorruptionWritten =
                firstIssue == 1 && secondIssue == 2 &&
                !firstIssueRef.isEmpty() &&
                writeTestFile(
                    writableIssueRepo.path() +
                        QStringLiteral("/.forkmesh/issues/open/2/issue-2.json"),
                    QByteArrayLiteral("{\"number\":2,\"events\":[]}")) &&
                commitTestTree(writableIssueRepo.path(),
                               QStringLiteral("corrupt current issue metadata"),
                               QStringLiteral("2026-07-12T10:00:00Z"),
                               QStringLiteral("Owner Name"),
                               QStringLiteral("owner@example.test"));
            check(semanticCorruptionWritten,
                  "writable issue history has a valid older ref and corrupt head");
            QString atRefError;
            const QList<Issue> historicalIssues =
                writableStore.loadAllStrictAtRef(firstIssueRef, &atRefError);
            QString strictHeadError;
            writableStore.loadAllStrict(&strictHeadError);
            check(historicalIssues.size() == 1 &&
                      historicalIssues.constFirst().number == 1 &&
                      atRefError.isEmpty(),
                  "writable strict issue reads honor the supplied older ref");
            check(!strictHeadError.isEmpty(),
                  "writable strict issue reads validate the current ref");
        }

        QTemporaryDir corruptPullRepo;
        bool corruptPullSetup =
            corruptPullRepo.isValid() &&
            runTestGit(corruptPullRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(corruptPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(corruptPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(corruptPullRepo.path() + "/source.cpp", "x") &&
            writeTestFile(corruptPullRepo.path() + "/pulls/1/pull.md",
                          "not front matter\n") &&
            commitTestTree(corruptPullRepo.path(), "corrupt pull metadata",
                           "2026-07-12T09:00:00Z", "Owner Name",
                           "owner@example.test");
        check(corruptPullSetup,
              "corrupt-pull snapshot repository is created");
        if (corruptPullSetup) {
            RepoContributionSnapshotInput input;
            input.workTreePath = corruptPullRepo.path();
            input.branch = "main";
            input.head = testGitHead(corruptPullRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            check(snapshot.complete &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("collaboration")
                              .toString() == "partial",
                  "snapshot marks unreadable pull metadata partial");
        }

        auto pullFrontMatter = [](const QString &schema,
                                  const QString &declaredNumber,
                                  bool branchBacked = false) {
            QStringList lines{QStringLiteral("---")};
            if (!schema.isNull())
                lines << QStringLiteral("schema: ") + schema;
            if (!declaredNumber.isNull())
                lines << QStringLiteral("number: ") + declaredNumber;
            lines << QStringLiteral("title: title")
                  << QStringLiteral("base: main")
                  << QStringLiteral("head: feature")
                  << QStringLiteral("status: open");
            if (branchBacked)
                lines << QStringLiteral("derive: branch");
            lines << QStringLiteral("ts: 1")
                  << QStringLiteral("author: actor")
                  << QStringLiteral("sig: signature")
                  << QStringLiteral("---") << QString() <<
                QStringLiteral("description");
            return lines.join(QLatin1Char('\n')).toUtf8() + '\n';
        };

        auto semanticPullSnapshot = [&](const QByteArray &pullMetadata,
                                        bool includePatch, bool includeMbox,
                                        bool expectStrictRejection,
                                        const char *setupLabel) {
            QTemporaryDir repo;
            bool setup =
                repo.isValid() &&
                runTestGit(repo.path(),
                           {QStringLiteral("init"), QStringLiteral("-q"),
                            QStringLiteral("-b"), QStringLiteral("main")}) &&
                runTestGit(repo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.name"),
                            QStringLiteral("Owner Name")}) &&
                runTestGit(repo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("user.email"),
                            QStringLiteral("owner@example.test")}) &&
                writeTestFile(repo.path() + QStringLiteral("/source.cpp"),
                              QByteArrayLiteral("x")) &&
                writeTestFile(repo.path() +
                                  QStringLiteral("/pulls/1/pull.md"),
                              pullMetadata);
            if (includePatch) {
                setup = setup &&
                        writeTestFile(
                            repo.path() +
                                QStringLiteral("/pulls/1/changes.patch"),
                            QByteArrayLiteral(
                                "diff --git a/a b/a\n--- a/a\n+++ b/a\n"));
            }
            if (includeMbox) {
                setup = setup &&
                        writeTestFile(
                            repo.path() +
                                QStringLiteral("/pulls/1/commits.mbox"),
                            QByteArrayLiteral("From fixture\n"));
            }
            setup = setup &&
                    commitTestTree(repo.path(),
                                   QStringLiteral("semantic pull corruption"),
                                   QStringLiteral("2026-07-12T09:00:00Z"),
                                   QStringLiteral("Owner Name"),
                                   QStringLiteral("owner@example.test"));
            check(setup, setupLabel);
            if (!setup)
                return RepoContributionSnapshot{};

            PullStore permissiveStore(QString(), repo.path(), nullptr);
            QString permissiveError;
            const QList<PullRequest> permissivePulls =
                permissiveStore.loadAll(&permissiveError);
            check(permissiveError.isEmpty() && permissivePulls.size() == 1,
                  "normal pull loading remains permissive for semantic corruption");
            if (expectStrictRejection) {
                QString strictError;
                const QList<PullRequest> strictPulls =
                    permissiveStore.loadAllStrict(&strictError);
                check(!strictError.isEmpty() && strictPulls.isEmpty(),
                      "strict pull loading excludes invalid front matter");
            }

            RepoContributionSnapshotInput input;
            input.workTreePath = repo.path();
            input.branch = QStringLiteral("main");
            input.head = testGitHead(repo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs(QStringLiteral("2026-07-13T12:00:00Z"));
            return buildRepoContributionSnapshot(input);
        };

        auto collaborationIsPartial = [](const RepoContributionSnapshot &snapshot) {
            return snapshot.complete &&
                   snapshot.payload.value(QStringLiteral("coverage"))
                           .toObject()
                           .value(QStringLiteral("collaboration"))
                           .toString() == QLatin1String("partial");
        };

        const RepoContributionSnapshot missingPullSchemaSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QString(), QStringLiteral("1")), true, false, true,
                "missing-schema pull repository is created");
        check(collaborationIsPartial(missingPullSchemaSnapshot),
              "snapshot marks a missing pull schema partial");

        const RepoContributionSnapshot wrongPullSchemaSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("wrong"), QStringLiteral("1")),
                true, false, true, "wrong-schema pull repository is created");
        check(collaborationIsPartial(wrongPullSchemaSnapshot),
              "snapshot marks a wrong pull schema partial");

        const RepoContributionSnapshot missingPullNumberSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"), QString()),
                true, false, true,
                "missing-number pull repository is created");
        check(collaborationIsPartial(missingPullNumberSnapshot),
              "snapshot marks a missing declared pull number partial");

        const RepoContributionSnapshot mismatchedPullNumberSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("2")),
                true, false, true,
                "mismatched-number pull repository is created");
        check(collaborationIsPartial(mismatchedPullNumberSnapshot),
              "snapshot marks a mismatched declared pull number partial");

        const RepoContributionSnapshot nonNumericPullNumberSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("one")),
                true, false, true,
                "nonnumeric-number pull repository is created");
        check(collaborationIsPartial(nonNumericPullNumberSnapshot),
              "snapshot marks a nonnumeric declared pull number partial");

        const RepoContributionSnapshot zeroPullNumberSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("0")),
                true, false, true,
                "zero-number pull repository is created");
        check(collaborationIsPartial(zeroPullNumberSnapshot),
              "snapshot rejects a non-positive pull number");

        auto pullWithField = [&](const QByteArray &field,
                                 const QByteArray &value) {
            QByteArray metadata =
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("1"));
            metadata.replace(field + QByteArrayLiteral(": 1\n"),
                             field + QByteArrayLiteral(": ") + value + '\n');
            return metadata;
        };
        auto strictPullTimestampSnapshot = [&](const QByteArray &value,
                                               bool rejected,
                                               const char *label) {
            return semanticPullSnapshot(
                pullWithField(QByteArrayLiteral("ts"), value), true, false,
                rejected, label);
        };
        const RepoContributionSnapshot maxSafePullTimestampSnapshot =
            strictPullTimestampSnapshot(
                QByteArrayLiteral("9007199254740991"), false,
                "max-safe pull timestamp repository is created");
        check(!collaborationIsPartial(maxSafePullTimestampSnapshot),
              "snapshot accepts the exact maximum JSON-safe pull timestamp");
        const RepoContributionSnapshot aboveSafePullTimestampSnapshot =
            strictPullTimestampSnapshot(
                QByteArrayLiteral("9007199254740992"), true,
                "above-safe pull timestamp repository is created");
        check(collaborationIsPartial(aboveSafePullTimestampSnapshot),
              "snapshot rejects a pull timestamp above the JSON-safe maximum");
        const RepoContributionSnapshot twoTo63PullTimestampSnapshot =
            strictPullTimestampSnapshot(
                QByteArrayLiteral("9223372036854775808"), true,
                "two-to-63 pull timestamp repository is created");
        check(collaborationIsPartial(twoTo63PullTimestampSnapshot),
              "snapshot rejects a two-to-the-63 pull timestamp");
        const RepoContributionSnapshot negativePullTimestampSnapshot =
            strictPullTimestampSnapshot(
                QByteArrayLiteral("-1"), true,
                "negative pull timestamp repository is created");
        check(collaborationIsPartial(negativePullTimestampSnapshot),
              "snapshot rejects a negative pull timestamp");
        const RepoContributionSnapshot fractionalPullTimestampSnapshot =
            strictPullTimestampSnapshot(
                QByteArrayLiteral("1.5"), true,
                "fractional pull timestamp repository is created");
        check(collaborationIsPartial(fractionalPullTimestampSnapshot),
              "snapshot rejects a fractional pull timestamp");

        QByteArray emptyPullAuthor =
            pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                            QStringLiteral("1"));
        emptyPullAuthor.replace(QByteArrayLiteral("author: actor\n"),
                                QByteArrayLiteral("author: \n"));
        const RepoContributionSnapshot emptyPullAuthorSnapshot =
            semanticPullSnapshot(
                emptyPullAuthor, true, false, true,
                "empty-author pull repository is created");
        check(collaborationIsPartial(emptyPullAuthorSnapshot),
              "snapshot rejects an empty pull author");
        QByteArray emptyPullSignature =
            pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                            QStringLiteral("1"));
        emptyPullSignature.replace(QByteArrayLiteral("sig: signature\n"),
                                   QByteArrayLiteral("sig: \n"));
        const RepoContributionSnapshot emptyPullSignatureSnapshot =
            semanticPullSnapshot(
                emptyPullSignature, true, false, true,
                "empty-signature pull repository is created");
        check(collaborationIsPartial(emptyPullSignatureSnapshot),
              "snapshot rejects an empty pull signature");

        const RepoContributionSnapshot mboxOnlyPullSnapshot =
            semanticPullSnapshot(
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("1")),
                false, true, false, "mbox-only pull repository is created");
        check(collaborationIsPartial(mboxOnlyPullSnapshot),
              "snapshot marks a stored pull with no changes patch partial");

        auto pullEventFrontMatter = [](const QString &type,
                                       const QString &timestamp,
                                       const QString &author,
                                       const QString &signature) {
            const QStringList lines{
                QStringLiteral("---"), QStringLiteral("type: ") + type,
                QStringLiteral("id: fixture-event"),
                QStringLiteral("author: ") + author,
                QStringLiteral("ts: ") + timestamp,
                QStringLiteral("sig: ") + signature,
                QStringLiteral("---"), QString(), QStringLiteral("body")};
            return lines.join(QLatin1Char('\n')).toUtf8() + '\n';
        };
        auto semanticPullEventSnapshot =
            [&](const QByteArray &eventMetadata, bool rejected,
                const char *setupLabel) {
                QTemporaryDir repo;
                const bool setup =
                    repo.isValid() &&
                    runTestGit(repo.path(),
                               {QStringLiteral("init"), QStringLiteral("-q"),
                                QStringLiteral("-b"),
                                QStringLiteral("main")}) &&
                    runTestGit(repo.path(),
                               {QStringLiteral("config"),
                                QStringLiteral("user.name"),
                                QStringLiteral("Owner Name")}) &&
                    runTestGit(repo.path(),
                               {QStringLiteral("config"),
                                QStringLiteral("user.email"),
                                QStringLiteral("owner@example.test")}) &&
                    writeTestFile(repo.path() +
                                      QStringLiteral("/source.cpp"),
                                  QByteArrayLiteral("x")) &&
                    writeTestFile(
                        repo.path() + QStringLiteral("/pulls/1/pull.md"),
                        pullFrontMatter(
                            QStringLiteral("forkmesh-pull-v1"),
                            QStringLiteral("1"))) &&
                    writeTestFile(
                        repo.path() +
                            QStringLiteral("/pulls/1/changes.patch"),
                        QByteArrayLiteral(
                            "diff --git a/a b/a\n--- a/a\n+++ b/a\n")) &&
                    writeTestFile(
                        repo.path() +
                            QStringLiteral("/pulls/1/0001-comment.md"),
                        eventMetadata) &&
                    commitTestTree(
                        repo.path(), QStringLiteral("pull event schema"),
                        QStringLiteral("2026-07-12T09:00:00Z"),
                        QStringLiteral("Owner Name"),
                        QStringLiteral("owner@example.test"));
                check(setup, setupLabel);
                if (!setup)
                    return RepoContributionSnapshot{};

                PullStore store(QString(), repo.path(), nullptr);
                QString permissiveError;
                const QList<PullRequest> permissive =
                    store.loadAll(&permissiveError);
                check(permissiveError.isEmpty() && permissive.size() == 1 &&
                          permissive.constFirst().events.size() == 1,
                      "normal pull event loading remains permissive for semantic corruption");
                QString strictError;
                const QList<PullRequest> strict =
                    store.loadAllStrict(&strictError);
                check(strict.size() == 1 &&
                          strict.constFirst().events.size() ==
                              (rejected ? 0 : 1) &&
                          (rejected ? !strictError.isEmpty()
                                    : strictError.isEmpty()),
                      rejected
                          ? "strict pull loading excludes an invalid event"
                          : "strict pull loading accepts a valid boundary event");

                RepoContributionSnapshotInput input;
                input.workTreePath = repo.path();
                input.branch = QStringLiteral("main");
                input.head = testGitHead(repo.path());
                input.publishingKey = identity.publicKey();
                input.capturedAtMs =
                    utcMs(QStringLiteral("2026-07-13T12:00:00Z"));
                return buildRepoContributionSnapshot(input);
            };

        const RepoContributionSnapshot maxSafePullEventSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"),
                    QStringLiteral("9007199254740991"),
                    QStringLiteral("actor"), QStringLiteral("signature")),
                false, "max-safe pull event repository is created");
        check(!collaborationIsPartial(maxSafePullEventSnapshot),
              "snapshot accepts the exact maximum JSON-safe pull event timestamp");
        const RepoContributionSnapshot aboveSafePullEventSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"),
                    QStringLiteral("9007199254740992"),
                    QStringLiteral("actor"), QStringLiteral("signature")),
                true, "above-safe pull event repository is created");
        check(collaborationIsPartial(aboveSafePullEventSnapshot),
              "snapshot rejects a pull event timestamp above the JSON-safe maximum");
        const RepoContributionSnapshot twoTo63PullEventSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"),
                    QStringLiteral("9223372036854775808"),
                    QStringLiteral("actor"), QStringLiteral("signature")),
                true, "two-to-63 pull event repository is created");
        check(collaborationIsPartial(twoTo63PullEventSnapshot),
              "snapshot rejects a two-to-the-63 pull event timestamp");
        const RepoContributionSnapshot negativePullEventSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"), QStringLiteral("-1"),
                    QStringLiteral("actor"), QStringLiteral("signature")),
                true, "negative pull event repository is created");
        check(collaborationIsPartial(negativePullEventSnapshot),
              "snapshot rejects a negative pull event timestamp");
        const RepoContributionSnapshot fractionalPullEventSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"), QStringLiteral("1.5"),
                    QStringLiteral("actor"), QStringLiteral("signature")),
                true, "fractional pull event repository is created");
        check(collaborationIsPartial(fractionalPullEventSnapshot),
              "snapshot rejects a fractional pull event timestamp");
        const RepoContributionSnapshot emptyPullEventTypeSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QString(), QStringLiteral("1"), QStringLiteral("actor"),
                    QStringLiteral("signature")),
                true, "empty-type pull event repository is created");
        check(collaborationIsPartial(emptyPullEventTypeSnapshot),
              "snapshot rejects an empty pull event type");
        const RepoContributionSnapshot emptyPullEventAuthorSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"), QStringLiteral("1"),
                    QString(), QStringLiteral("signature")),
                true, "empty-author pull event repository is created");
        check(collaborationIsPartial(emptyPullEventAuthorSnapshot),
              "snapshot rejects an empty pull event author");
        const RepoContributionSnapshot emptyPullEventSignatureSnapshot =
            semanticPullEventSnapshot(
                pullEventFrontMatter(
                    QStringLiteral("comment"), QStringLiteral("1"),
                    QStringLiteral("actor"), QString()),
                true, "empty-signature pull event repository is created");
        check(collaborationIsPartial(emptyPullEventSignatureSnapshot),
              "snapshot rejects an empty pull event signature");

        QTemporaryDir batchedPullRepo;
        bool batchedPullSetup =
            batchedPullRepo.isValid() &&
            runTestGit(batchedPullRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(batchedPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(batchedPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")});
        for (int number = 1; batchedPullSetup && number <= 2; ++number) {
            batchedPullSetup =
                writeTestFile(
                    batchedPullRepo.path() +
                        QStringLiteral("/pulls/%1/pull.md").arg(number),
                    pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                    QString::number(number))) &&
                writeTestFile(
                    batchedPullRepo.path() +
                        QStringLiteral("/pulls/%1/changes.patch").arg(number),
                    QByteArrayLiteral(
                        "diff --git a/a b/a\n--- a/a\n+++ b/a\n")) &&
                writeTestFile(
                    batchedPullRepo.path() +
                        QStringLiteral("/pulls/%1/0001-comment.md")
                            .arg(number),
                    pullEventFrontMatter(
                        QStringLiteral("comment"), QStringLiteral("1"),
                        QStringLiteral("actor"),
                        QStringLiteral("signature")));
        }
        batchedPullSetup =
            batchedPullSetup &&
            commitTestTree(batchedPullRepo.path(),
                           QStringLiteral("batched pull metadata"),
                           QStringLiteral("2026-07-12T09:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test"));
        check(batchedPullSetup,
              "batched strict pull repository is created");
        if (batchedPullSetup) {
            PullStore store(QString(), batchedPullRepo.path(), nullptr);
            QString permissiveError;
            const QList<PullRequest> permissive =
                store.loadAll(&permissiveError);
            check(permissiveError.isEmpty() && permissive.size() == 2,
                  "normal pull loading ignores strict item and command budgets");

            StrictGitReadInternal::Limits exactLimits;
            exactLimits.maxGitCommands = 2;
            exactLimits.maxPullItems = 2;
            exactLimits.maxEventItems = 2;
            QString exactError;
            QList<PullRequest> exactPulls;
            {
                StrictGitReadInternal::ScopedLimitsForTests scoped(exactLimits);
                exactPulls = store.loadAllStrictAtRef(
                    QStringLiteral("HEAD"), &exactError);
            }
            check(exactError.isEmpty() && exactPulls.size() == 2 &&
                      exactPulls.at(0).events.size() == 1 &&
                      exactPulls.at(1).events.size() == 1,
                  "strict pull batching accepts exact command and item caps");

            auto strictLoadWithLimits =
                [&](const StrictGitReadInternal::Limits &limits,
                    QString *strictError) {
                    StrictGitReadInternal::ScopedLimitsForTests scoped(limits);
                    return store.loadAllStrictAtRef(QStringLiteral("HEAD"),
                                                    strictError);
                };
            StrictGitReadInternal::Limits commandOverflow = exactLimits;
            commandOverflow.maxGitCommands = 1;
            QString commandError;
            const QList<PullRequest> commandLimited =
                strictLoadWithLimits(commandOverflow, &commandError);
            check(!commandError.isEmpty() && commandLimited.isEmpty(),
                  "strict pull batching rejects command-cap overflow");

            StrictGitReadInternal::Limits pullOverflow = exactLimits;
            pullOverflow.maxPullItems = 1;
            QString pullLimitError;
            const QList<PullRequest> pullLimited =
                strictLoadWithLimits(pullOverflow, &pullLimitError);
            check(!pullLimitError.isEmpty() && pullLimited.isEmpty(),
                  "strict pull batching rejects pull-item overflow");

            StrictGitReadInternal::Limits eventOverflow = exactLimits;
            eventOverflow.maxEventItems = 1;
            QString eventLimitError;
            const QList<PullRequest> eventLimited =
                strictLoadWithLimits(eventOverflow, &eventLimitError);
            check(!eventLimitError.isEmpty() && eventLimited.isEmpty(),
                  "strict pull batching rejects event-item overflow");

            StrictGitReadInternal::Limits deadlineOverflow = exactLimits;
            deadlineOverflow.maxElapsedMs = 0;
            QString deadlineError;
            const QList<PullRequest> deadlineLimited =
                strictLoadWithLimits(deadlineOverflow, &deadlineError);
            check(!deadlineError.isEmpty() && deadlineLimited.isEmpty(),
                  "strict pull batching enforces one aggregate elapsed deadline");
        }

        QTemporaryDir writablePullRepo;
        bool writablePullSetup =
            writablePullRepo.isValid() &&
            runTestGit(writablePullRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(writablePullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(writablePullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(writablePullRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("x")) &&
            writeTestFile(writablePullRepo.path() +
                              QStringLiteral("/pulls/1/pull.md"),
                          pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                          QStringLiteral("1"))) &&
            writeTestFile(writablePullRepo.path() +
                              QStringLiteral("/pulls/1/changes.patch"),
                          QByteArrayLiteral("patch one\n")) &&
            commitTestTree(writablePullRepo.path(),
                           QStringLiteral("valid historical pull"),
                           QStringLiteral("2026-07-12T09:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test"));
        const QString firstPullRef =
            writablePullSetup ? testGitHead(writablePullRepo.path()) : QString();
        writablePullSetup =
            writablePullSetup && !firstPullRef.isEmpty() &&
            writeTestFile(writablePullRepo.path() +
                              QStringLiteral("/pulls/2/pull.md"),
                          pullFrontMatter(QString(), QStringLiteral("2"))) &&
            writeTestFile(writablePullRepo.path() +
                              QStringLiteral("/pulls/2/changes.patch"),
                          QByteArrayLiteral("patch two\n")) &&
            commitTestTree(writablePullRepo.path(),
                           QStringLiteral("corrupt current pull metadata"),
                           QStringLiteral("2026-07-12T10:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test"));
        check(writablePullSetup,
              "writable pull history has a valid older ref and corrupt head");
        if (writablePullSetup) {
            PullStore writableStore(writablePullRepo.path(), QString(),
                                    &identity, QStringLiteral("owner"));
            QString atRefError;
            const QList<PullRequest> historicalPulls =
                writableStore.loadAllStrictAtRef(firstPullRef, &atRefError);
            QString strictHeadError;
            writableStore.loadAllStrict(&strictHeadError);
            check(historicalPulls.size() == 1 &&
                      historicalPulls.constFirst().number == 1 &&
                      atRefError.isEmpty(),
                  "writable strict pull reads honor the supplied older ref");
            check(!strictHeadError.isEmpty(),
                  "writable strict pull reads validate the current ref");
        }

        QTemporaryDir strictDeriveRepo;
        bool strictDeriveSetup =
            strictDeriveRepo.isValid() &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(strictDeriveRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("base\n")) &&
            commitTestTree(strictDeriveRepo.path(), QStringLiteral("base"),
                           QStringLiteral("2026-07-12T08:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test")) &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("checkout"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("feature")}) &&
            writeTestFile(strictDeriveRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("feature\n")) &&
            commitTestTree(strictDeriveRepo.path(), QStringLiteral("feature"),
                           QStringLiteral("2026-07-12T08:30:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test")) &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("checkout"), QStringLiteral("-q"),
                        QStringLiteral("main")}) &&
            writeTestFile(
                strictDeriveRepo.path() +
                    QStringLiteral("/pulls/1/pull.md"),
                pullFrontMatter(QStringLiteral("forkmesh-pull-v1"),
                                QStringLiteral("1"), true)) &&
            commitTestTree(strictDeriveRepo.path(),
                           QStringLiteral("branch pull metadata"),
                           QStringLiteral("2026-07-12T09:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test")) &&
            runTestGit(strictDeriveRepo.path(),
                       {QStringLiteral("config"),
                        QStringLiteral("diff.external"),
                        QStringLiteral("/definitely/missing/forkmesh-diff")});
        check(strictDeriveSetup,
              "strict branch-derive failure repository is created");
        if (strictDeriveSetup) {
            RepoContributionSnapshotInput input;
            input.workTreePath = strictDeriveRepo.path();
            input.branch = QStringLiteral("main");
            input.head = testGitHead(strictDeriveRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs(QStringLiteral("2026-07-13T12:00:00Z"));
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            check(collaborationIsPartial(snapshot),
                  "snapshot marks a failed strict branch derivation partial");
        }

        QTemporaryDir historicalPullRepo;
        bool historicalPullSetup =
            historicalPullRepo.isValid() &&
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Owner Name")}) &&
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("owner@example.test")}) &&
            writeTestFile(historicalPullRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("base\n")) &&
            commitTestTree(historicalPullRepo.path(), QStringLiteral("base"),
                           QStringLiteral("2026-07-14T08:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test")) &&
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("checkout"), QStringLiteral("-q"),
                        QStringLiteral("-b"),
                        QStringLiteral("historical-feature")}) &&
            writeTestFile(historicalPullRepo.path() +
                              QStringLiteral("/source.cpp"),
                          QByteArrayLiteral("first feature\n")) &&
            commitTestTree(historicalPullRepo.path(),
                           QStringLiteral("first feature"),
                           QStringLiteral("2026-07-14T09:00:00Z"),
                           QStringLiteral("Owner Name"),
                           QStringLiteral("owner@example.test")) &&
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("checkout"), QStringLiteral("-q"),
                        QStringLiteral("main")});
        check(historicalPullSetup,
              "historical branch pull repository is created");
        if (historicalPullSetup) {
            PullStore historicalPullStore(historicalPullRepo.path(), QString(),
                                          &identity,
                                          QStringLiteral("owner"));
            QString createError;
            const int pullNumber = historicalPullStore.createPull(
                QStringLiteral("Historical branch pull"),
                QStringLiteral("description"), QStringLiteral("main"),
                QStringLiteral("historical-feature"), QString(), QString(),
                true, &createError);
            const QString externalDiff =
                historicalPullRepo.path() + QStringLiteral("/external-diff.sh");
            const QString externalMarker = externalDiff +
                                           QStringLiteral(".invoked");
            const bool externalDiffConfigured =
                pullNumber == 1 && createError.isEmpty() &&
                writeTestFile(externalDiff,
                              QByteArrayLiteral(
                                  "#!/bin/sh\n: > \"$0.invoked\"\nexit 1\n")) &&
                QFile::setPermissions(
                    externalDiff,
                    QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                        QFileDevice::ExeOwner) &&
                runTestGit(historicalPullRepo.path(),
                           {QStringLiteral("config"),
                            QStringLiteral("diff.external"), externalDiff});
            check(externalDiffConfigured,
                  "strict pull external-diff marker is configured");
            if (externalDiffConfigured) {
                RepoContributionSnapshotInput externalInput;
                externalInput.workTreePath = historicalPullRepo.path();
                externalInput.branch = QStringLiteral("main");
                externalInput.head = testGitHead(historicalPullRepo.path());
                externalInput.publishingKey = identity.publicKey();
                externalInput.capturedAtMs =
                    QDateTime::currentMSecsSinceEpoch() + 60000;
                const RepoContributionSnapshot externalSnapshot =
                    buildRepoContributionSnapshot(externalInput);
                int externalCreditedPulls = 0;
                for (const QJsonValue &value :
                     externalSnapshot.payload.value("days").toArray()) {
                    externalCreditedPulls += value.toArray().at(4).toInt();
                }
                check(externalSnapshot.complete &&
                          externalSnapshot.error.isEmpty() &&
                          externalCreditedPulls == 1 &&
                          !QFileInfo::exists(externalMarker),
                      "strict pull derivation disables external diff drivers");
            }
            runTestGit(historicalPullRepo.path(),
                       {QStringLiteral("config"), QStringLiteral("--unset"),
                        QStringLiteral("diff.external")});
            QFile::remove(externalMarker);
            historicalPullSetup =
                externalDiffConfigured &&
                runTestGit(historicalPullRepo.path(),
                           {QStringLiteral("checkout"), QStringLiteral("-q"),
                            QStringLiteral("historical-feature")}) &&
                writeTestFile(historicalPullRepo.path() +
                                  QStringLiteral("/source.cpp"),
                              QByteArrayLiteral("second feature\n")) &&
                commitTestTree(historicalPullRepo.path(),
                               QStringLiteral("second feature"),
                               QStringLiteral("2026-07-14T10:00:00Z"),
                               QStringLiteral("Owner Name"),
                               QStringLiteral("owner@example.test")) &&
                runTestGit(historicalPullRepo.path(),
                           {QStringLiteral("checkout"), QStringLiteral("-q"),
                            QStringLiteral("main")});
            check(historicalPullSetup,
                  "historical branch pull head advances after signing");
            if (historicalPullSetup) {
                RepoContributionSnapshotInput input;
                input.workTreePath = historicalPullRepo.path();
                input.branch = QStringLiteral("main");
                input.head = testGitHead(historicalPullRepo.path());
                input.publishingKey = identity.publicKey();
                input.capturedAtMs =
                    QDateTime::currentMSecsSinceEpoch() + 60000;
                const RepoContributionSnapshot snapshot =
                    buildRepoContributionSnapshot(input);
                int creditedPulls = 0;
                for (const QJsonValue &value :
                     snapshot.payload.value("days").toArray()) {
                    creditedPulls += value.toArray().at(4).toInt();
                }
                check(snapshot.complete && snapshot.error.isEmpty() &&
                          creditedPulls == 1 &&
                          snapshot.payload.value("coverage")
                                  .toObject()
                                  .value("collaboration")
                                  .toString() == "complete",
                      "strict snapshot credits a branch pull after its head advances");

                QString mergeError;
                const bool creationRefRemoved =
                    runTestGit(historicalPullRepo.path(),
                               {QStringLiteral("update-ref"),
                                QStringLiteral("-d"),
                                QStringLiteral("refs/pr/1/head")});
                check(creationRefRemoved,
                      "historical pull merge resolves the advanced branch head");
                const bool merged = creationRefRemoved &&
                                    historicalPullStore.mergePull(
                                        pullNumber, &mergeError);
                check(merged && mergeError.isEmpty(),
                      "advanced historical branch pull is merged");
                if (merged) {
                    const QList<PullRequest> mergedRecords =
                        historicalPullStore.loadAll();
                    const bool distinctMergeRange =
                        mergedRecords.size() == 1 &&
                        !mergedRecords.first().creationHeadOid.isEmpty() &&
                        !mergedRecords.first().mergeHead.isEmpty() &&
                        mergedRecords.first().creationHeadOid !=
                            mergedRecords.first().mergeHead;
                    check(distinctMergeRange,
                          "merged pull preserves a distinct signed creation range");
                    input.head = testGitHead(historicalPullRepo.path());
                    const RepoContributionSnapshot mergedSnapshot =
                        buildRepoContributionSnapshot(input);
                    int mergedCreditedPulls = 0;
                    for (const QJsonValue &value :
                         mergedSnapshot.payload.value("days").toArray()) {
                        mergedCreditedPulls += value.toArray().at(4).toInt();
                    }
                    check(mergedSnapshot.complete &&
                              mergedSnapshot.error.isEmpty() &&
                              mergedCreditedPulls == 1 &&
                              mergedSnapshot.payload.value("coverage")
                                      .toObject()
                                      .value("collaboration")
                                      .toString() == "complete",
                          "strict snapshot keeps signed creation credit after merge");
                }
            }
        }

        QTemporaryDir mirrorOnlyRoot;
        const QString mirrorOnlyWork =
            mirrorOnlyRoot.path() + QStringLiteral("/work");
        const QString mirrorOnlyBare =
            mirrorOnlyRoot.path() + QStringLiteral("/mirror.git");
        bool mirrorOnlySetup =
            mirrorOnlyRoot.isValid() && QDir().mkpath(mirrorOnlyWork) &&
            runTestGit(mirrorOnlyWork,
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("served")}) &&
            runTestGit(mirrorOnlyWork,
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Mirror Owner")}) &&
            runTestGit(mirrorOnlyWork,
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("mirror@example.test")}) &&
            writeTestFile(mirrorOnlyWork + QStringLiteral("/served.cpp"),
                          "int x;\n") &&
            commitTestTree(mirrorOnlyWork, "served snapshot",
                           "2026-07-12T09:00:00Z", "Mirror Owner",
                           "mirror@example.test") &&
            runTestGit(mirrorOnlyRoot.path(),
                       {QStringLiteral("clone"), QStringLiteral("-q"),
                        QStringLiteral("--bare"), mirrorOnlyWork,
                        mirrorOnlyBare}) &&
            runTestGit(mirrorOnlyBare,
                       {QStringLiteral("config"), QStringLiteral("user.name"),
                        QStringLiteral("Mirror Owner")}) &&
            runTestGit(mirrorOnlyBare,
                       {QStringLiteral("config"), QStringLiteral("user.email"),
                        QStringLiteral("mirror@example.test")}) &&
            runTestGit(mirrorOnlyBare,
                       {QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"),
                        QStringLiteral("refs/heads/missing")});
        check(mirrorOnlySetup,
              "mirror-only dangling-HEAD snapshot repository is created");
        if (mirrorOnlySetup) {
            RepoContributionSnapshotInput input;
            input.mirrorPath = mirrorOnlyBare;
            input.branch = "served";
            input.head = testGitHead(mirrorOnlyWork);
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            int commits = 0;
            for (const QJsonValue &value :
                 snapshot.payload.value("days").toArray()) {
                commits += value.toArray().at(2).toInt();
            }
            check(snapshot.complete && commits == 1 &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("commits")
                              .toString() == "complete" &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("collaboration")
                              .toString() == "partial" &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("languages")
                              .toString() == "complete" &&
                      snapshot.payload.value("extensions").toArray() ==
                          QJsonArray{QJsonArray{"cpp", 7, 1}},
                  "mirror-only snapshot ignores dangling HEAD and keeps exact data");
        }

        // With no configured email, normalized author-name matching is the only
        // permitted commit fallback.
        QTemporaryDir nameRepo;
        bool nameSetup = nameRepo.isValid() &&
                         runTestGit(nameRepo.path(),
                                    {QStringLiteral("init"), QStringLiteral("-q"),
                                     QStringLiteral("-b"), QStringLiteral("main")}) &&
                         runTestGit(nameRepo.path(),
                                    {QStringLiteral("config"),
                                     QStringLiteral("user.name"),
                                     QStringLiteral("  Owner   Name  ")}) &&
                         writeTestFile(nameRepo.path() + "/name.cpp", "x") &&
                         commitTestTree(nameRepo.path(), "matching name",
                                        "2026-07-12T09:00:00Z", "owner name",
                                        "unconfigured@example.test") &&
                         writeTestFile(nameRepo.path() + "/name.cpp", "y") &&
                         commitTestTree(nameRepo.path(), "different name",
                                        "2026-07-13T09:00:00Z", "Someone Else",
                                        "unconfigured@example.test");
        check(nameSetup, "name-fallback snapshot repository is created");
        if (nameSetup) {
            const bool hadGlobal = qEnvironmentVariableIsSet("GIT_CONFIG_GLOBAL");
            const bool hadSystem = qEnvironmentVariableIsSet("GIT_CONFIG_SYSTEM");
            const QByteArray oldGlobal = qgetenv("GIT_CONFIG_GLOBAL");
            const QByteArray oldSystem = qgetenv("GIT_CONFIG_SYSTEM");
            qputenv("GIT_CONFIG_GLOBAL", QByteArrayLiteral("/dev/null"));
            qputenv("GIT_CONFIG_SYSTEM", QByteArrayLiteral("/dev/null"));

            RepoContributionSnapshotInput input;
            input.workTreePath = nameRepo.path();
            input.branch = "main";
            input.head = testGitHead(nameRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);

            if (hadGlobal)
                qputenv("GIT_CONFIG_GLOBAL", oldGlobal);
            else
                qunsetenv("GIT_CONFIG_GLOBAL");
            if (hadSystem)
                qputenv("GIT_CONFIG_SYSTEM", oldSystem);
            else
                qunsetenv("GIT_CONFIG_SYSTEM");

            int commits = 0;
            for (const QJsonValue &value : snapshot.payload.value("days").toArray())
                commits += value.toArray().at(2).toInt();
            check(snapshot.complete && commits == 1 &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("commits")
                              .toString() == "complete",
                  "snapshot falls back to normalized name only when email is unset");
        }

        // When neither repository nor global Git identity exists, commit rows
        // are empty and commit coverage is explicitly partial.
        QTemporaryDir noIdentityRepo;
        bool noIdentitySetup =
            noIdentityRepo.isValid() &&
            runTestGit(noIdentityRepo.path(),
                       {QStringLiteral("init"), QStringLiteral("-q"),
                        QStringLiteral("-b"), QStringLiteral("main")}) &&
            writeTestFile(noIdentityRepo.path() + "/none.cpp", "x") &&
            commitTestTree(noIdentityRepo.path(), "unconfigured identity",
                           "2026-07-12T09:00:00Z", "Transient Author",
                           "transient@example.test");
        check(noIdentitySetup, "missing-identity snapshot repository is created");
        if (noIdentitySetup) {
            const bool hadGlobal = qEnvironmentVariableIsSet("GIT_CONFIG_GLOBAL");
            const bool hadSystem = qEnvironmentVariableIsSet("GIT_CONFIG_SYSTEM");
            const QByteArray oldGlobal = qgetenv("GIT_CONFIG_GLOBAL");
            const QByteArray oldSystem = qgetenv("GIT_CONFIG_SYSTEM");
            qputenv("GIT_CONFIG_GLOBAL", QByteArrayLiteral("/dev/null"));
            qputenv("GIT_CONFIG_SYSTEM", QByteArrayLiteral("/dev/null"));

            RepoContributionSnapshotInput input;
            input.workTreePath = noIdentityRepo.path();
            input.branch = "main";
            input.head = testGitHead(noIdentityRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T12:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);

            if (hadGlobal)
                qputenv("GIT_CONFIG_GLOBAL", oldGlobal);
            else
                qunsetenv("GIT_CONFIG_GLOBAL");
            if (hadSystem)
                qputenv("GIT_CONFIG_SYSTEM", oldSystem);
            else
                qunsetenv("GIT_CONFIG_SYSTEM");

            int commits = 0;
            for (const QJsonValue &value : snapshot.payload.value("days").toArray())
                commits += value.toArray().at(2).toInt();
            check(snapshot.complete && commits == 0 &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("commits")
                              .toString() == "partial",
                  "snapshot marks commit coverage partial without a Git identity");
        }

        // A dense but bounded history must be trimmed by whole oldest dates until
        // its base64url transport fits, without making serialization unstable.
        QTemporaryDir denseRepo;
        bool denseSetup = denseRepo.isValid() &&
                          runTestGit(denseRepo.path(),
                                     {QStringLiteral("init"), QStringLiteral("-q"),
                                      QStringLiteral("-b"), QStringLiteral("main")}) &&
                          runTestGit(denseRepo.path(),
                                     {QStringLiteral("config"),
                                      QStringLiteral("user.name"),
                                      QStringLiteral("Dense Owner")}) &&
                          runTestGit(denseRepo.path(),
                                     {QStringLiteral("config"),
                                      QStringLiteral("user.email"),
                                      QStringLiteral("dense@example.test")});
        QByteArray import;
        import += "blob\nmark :1\ndata 1\nx\n";
        const QDate denseThrough(2026, 7, 13);
        int previousMark = 0;
        for (int i = 0; i < 1000; ++i) {
            const QDate day = denseThrough.addDays(i - 999);
            const qint64 seconds =
                QDateTime::fromString(day.toString(Qt::ISODate) + "T12:00:00Z",
                                      Qt::ISODate)
                    .toSecsSinceEpoch();
            const int mark = i + 2;
            const QByteArray message = "dense-" + QByteArray::number(i);
            import += "commit refs/heads/main\nmark :" + QByteArray::number(mark) +
                      "\nauthor Dense Owner <dense@example.test> " +
                      QByteArray::number(seconds) +
                      " +0000\ncommitter Dense Owner <dense@example.test> " +
                      QByteArray::number(seconds) + " +0000\ndata " +
                      QByteArray::number(message.size()) + "\n" + message + "\n";
            if (previousMark > 0)
                import += "from :" + QByteArray::number(previousMark) + "\n";
            else
                import += "M 100644 :1 dense.cpp\n";
            import += "\n";
            previousMark = mark;
        }
        import += "done\n";
        denseSetup = denseSetup &&
                     runTestGitInput(denseRepo.path(),
                                     {QStringLiteral("fast-import"),
                                      QStringLiteral("--quiet")},
                                     import);
        check(denseSetup, "dense snapshot history is imported");
        if (denseSetup) {
            RepoContributionSnapshotInput input;
            input.workTreePath = denseRepo.path();
            input.branch = "main";
            input.head = testGitHead(denseRepo.path());
            input.publishingKey = identity.publicKey();
            input.capturedAtMs = utcMs("2026-07-13T23:00:00Z");
            const RepoContributionSnapshot snapshot =
                buildRepoContributionSnapshot(input);
            const QByteArray encoded = snapshot.compactPayload.toBase64(
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
            const QJsonArray rows = snapshot.payload.value("days").toArray();
            check(snapshot.complete && encoded.size() <= 64 * 1024 &&
                      rows.size() < 1000 && !rows.isEmpty() &&
                      rows.first().toArray().at(0).toString() > "2023-10-18" &&
                      snapshot.payload.value("coverage")
                              .toObject()
                              .value("commits")
                              .toString() == "partial",
                  "snapshot trims whole oldest dates to the encoded size cap");
            check(snapshot.payload.value("coverage")
                          .toObject()
                          .value("collaboration")
                          .toString() == "complete",
                  "commit-only trimming preserves complete collaboration coverage");
            check(buildRepoContributionSnapshot(input).compactPayload ==
                      snapshot.compactPayload,
                  "trimmed snapshot bytes remain deterministic");
        }
    }

    // "dates" event (issue #384, planned start/end): content is
    // "<startMs>\0<endMs>" as plain base-10 integers. Pin it so the client and
    // the worker's issue_event_content stay byte-identical.
    IssueEvent datesVec;
    datesVec.type = "dates";
    datesVec.author = "TESTPUB";
    datesVec.ts = 4000;
    datesVec.startDate = 1700000000000LL;
    datesVec.endDate = 1702000000000LL;
    const QByteArray expectedDates =
        "forkmesh-issue-event-v1\ndates\n7\nTESTPUB\n4000\n"
        "e07365adb1daf83b068e4f38034e0eef07af8d9fdda7b763836f9af9ef091daa";
    check(IssueStore::canonicalString(7, datesVec) == expectedDates,
          "issue dates-event canonical string matches the cross-language vector");

    // --- Project event signing (issue #384) --------------------------------
    // Pin the canonical byte format so the C++ client and the worker's
    // verifier stay byte-identical. The project number is bound.
    {
        ProjectEvent openProject;
        openProject.type = "open";
        openProject.author = "TESTPUB";
        openProject.ts = 1000;
        openProject.title = "Roadmap";
        openProject.body = "Ship the mesh";
        const QByteArray expectedProjectOpen =
            "forkmesh-project-event-v1\nopen\n1\nTESTPUB\n1000\n"
            "734c32c3b5b0e94aaa58bac54fcd507746c3e64ef8273f5e537819a268f576ee";
        check(ProjectStore::canonicalString(1, openProject) == expectedProjectOpen,
              "project open canonical string matches the cross-language vector");

        ProjectEvent projectDates;
        projectDates.type = "dates";
        projectDates.author = "TESTPUB";
        projectDates.ts = 2000;
        projectDates.startDate = 1700000000000LL;
        projectDates.endDate = 1702000000000LL;
        const QByteArray expectedProjectDates =
            "forkmesh-project-event-v1\ndates\n1\nTESTPUB\n2000\n"
            "e07365adb1daf83b068e4f38034e0eef07af8d9fdda7b763836f9af9ef091daa";
        check(ProjectStore::canonicalString(1, projectDates) ==
                  expectedProjectDates,
              "project dates canonical string matches the cross-language vector");

        // Linked issues sign as a comma-joined ascending list ("384,385") no
        // matter the stored order, so both sides hash identical bytes.
        ProjectEvent projectIssues;
        projectIssues.type = "issues";
        projectIssues.author = "TESTPUB";
        projectIssues.ts = 3000;
        projectIssues.issues = {385, 384};
        const QByteArray expectedProjectIssues =
            "forkmesh-project-event-v1\nissues\n1\nTESTPUB\n3000\n"
            "452ebda460e1290f69693757d3971639ab5f8164af5014014061b1704b47e157";
        check(ProjectStore::canonicalString(1, projectIssues) ==
                  expectedProjectIssues,
              "project issues canonical string sorts ascending and matches the "
              "cross-language vector");

        // Sign a real project event with the node identity and verify it.
        ProjectStore projectStore(QString(), QString(), &identity, "tester");
        ProjectEvent signedProject =
            projectStore.makeSignedEvent(1, openProject);
        check(verifyEd25519(signedProject.author, signedProject.sig,
                            ProjectStore::canonicalString(1, signedProject)),
              "project-event signature verifies against the public key");
        ProjectEvent tamperedProject = signedProject;
        tamperedProject.body = "Ship the mesh!";
        check(!verifyEd25519(tamperedProject.author, tamperedProject.sig,
                             ProjectStore::canonicalString(1, tamperedProject)),
              "tampered project-event signature is rejected");
    }

    // Functional round-trip (issue #384): create a project in a real temp git
    // repo, mutate its fields, and reload it — plus the issue "dates" event.
    {
        QTemporaryDir td;
        check(td.isValid(), "project store temp repo dir is valid");
        const QString dir = td.path();
        const auto git = [&](const QStringList &args) {
            QProcess p;
            p.start(QStringLiteral("git"),
                    QStringList{QStringLiteral("-C"), dir} + args);
            p.waitForFinished(10000);
        };
        git({QStringLiteral("init")});
        git({QStringLiteral("config"), QStringLiteral("user.email"),
             QStringLiteral("t@t")});
        git({QStringLiteral("config"), QStringLiteral("user.name"),
             QStringLiteral("T")});

        ProjectStore projects(dir, QString(), &identity, "tester");
        check(projects.canWrite(), "project store can write to the temp repo");
        QString err;
        const int number =
            projects.createProject("Roadmap", "Ship the mesh", 1700000000000LL,
                                   1702000000000LL, "v1.0", {385, 384}, &err);
        check(number == 1, "first project takes number 1");
        QList<Project> loaded = projects.loadAll(&err);
        check(loaded.size() == 1 && loaded.first().title == "Roadmap" &&
                  loaded.first().body == "Ship the mesh" &&
                  loaded.first().status == "open" &&
                  loaded.first().startDate == 1700000000000LL &&
                  loaded.first().endDate == 1702000000000LL &&
                  loaded.first().milestone == "v1.0" &&
                  loaded.first().issues == (QList<int>{384, 385}),
              "a created project round-trips through disk with sorted issues");
        check(projects.setStatus(number, "closed", &err) &&
                  projects.setIssues(number, {7}, &err) &&
                  projects.setDates(number, 0, 1702000000000LL, &err),
              "project status/issues/dates mutations succeed");
        loaded = projects.loadAll(&err);
        check(loaded.size() == 1 && loaded.first().status == "closed" &&
                  loaded.first().issues == (QList<int>{7}) &&
                  loaded.first().startDate == 0 &&
                  loaded.first().endDate == 1702000000000LL,
              "later project events fold over the earlier metadata");
        check(projects.tombstoneProject(number, &err),
              "tombstoning a project succeeds");
        check(projects.loadAll().isEmpty(),
              "a tombstoned project disappears from loadAll");

        IssueStore issues(dir, QString(), &identity, "tester");
        const int issueNumber =
            issues.createIssue("Test issue", "body", {}, QString(), 0, {}, {},
                               &err);
        check(issueNumber == 1, "first issue takes number 1");
        check(issues.setDates(issueNumber, 1700000000000LL, 0, &err),
              "issue setDates appends a signed dates event");
        const QList<Issue> reloaded = issues.loadAll();
        check(reloaded.size() == 1 &&
                  reloaded.first().startDate == 1700000000000LL &&
                  reloaded.first().endDate == 0,
              "issue dates fold into the reloaded metadata");
    }

    // --- PullAiReview: prompt, findings JSON, quick-fix suggestions ------
    {
        const QString prompt = buildAiReviewPrompt(
            "Add feature", "Does things.",
            "diff --git a/f.c b/f.c\n--- a/f.c\n+++ b/f.c\n@@ -1 +1 @@\n+line\n");
        check(prompt.contains("Add feature") && prompt.contains("Does things.") &&
                  prompt.contains("+line"),
              "AI review prompt carries the title, description and diff");
        check(prompt.contains("JSON"), "AI review prompt demands JSON output");
        QString longDiff;
        for (int i = 0; i < 200; ++i)
            longDiff += QStringLiteral("+line number %1 padding\n").arg(i);
        const QString capped = buildAiReviewPrompt("T", QString(), longDiff, 500);
        check(capped.contains("truncated") && !capped.contains("line number 199"),
              "an oversized diff is capped with a truncation note");

        bool ok = false;
        QList<AiReviewFinding> findings = parseAiReviewFindings(
            QStringLiteral(
                "[{\"path\": \"src/a.cpp\", \"line_start\": 3, \"line_end\": 4,"
                "  \"severity\": \"bug\", \"comment\": \"Off by one\","
                "  \"original\": \"int i = 0;\\nint j = 1;\","
                "  \"fix\": \"int i = 1;\\nint j = 2;\"},"
                " {\"path\": \"src/b.cpp\", \"line\": 9, \"severity\": \"odd\","
                "  \"comment\": \"No fix here\", \"original\": null,"
                "  \"fix\": null},"
                " {\"path\": \"\", \"line_start\": 1, \"comment\": \"dropped\"}]"),
            &ok);
        check(ok && findings.size() == 2,
              "findings parse from a bare JSON array and invalid entries drop");
        check(!findings.isEmpty() && findings.at(0).path == "src/a.cpp" &&
                  findings.at(0).lineStart == 3 && findings.at(0).lineEnd == 4 &&
                  findings.at(0).severity == "bug",
              "a finding carries its anchor and severity");
        check(!findings.isEmpty() &&
                  findings.at(0).suggestionPatch.contains("-int i = 0;") &&
                  findings.at(0).suggestionPatch.contains("+int j = 2;"),
              "an original+fix pair becomes a committable suggestion patch");
        check(findings.size() == 2 && findings.at(1).severity == "warning" &&
                  findings.at(1).suggestionPatch.isEmpty() &&
                  findings.at(1).lineStart == 9 && findings.at(1).lineEnd == 9,
              "a fix-less finding normalizes its severity and line range");

        findings = parseAiReviewFindings(
            QStringLiteral("Sure!\n```json\n[{\"path\": \"x\", \"line_start\": 1,"
                           " \"comment\": \"c\"}]\n```"),
            &ok);
        check(ok && findings.size() == 1,
              "findings parse out of surrounding chatter and fences");
        findings = parseAiReviewFindings(
            QStringLiteral("{\"findings\": [{\"path\": \"x\", \"line_start\": 2,"
                           " \"comment\": \"c\"}]}"),
            &ok);
        check(ok && findings.size() == 1,
              "findings parse from a {\"findings\": [...]} wrapper");
        check(parseAiReviewFindings(QStringLiteral("[]"), &ok).isEmpty() && ok,
              "an empty array parses as a clean review");
        check(parseAiReviewFindings(QStringLiteral("no json here"), &ok).isEmpty() &&
                  !ok,
              "an unparseable reply is distinguished from a clean review");

        const QString patch =
            buildSuggestionPatch({"foo(1);", "bar(2);"}, {"foo(2);"}, 5);
        check(patch.startsWith("@@ -5,2 +5,1 @@"),
              "suggestion patch header carries the line range");
        QStringList orig, repl;
        check(parseSuggestionPatch(patch, &orig, &repl) && orig.size() == 2 &&
                  repl.size() == 1 && orig.first() == "foo(1);" &&
                  repl.first() == "foo(2);",
              "suggestion patch round-trips original and replacement lines");

        QString exact = "1\n2\n3\n4\nfoo(1);\nbar(2);\n7\n";
        QString applyErr;
        check(applySuggestionToContent(&exact, 5, patch, &applyErr) &&
                  exact == "1\n2\n3\n4\nfoo(2);\n7\n",
              "a suggestion applies at its recorded line");
        QString drifted = "a\nfoo(1);\nbar(2);\nz\n";
        check(applySuggestionToContent(&drifted, 5, patch, &applyErr) &&
                  drifted == "a\nfoo(2);\nz\n",
              "a drifted suggestion relocates to the unique matching block");
        QString ambiguous = "foo(1);\nbar(2);\nfoo(1);\nbar(2);\n";
        check(!applySuggestionToContent(&ambiguous, 9, patch, &applyErr),
              "an ambiguous suggestion anchor is refused");
        QString missing = "nothing here\n";
        check(!applySuggestionToContent(&missing, 1, patch, &applyErr),
              "a suggestion whose lines are gone is refused");
        const QString delPatch = buildSuggestionPatch({"kill me"}, {}, 2);
        QString delContent = "keep\nkill me\nkeep2\n";
        check(applySuggestionToContent(&delContent, 2, delPatch, &applyErr) &&
                  delContent == "keep\nkeep2\n",
              "an empty replacement deletes the anchored lines");
        check(buildSuggestionPatch({}, {"new"}, 1).isEmpty(),
              "a pure insertion is not representable as a suggestion");
    }

    // --- Full IssueStore round-trip in a throwaway git repo --------------
    QTemporaryDir tmp;
    if (tmp.isValid()) {
        auto git = [&](const QStringList &args) {
            QProcess p;
            p.start("git", QStringList{"-C", tmp.path()} + args);
            p.waitForFinished(8000);
        };
        auto gitOutput = [&](const QStringList &args) {
            QProcess p;
            p.start("git", QStringList{"-C", tmp.path()} + args);
            p.waitForFinished(8000);
            return p.readAllStandardOutput();
        };
        git({"init", "-q"});
        git({"config", "user.email", "test@forkmesh.local"});
        git({"config", "user.name", "tester"});

        IssueStore repo(tmp.path(), QString(), &identity, "tester");
        check(repo.canWrite(), "store reports the temp work tree as writable");

        QString err;
        const int n = repo.createIssue("Round trip", "Hello **body**",
                                       {"bug"}, "v1", 7, {}, {}, &err);
        check(n == 1, "createIssue returns the first issue number");
        const QString issueJsonPath =
            QDir(tmp.path()).filePath(
                QStringLiteral(".forkmesh/issues/open/1/issue-1.json"));
        check(QFileInfo::exists(issueJsonPath),
              "createIssue writes .forkmesh/issues/open/1/issue-1.json");
        check(!QFileInfo::exists(
                  QDir(tmp.path()).filePath(QStringLiteral("issues/1/issue.md"))),
              "createIssue does not write legacy issue.md");
        QList<Issue> loaded = repo.loadAll();
        check(loaded.size() == 1 && loaded.first().title == "Round trip" &&
                  loaded.first().labels.contains("bug") &&
                  loaded.first().milestone == "v1" &&
                  loaded.first().priority == 7,
              "issue loads back with title, label, milestone and priority");
        check(!loaded.isEmpty() && loaded.first().events.first().body == "Hello **body**",
              "open-event body round-trips from issue JSON");
        const QString legacyIssueDir =
            QDir(tmp.path()).filePath(QStringLiteral("issues/99"));
        QDir().mkpath(legacyIssueDir);
        QFile legacyIssue(QDir(legacyIssueDir).filePath(QStringLiteral("issue.md")));
        if (legacyIssue.open(QIODevice::WriteOnly | QIODevice::Truncate))
            legacyIssue.write("---\ntitle: Old format\nstatus: open\n---\n\nignored\n");
        legacyIssue.close();
        loaded = repo.loadAll();
        check(loaded.size() == 1 && loaded.first().number == n,
              "legacy issues/<n>/issue.md folders are ignored");
        const int second = repo.createIssue("Second after legacy", "body", {},
                                            QString(), 0, {}, {}, &err);
        check(second == 2, "legacy issue folders do not affect new issue numbers");

        // A pre-split .forkmesh/issues/<n>/ folder is migrated into the
        // status-named subfolder (and committed) the next time issues load on
        // the owning node.
        const QString preSplitDir =
            QDir(tmp.path()).filePath(QStringLiteral(".forkmesh/issues/77"));
        QDir().mkpath(preSplitDir);
        QFile preSplitJson(
            QDir(preSplitDir).filePath(QStringLiteral("issue-77.json")));
        if (preSplitJson.open(QIODevice::WriteOnly | QIODevice::Truncate))
            preSplitJson.write(
                "{\"schema\":\"forkmesh-issue-v1\",\"number\":77,"
                "\"title\":\"Legacy layout\",\"status\":\"open\","
                "\"events\":[{\"type\":\"open\",\"id\":\"open-77\","
                "\"author\":\"a\",\"ts\":1,\"title\":\"Legacy layout\","
                "\"body\":\"\",\"attachments\":[],\"sig\":\"s\"},"
                "{\"type\":\"status\",\"id\":\"s1\",\"author\":\"a\","
                "\"ts\":2,\"status\":\"closed\",\"sig\":\"s\"}]}");
        preSplitJson.close();
        loaded = repo.loadAll();
        check(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral(
                  ".forkmesh/issues/closed/77/issue-77.json"))) &&
                  !QDir(preSplitDir).exists(),
              "loadAll migrates a pre-split issue folder into closed/");
        check(std::any_of(loaded.begin(), loaded.end(),
                          [](const Issue &i) {
                              return i.number == 77 &&
                                     i.status == QStringLiteral("closed");
                          }),
              "migrated legacy issue still loads with its folded status");

        // Folder-authoritative status (adhoc #16): an issue still sitting in
        // open/ whose record carries a stale status->closed event must load as
        // OPEN, so the Issues-tab tally matches the open/ folder listing and the
        // served/advertised open count instead of drifting below it (the
        // 11-vs-7 skew the file browser exposes).
        const QString skewDir =
            QDir(tmp.path()).filePath(QStringLiteral(".forkmesh/issues/open/55"));
        QDir().mkpath(skewDir);
        QFile skewJson(QDir(skewDir).filePath(QStringLiteral("issue-55.json")));
        if (skewJson.open(QIODevice::WriteOnly | QIODevice::Truncate))
            skewJson.write(
                "{\"schema\":\"forkmesh-issue-v1\",\"number\":55,"
                "\"title\":\"Stale status\",\"status\":\"closed\","
                "\"events\":[{\"type\":\"open\",\"id\":\"open-55\","
                "\"author\":\"a\",\"ts\":1,\"title\":\"Stale status\","
                "\"body\":\"\",\"attachments\":[],\"sig\":\"s\"},"
                "{\"type\":\"status\",\"id\":\"s55\",\"author\":\"a\","
                "\"ts\":2,\"status\":\"closed\",\"sig\":\"s\"}]}");
        skewJson.close();
        loaded = repo.loadAll();
        check(std::any_of(loaded.begin(), loaded.end(),
                          [](const Issue &i) {
                              return i.number == 55 &&
                                     i.status == QStringLiteral("open");
                          }),
              "an issue in open/ with a stale status->closed record counts as open");

        // Deletion is shown, not hidden (adhoc #16). A delete/self event only
        // deletes the issue when its own creator signed it; a delete from anyone
        // else is an unauthorized attempt that leaves the issue open and counted
        // but flagged. Every issue is returned by loadAll either way — the UI
        // badges it — and the record is never rewritten.
        auto writeIssueWithDelete = [&](int number, const QString &creator,
                                        const QString &deleter) {
            const QString dir = QDir(tmp.path()).filePath(
                QStringLiteral(".forkmesh/issues/open/%1").arg(number));
            QDir().mkpath(dir);
            QFile f(QDir(dir).filePath(
                QStringLiteral("issue-%1.json").arg(number)));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(QStringLiteral(
                            "{\"schema\":\"forkmesh-issue-v1\",\"number\":%1,"
                            "\"title\":\"t%1\",\"status\":\"open\","
                            "\"events\":[{\"type\":\"open\",\"id\":\"open-%1\","
                            "\"author\":\"%2\",\"ts\":1,\"title\":\"t%1\","
                            "\"body\":\"\",\"attachments\":[],\"sig\":\"s\"},"
                            "{\"type\":\"delete\",\"id\":\"d%1\",\"author\":\"%3\","
                            "\"ts\":2,\"target\":\"self\",\"sig\":\"s\"}]}")
                            .arg(QString::number(number), creator, deleter)
                            .toUtf8());
        };
        writeIssueWithDelete(60, "creatorA", "stranger");  // unauthorized attempt
        writeIssueWithDelete(61, "creatorB", "creatorB");  // creator self-delete
        loaded = repo.loadAll();
        const auto find60 = std::find_if(loaded.begin(), loaded.end(),
            [](const Issue &i) { return i.number == 60; });
        const auto find61 = std::find_if(loaded.begin(), loaded.end(),
            [](const Issue &i) { return i.number == 61; });
        check(find60 != loaded.end() && !find60->isDeleted() &&
                  find60->hasUnauthorizedDeleteAttempt(),
              "a non-author's delete is shown, flagged, and does not delete the issue");
        check(find61 != loaded.end() && find61->isDeleted() &&
                  !find61->hasUnauthorizedDeleteAttempt(),
              "a creator's own self-delete marks the issue deleted but still shows it");
        {
            // The record is left intact — the delete event is not stripped.
            QFile keptFile(QDir(tmp.path()).filePath(QStringLiteral(
                ".forkmesh/issues/open/60/issue-60.json")));
            bool hasDelete = false;
            if (keptFile.open(QIODevice::ReadOnly)) {
                const QJsonArray evs =
                    QJsonDocument::fromJson(keptFile.readAll())
                        .object().value("events").toArray();
                hasDelete = std::any_of(
                    evs.begin(), evs.end(), [](const QJsonValue &v) {
                        return v.toObject().value("type").toString() == "delete";
                    });
            }
            check(hasDelete,
                  "the unauthorized delete event is preserved, not stripped");
        }

        check(repo.addComment(n, "a comment", {}, &err), "addComment succeeds");
        QFile issueJson(issueJsonPath);
        const bool commentJsonOk =
            issueJson.open(QIODevice::ReadOnly) &&
            QJsonDocument::fromJson(issueJson.readAll())
                    .object()
                    .value("events")
                    .toArray()
                    .at(1)
                    .toObject()
                    .value("body")
                    .toString() == "a comment";
        issueJson.close();
        check(commentJsonOk, "comment body is stored inside the issue JSON");
        check(QDir(QDir(tmp.path())
                       .filePath(QStringLiteral(".forkmesh/issues/open/1")))
                  .entryList(QStringList{QStringLiteral("*.md")}, QDir::Files)
                  .isEmpty(),
              "issue folder contains no markdown event files");
        check(repo.setStatus(n, "closed", &err), "setStatus succeeds");
        check(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral(
                  ".forkmesh/issues/closed/1/issue-1.json"))) &&
                  !QDir(QDir(tmp.path())
                            .filePath(QStringLiteral(".forkmesh/issues/open/1")))
                       .exists(),
              "closing an issue moves its folder from open/ to closed/");
        check(repo.setPriority(n, 3, &err), "setPriority succeeds");
        check(repo.assignAgent(n, "codex", 42, true, "queued", &err),
              "assignAgent succeeds");
        check(repo.assignAgent(n, QString(), 0, false, "cleared", &err),
              "assignAgent clears the issue agent");
        loaded = repo.loadAll();
        check(!loaded.isEmpty() && loaded.first().priority == 3,
              "priority event folds into issue metadata");
        bool sawComment = false;
        bool sawAgentAssign = false;
        bool sawAgentClear = false;
        for (const IssueEvent &e : loaded.first().events)
            if (e.type == "comment" && e.body == "a comment")
                sawComment = true;
            else if (e.type == "agent" && e.agentProvider == "codex" &&
                     e.agentSessionId == 42 && e.agentCreatePr &&
                     e.agentStatus == "queued")
                sawAgentAssign = true;
            else if (e.type == "agent" && e.agentProvider.isEmpty() &&
                     e.agentSessionId == 0 && !e.agentCreatePr &&
                     e.agentStatus == "cleared")
                sawAgentClear = true;
        check(sawComment, "comment body round-trips from issue JSON");
        check(sawAgentAssign, "agent assignment event round-trips");
        check(sawAgentClear, "agent clear event round-trips");
        check(loaded.first().status == "closed", "status reflects close event");

        // Deleting a comment appends a signed "delete" event targeting the
        // comment's id; the comment event itself is left in place (folding it
        // out of the UI is a render-time concern) so history is preserved.
        const QString commentId = loaded.first().events.at(1).id;
        check(repo.deleteEvent(n, commentId, &err), "deleteEvent succeeds");
        const QList<Issue> afterCommentDelete = repo.loadAll();
        const Issue &withDeletedComment = afterCommentDelete.first();
        check(withDeletedComment.number == n,
              "issue with a deleted comment still loads");
        bool sawDeleteEvent = false;
        for (const IssueEvent &e : withDeletedComment.events)
            if (e.type == "delete" && e.target == commentId)
                sawDeleteEvent = true;
        check(sawDeleteEvent,
              "delete event targeting the comment id round-trips from issue JSON");
        check(!withDeletedComment.isDeleted(),
              "deleting a single comment does not tombstone the whole issue");

        // Fast "regular" delete: a creator's self-tombstone marks the issue
        // deleted but loadAll still returns it (adhoc #16 — shown with a Deleted
        // badge, not hidden), and its history stays intact in git.
        const int tomb = repo.createIssue("Tombstone me", "body", {}, QString(),
                                          0, {}, {}, &err);
        check(repo.tombstoneIssue(tomb, &err), "tombstoneIssue succeeds");
        const QList<Issue> afterTombstone = repo.loadAll();
        const auto tombIt =
            std::find_if(afterTombstone.begin(), afterTombstone.end(),
                         [&](const Issue &i) { return i.number == tomb; });
        check(tombIt != afterTombstone.end() && tombIt->isDeleted(),
              "a self-tombstoned issue still loads, flagged as deleted");
        check(!gitOutput({"log", "--all", "--",
                          QStringLiteral(".forkmesh/issues/open/%1").arg(tomb)})
                   .trimmed()
                   .isEmpty(),
              "tombstoned issue is preserved in git history");

        check(repo.deleteIssue(n, &err), "deleteIssue succeeds");
        const QList<Issue> afterDelete = repo.loadAll();
        check(std::none_of(afterDelete.begin(), afterDelete.end(),
                           [&](const Issue &i) { return i.number == n; }),
              "deleted issue no longer loads");
        // The issue lived at open/<n> and then closed/<n>; the purge must strip
        // every location (including the pre-split legacy path) from history.
        check(gitOutput({"log", "--all", "--",
                         QStringLiteral(".forkmesh/issues/%1").arg(n),
                         QStringLiteral(".forkmesh/issues/open/%1").arg(n),
                         QStringLiteral(".forkmesh/issues/closed/%1").arg(n)})
                  .trimmed()
                  .isEmpty(),
              "deleted issue is purged from git history");

        // A mirror node files an issue: it arrives as a signed "open" event with
        // a proposed number that collides with an existing issue. applyRemoteEvent
        // must reassign a fresh number, keep the submitter as author, and apply
        // the vouched issue-level metadata that rode along with the submission.
        const int base = repo.createIssue("Base", "b", {}, QString(), 0, {}, {}, &err);
        IssueEvent remoteOpen;
        remoteOpen.type = "open";
        remoteOpen.id = QStringLiteral("open-%1").arg(base); // collides with base
        remoteOpen.title = "From a mirror";
        remoteOpen.body = "filed remotely";
        remoteOpen = repo.makeSignedEvent(base, remoteOpen);
        remoteOpen.authorName = "mirrornode";
        RemoteIssueMeta meta;
        meta.labels = QStringList{"enhancement"};
        meta.priority = 9;
        check(repo.applyRemoteEvent(base, remoteOpen, "From a mirror", &err, meta),
              "applyRemoteEvent accepts a mirror-authored open event");
        const QList<Issue> afterRemote = repo.loadAll();
        const Issue *mirrored = nullptr;
        for (const Issue &i : afterRemote)
            if (i.title == "From a mirror")
                mirrored = &i;
        check(mirrored && mirrored->number != base,
              "colliding remote issue is reassigned a fresh number");
        check(mirrored && mirrored->authorName == "mirrornode",
              "remote issue preserves the submitting node as author");
        check(mirrored && mirrored->labels.contains("enhancement") &&
                  mirrored->priority == 9,
              "remote issue applies vouched metadata (labels + priority)");

        // Re-syncing the same submission (same signature) must be idempotent:
        // the inbox can redeliver before the owner acknowledges it.
        const int before = repo.loadAll().size();
        check(repo.applyRemoteEvent(base, remoteOpen, "From a mirror", &err, meta),
              "re-applying the same remote open event succeeds");
        check(repo.loadAll().size() == before,
              "re-syncing a merged issue does not duplicate it");

        // Settle any untracked state the IssueStore section above left behind
        // (e.g. its issues/ folder) so the PullStore tests below start from a
        // clean tree — beginPullBranch (agent edit / file edit / conflict
        // resolve) requires one, and pulls/ commits no longer land in this
        // working tree at all (issue #399) to incidentally sweep it up.
        git({"add", "-A"});
        git({"commit", "-q", "-m", "test: settle pre-pull-tests state",
            "--allow-empty"});

        // --- PullStore conversation round-trip ---------------------------
        PullStore pulls(tmp.path(), QString(), &identity, "tester");
        const int pn = pulls.createPull(
            "A change", "Body", "main", "feature",
            "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n---flag\n+hi\n",
            QString(), /*branchBacked=*/false, &err);
        check(pn == 1, "createPull returns the first PR number");
        check(pulls.addComment(pn, "first comment", &err), "PR addComment succeeds");
        check(pulls.addReview(pn, "approved", "LGTM", &err), "PR addReview succeeds");
        check(pulls.addLineComment(pn, "x", "new", 1, "inline note", &err),
              "PR addLineComment succeeds");
        check(pulls.addThreadComment(pn, "x", "new", 1, 2, "thread note",
                                     "@@ -0,0 +1 @@\n+suggested\n", &err),
              "PR addThreadComment succeeds");
        check(pulls.addThreadReply(pn, "thread-1", "event-1", "reply note", &err),
              "PR addThreadReply succeeds");
        check(pulls.setThreadState(pn, "thread-1", "resolved", "fixed", &err),
              "PR setThreadState succeeds");
        check(pulls.setSuggestionState(pn, "thread-1", "applied", "abc123",
                                       "applied", &err),
              "PR setSuggestionState succeeds");
        QList<PullRequest> loadedPulls = pulls.loadAll();
        check(!loadedPulls.isEmpty() && loadedPulls.first().events.size() == 7,
              "PR conversation events round-trip from NNNN-*.md");
        bool sawLine = false, sawThread = false, sawReply = false;
        bool sawResolved = false, sawSuggestionApplied = false;
        if (!loadedPulls.isEmpty()) {
            for (const PullEvent &e : loadedPulls.first().events) {
                if (e.type == "line-comment" && e.path == "x" && e.side == "new" &&
                    e.line == 1 && e.body == "inline note") {
                    sawLine = true;
                } else if (e.type == "thread-comment" && e.path == "x" &&
                           e.side == "new" && e.lineStart == 1 && e.lineEnd == 2 &&
                           e.body == "thread note" &&
                           e.suggestionPatch.contains("+suggested")) {
                    sawThread = true;
                } else if (e.type == "thread-reply" && e.threadId == "thread-1" &&
                           e.parentId == "event-1" && e.body == "reply note") {
                    sawReply = true;
                } else if (e.type == "thread-state" && e.threadId == "thread-1" &&
                           e.state == "resolved" && e.body == "fixed") {
                    sawResolved = true;
                } else if (e.type == "suggestion-state" &&
                           e.threadId == "thread-1" && e.state == "applied" &&
                           e.appliedCommit == "abc123") {
                    sawSuggestionApplied = true;
                }
            }
        }
        check(sawLine, "line-comment round-trips with path/side/line");
        check(sawThread, "thread-comment round-trips with anchor and suggestion");
        check(sawReply, "thread-reply round-trips with thread and parent");
        check(sawResolved, "thread-state round-trips with resolved state");
        check(sawSuggestionApplied,
              "suggestion-state round-trips with applied commit");
        check(!loadedPulls.isEmpty() &&
                  loadedPulls.first().reviewSummary() == "approved",
              "PR review summary folds to approved");
        check(!loadedPulls.isEmpty() && loadedPulls.first().additions == 1 &&
                  loadedPulls.first().deletions == 1,
              "PR stats count removed content beginning with two hyphens");

        // A browser-created pull carries portable, signed change bytes rather
        // than only mutable branch names. Exercise the exact Worker -> Qt wire
        // shape (including legacy `body`) and prove the drained record merges.
        const QString webBase = QString::fromUtf8(
            gitOutput({"rev-parse", "--abbrev-ref", "HEAD"})).trimmed();
        const QString webBaseOid = QString::fromUtf8(
            gitOutput({"rev-parse", "HEAD"})).trimmed();
        git({"checkout", "-q", "-b", "web-wire-feature"});
        check(writeTestFile(tmp.path() + "/web-wire.txt",
                            QByteArrayLiteral("portable browser change\n")),
              "write browser pull fixture");
        git({"add", "web-wire.txt"});
        git({"commit", "-q", "-m", "portable browser commit"});
        const QString webHeadOid = QString::fromUtf8(
            gitOutput({"rev-parse", "HEAD"})).trimmed();
        const QString webPatch = QString::fromUtf8(
            gitOutput({"diff", "--binary",
                       webBase + "...web-wire-feature"}));
        const QString webCommits = QString::fromUtf8(
            gitOutput({"format-patch", "--binary", "--no-signature", "--stdout",
                       webBase + "..web-wire-feature"}));
        git({"checkout", "-q", webBase});

        PullRequest browserPull;
        browserPull.title = "Portable browser pull";
        browserPull.description = "Browser description";
        browserPull.base = webBase;
        browserPull.head = "web-wire-feature";
        browserPull.patch = webPatch;
        browserPull.commits = webCommits;
        browserPull = pulls.makeSignedPull(browserPull);
        QJsonObject browserWire = browserPull.toJson();
        browserWire.remove("description");
        browserWire.insert("body", "Browser description");
        const PullRequest drainedBrowserPull =
            PullRequest::fromJson(browserWire);
        check(drainedBrowserPull.description == "Browser description",
              "PullRequest wire reader preserves legacy browser body text");
        check(drainedBrowserPull.patch == webPatch &&
                  drainedBrowserPull.commits == webCommits &&
                  !drainedBrowserPull.patch.isEmpty() &&
                  !drainedBrowserPull.commits.isEmpty(),
              "PullRequest wire reader retains the signed portable change set");
        check(verifyEd25519(drainedBrowserPull.author,
                            drainedBrowserPull.sig,
                            PullStore::canonicalString(drainedBrowserPull)),
              "drained browser change bytes retain their valid signature");
        check(pulls.applyRemotePull(drainedBrowserPull, &err),
              "owner drain stores a portable browser pull");
        int browserPullNumber = 0;
        for (const PullRequest &candidate : pulls.loadAll()) {
            if (candidate.title == browserPull.title) {
                browserPullNumber = candidate.number;
                check(candidate.description == "Browser description" &&
                          candidate.patch == webPatch &&
                          candidate.commits == webCommits,
                      "stored browser pull keeps description, patch, and commits");
                break;
            }
        }
        check(browserPullNumber > 0,
              "owner drain assigns the browser pull a local number");
        check(browserPullNumber > 0 &&
                  pulls.mergePull(browserPullNumber, &err),
              "portable browser pull merges after inbox drain");
        QFile mergedWebFile(tmp.path() + "/web-wire.txt");
        check(mergedWebFile.open(QIODevice::ReadOnly) &&
                  mergedWebFile.readAll() ==
                      QByteArrayLiteral("portable browser change\n"),
              "merged browser pull lands its signed repository change");
        check(webBaseOid != webHeadOid,
              "browser pull fixture resolves distinct immutable commits");

        // A remote node files a review event via the inbox; it must append+commit.
        PullEvent remoteReview;
        remoteReview.type = "review";
        remoteReview.state = "changes_requested";
        remoteReview.body = "please fix";
        remoteReview = pulls.makeSignedEvent(pn, remoteReview);
        remoteReview.authorName = "reviewer-node";
        check(pulls.applyRemoteEvent(pn, remoteReview, &err),
              "applyRemoteEvent accepts a mirror-authored review");
        loadedPulls = pulls.loadAll();
        check(!loadedPulls.isEmpty() &&
                  loadedPulls.first().reviewSummary() == "changes_requested",
              "a later changes-requested review supersedes approval");

        // --- DiscussionStore round-trip ---------------------------------
        DiscussionStore discussions(tmp.path(), QString(), &identity, "tester");
        const int dn = discussions.createDiscussion(
            "Welcome", "Hello **discussion**", "Announcements", &err);
        check(dn == 1, "createDiscussion returns the first discussion number");
        check(discussions.addComment(dn, "first reply", &err),
              "discussion addComment succeeds");
        QList<Discussion> loadedDiscussions = discussions.loadAll();
        check(!loadedDiscussions.isEmpty() &&
                  loadedDiscussions.first().title == "Welcome" &&
                  loadedDiscussions.first().category == "Announcements" &&
                  loadedDiscussions.first().events.size() == 2,
              "discussion loads back with title, category and comments");
        DiscussionEvent remoteComment;
        remoteComment.type = "comment";
        remoteComment.body = "remote reply";
        remoteComment = discussions.makeSignedEvent(dn, remoteComment);
        check(discussions.applyRemoteEvent(dn, remoteComment, "Remote welcome", &err),
              "discussion remote comment applies");
        check(discussions.applyRemoteEvent(dn, remoteComment, "Remote welcome", &err),
              "discussion remote comment reapply is idempotent");

        PullRequest reviewPr;
        reviewPr.number = 99;
        PullEvent approvingReview;
        approvingReview.type = "review";
        approvingReview.author = "reviewer-a";
        approvingReview.state = "approved";
        approvingReview.ts = 100;
        PullEvent blockingReview;
        blockingReview.type = "review";
        blockingReview.author = "reviewer-b";
        blockingReview.state = "changes_requested";
        blockingReview.ts = 110;
        PullEvent openThread;
        openThread.type = "thread-comment";
        openThread.id = "event-open";
        openThread.threadId = "thread-open";
        openThread.path = "src/a.cpp";
        openThread.side = "new";
        openThread.lineStart = 10;
        openThread.lineEnd = 12;
        openThread.body = "please guard this";
        openThread.suggestionPatch = "@@ -10 +10 @@\n-old\n+new\n";
        openThread.ts = 120;
        PullEvent openReply;
        openReply.type = "thread-reply";
        openReply.threadId = "thread-open";
        openReply.parentId = "event-open";
        openReply.body = "reply";
        openReply.ts = 130;
        PullEvent appliedSuggestion;
        appliedSuggestion.type = "suggestion-state";
        appliedSuggestion.threadId = "thread-open";
        appliedSuggestion.state = "applied";
        appliedSuggestion.appliedCommit = "abc123";
        appliedSuggestion.ts = 140;
        PullEvent resolvedThread;
        resolvedThread.type = "thread-comment";
        resolvedThread.id = "event-resolved";
        resolvedThread.threadId = "thread-resolved";
        resolvedThread.path = "src/a.cpp";
        resolvedThread.side = "old";
        resolvedThread.lineStart = 20;
        resolvedThread.lineEnd = 20;
        resolvedThread.body = "remove this";
        resolvedThread.ts = 150;
        PullEvent resolvedState;
        resolvedState.type = "thread-state";
        resolvedState.threadId = "thread-resolved";
        resolvedState.state = "resolved";
        resolvedState.ts = 160;
        PullEvent legacyLine;
        legacyLine.type = "line-comment";
        legacyLine.path = "src/b.cpp";
        legacyLine.side = "new";
        legacyLine.line = 5;
        legacyLine.body = "legacy inline note";
        legacyLine.ts = 170;
        reviewPr.events = {approvingReview, blockingReview, openThread,
                           openReply, appliedSuggestion, resolvedThread,
                           resolvedState, legacyLine};
        const PullReviewSnapshot snapshot = buildPullReviewSnapshot(reviewPr);
        check(snapshot.reviewSummary == "changes_requested",
              "review model folds latest blocking review state");
        check(snapshot.totalThreads == 3 && snapshot.unresolvedThreads == 2 &&
                  snapshot.resolvedThreads == 1,
              "review model counts resolved and unresolved threads");
        check(snapshot.suggestions == 1,
              "review model counts suggested changes");
        check(snapshot.files.value("src/a.cpp").totalThreads == 2 &&
                  snapshot.files.value("src/a.cpp").unresolvedThreads == 1,
              "review model folds per-file thread counts");
        check(snapshot.files.value("src/b.cpp").totalThreads == 1 &&
                  snapshot.files.value("src/b.cpp").unresolvedThreads == 1,
              "review model treats legacy line-comments as unresolved threads");

        // --- Agent provenance from signed commit trailers (issue #365) --------
        // A PR whose commit series carries the ForkMesh-Agent trailer is
        // attributable to a tool/model without any local AgentSession; the
        // per-file map lets the review UI filter by authorship.
        {
            const QString agentMbox =
                "From 1111111 Mon Sep 17 00:00:00 2001\n"
                "From: Bot <bot@example.com>\n"
                "Subject: [PATCH 1/2] agent change\n\n"
                "Body line.\n\n"
                "ForkMesh-Agent: claude-code/claude-opus-4-8\n"
                "---\n"
                "diff --git a/src/agent.cpp b/src/agent.cpp\n"
                "index 000..111 100644\n"
                "--- a/src/agent.cpp\n+++ b/src/agent.cpp\n"
                "@@ -0,0 +1 @@\n+agent\n"
                "From 2222222 Mon Sep 17 00:00:00 2001\n"
                "From: Human <dev@example.com>\n"
                "Subject: [PATCH 2/2] human change\n\n"
                "Hand-written.\n"
                "---\n"
                "diff --git a/src/human.cpp b/src/human.cpp\n"
                "index 000..222 100644\n"
                "--- a/src/human.cpp\n+++ b/src/human.cpp\n"
                "@@ -0,0 +1 @@\n+human\n";
            PullRequest agentPr;
            agentPr.commits = agentMbox;
            const PullAgentProvenance prov = pullAgentProvenance(agentPr);
            check(prov.isAgent && prov.tool == "claude-code" &&
                      prov.model == "claude-opus-4-8",
                  "pullAgentProvenance reads the ForkMesh-Agent trailer");
            const QHash<QString, bool> authorship = pullFileAuthorship(agentPr);
            check(authorship.value("src/agent.cpp") == true &&
                      authorship.value("src/human.cpp") == false,
                  "pullFileAuthorship maps each file to its commit's authorship");

            PullRequest humanPr;
            humanPr.commits =
                "From 3333333 Mon Sep 17 00:00:00 2001\n"
                "From: Human <dev@example.com>\n"
                "Subject: [PATCH] plain change\n\n"
                "No trailer here.\n"
                "---\n"
                "diff --git a/src/x.cpp b/src/x.cpp\n";
            check(!pullAgentProvenance(humanPr).isAgent,
                  "pullAgentProvenance treats an un-trailered PR as human-authored");
        }

        // --- PullStore deletePullFile excises one file, keeps the rest -------
        // Regression for issue #258: deleting a file used to rebuild the PR by
        // replaying its commits onto the current base with `git am`, which drops
        // commits already present on the base and so could wipe most of the PR.
        // The delete now edits the stored diff/commits in place.
        {
            const QString baseBranch = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            auto writeFile = [&](const QString &rel, const QString &text) {
                check(writeTestFile(tmp.path() + "/" + rel, text.toUtf8()),
                      "write pull-delete fixture file");
            };
            git({"checkout", "-q", "-b", "feat-258"});
            writeFile("alpha.txt", "alpha-1\n");
            writeFile("beta.txt", "beta-1\n");
            git({"add", "alpha.txt", "beta.txt"});
            git({"commit", "-q", "-m", "add alpha and beta"});
            writeFile("beta.txt", "beta-2\n"); // a commit that touches only beta
            git({"add", "beta.txt"});
            git({"commit", "-q", "-m", "tweak beta"});
            writeFile("gamma.txt", "gamma-1\n");
            git({"add", "gamma.txt"});
            git({"commit", "-q", "-m", "add gamma"});
            const QString delPatch =
                QString::fromUtf8(gitOutput({"diff", baseBranch + "..feat-258"}));
            const QString delMbox = QString::fromUtf8(
                gitOutput({"format-patch", "--stdout", baseBranch + "..feat-258"}));
            git({"checkout", "-q", baseBranch});

            const int dn = pulls.createPull("Three files", "body", baseBranch,
                                            "feat-258", delPatch, delMbox,
                                            /*branchBacked=*/false, &err);
            check(dn > 0, "createPull stores a multi-file PR with a commit series");
            check(pulls.deletePullFile(dn, "beta.txt", &err),
                  "deletePullFile removes one file from the PR");

            PullRequest afterDel;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == dn)
                    afterDel = p;
            check(afterDel.patch.contains("alpha.txt") &&
                      afterDel.patch.contains("gamma.txt"),
                  "deleting one file leaves the other files in the patch (#258)");
            check(!afterDel.patch.contains("beta.txt"),
                  "the deleted file is gone from the patch");
            check(afterDel.filesChanged == 2,
                  "stats drop by exactly the one deleted file");
            check(afterDel.commits.contains("add alpha and beta") &&
                      afterDel.commits.contains("add gamma"),
                  "multi-file commits survive with their other changes intact");
            check(!afterDel.commits.contains("tweak beta") &&
                      !afterDel.commits.contains("beta.txt"),
                  "a commit touching only the deleted file is dropped from the series");

            // The trimmed commit series must still be a valid, appliable patch.
            check(pulls.mergePull(dn, &err),
                  "the PR still merges cleanly after a file was deleted from it");
            check(QFile::exists(tmp.path() + "/alpha.txt") &&
                      QFile::exists(tmp.path() + "/gamma.txt") &&
                      !QFile::exists(tmp.path() + "/beta.txt"),
                  "merging applies the kept files and not the deleted one");
        }

        // --- PullStore deletePull works with a dirty working tree -----------
        // Regression for adhoc #214: a plain delete commits path-scoped
        // (`commit -- pulls/<n>`), so unrelated tracked edits elsewhere (e.g.
        // uncommitted changes on main) never enter it and must not block the
        // deletion. Only the opt-in history rewrite needs a clean tree.
        {
            const QString baseBranch = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            check(writeTestFile(tmp.path() + "/del214.txt", "v1\n"),
                  "write pull-delete-dirty fixture file");
            git({"add", "del214.txt"});
            git({"commit", "-q", "-m", "add del214"});
            const QString patch214 =
                "diff --git a/del214.txt b/del214.txt\n"
                "index 000..111 100644\n--- a/del214.txt\n+++ b/del214.txt\n"
                "@@ -1 +1 @@\n-v1\n+v2\n";
            const int pn = pulls.createPull("Dirty tree", "body", baseBranch,
                                            "feat-214", patch214, QString(),
                                            /*branchBacked=*/false, &err);
            check(pn > 0, "createPull stores a PR to delete against a dirty tree");

            // Dirty the working tree with an unrelated tracked change.
            check(writeTestFile(tmp.path() + "/del214.txt", "dirty\n"),
                  "make an unrelated tracked change before deleting");

            QString delErr;
            check(pulls.deletePull(pn, /*rewriteHistory=*/false, &delErr),
                  "deletePull succeeds even with unrelated tracked changes");
            bool stillThere = false;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == pn)
                    stillThere = true;
            check(!stillThere, "the deleted PR is gone from the store");
            // Leave the tree clean for later blocks.
            git({"checkout", "-q", "--", "del214.txt"});
        }

        // --- PullStore branch-backed PRs keep the diff out of the repo -------
        // A PR whose head is a real branch stores only the signed pull.md
        // pointer; its diff and full commit series are reconstructed from
        // base..head, and the branch's own commits (with authorship) drive the
        // merge — nothing about the diff is committed into the repo.
        {
            const QString baseBranch = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            auto writeFile = [&](const QString &rel, const QString &text) {
                check(writeTestFile(tmp.path() + "/" + rel, text.toUtf8()),
                      "write branch-backed fixture file");
            };
            git({"checkout", "-q", "-b", "feat-bb"});
            writeFile("bb1.txt", "one\n");
            git({"add", "bb1.txt"});
            git({"commit", "-q", "-m", "bb: add one"});
            writeFile("bb2.txt", "two\n");
            git({"add", "bb2.txt"});
            git({"commit", "-q", "-m", "bb: add two"});
            git({"checkout", "-q", baseBranch});

            const int bn =
                pulls.createPull("Branch backed", "body", baseBranch, "feat-bb",
                                 QString(), QString(), /*branchBacked=*/true, &err);
            check(bn > 0, "createPull stores a branch-backed PR");
            // pulls/ metadata lives on its own linked worktree, not tmp.path()
            // (issue #399) - it never lands on whatever's checked out there.
            const QString metaDir = pulls.metaWorkTree();
            auto metaGitOutput = [&](const QStringList &args) {
                QProcess p;
                p.start("git", QStringList{"-C", metaDir} + args);
                p.waitForFinished(8000);
                return p.readAllStandardOutput();
            };
            const QString bdir = metaDir + "/pulls/" + QString::number(bn);
            check(!QFile::exists(bdir + "/changes.patch"),
                  "a branch-backed PR writes no changes.patch into the repo");
            check(QFile::exists(bdir + "/pull.md"),
                  "a branch-backed PR still records its signed pull.md pointer");
            const QString tracked = QString::fromUtf8(metaGitOutput(
                {"ls-tree", "-r", "--name-only", "HEAD",
                 "pulls/" + QString::number(bn)}));
            check(tracked.contains("pull.md") &&
                      !tracked.contains("changes.patch") &&
                      !tracked.contains("commits.mbox"),
                  "git tracks only pull.md for a branch-backed PR");

            PullRequest bb;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == bn)
                    bb = p;
            check(bb.patch.contains("bb1.txt") && bb.patch.contains("bb2.txt"),
                  "a branch-backed PR's diff is reconstructed from the refs");
            check(bb.commits.contains("bb: add one") &&
                      bb.commits.contains("bb: add two"),
                  "a branch-backed PR's full commit series is reconstructed");
            check(bb.filesChanged == 2,
                  "branch-backed stats come from the reconstructed diff");

            check(pulls.mergePull(bn, &err),
                  "a branch-backed PR merges by replaying its commits");
            check(QFile::exists(tmp.path() + "/bb1.txt") &&
                      QFile::exists(tmp.path() + "/bb2.txt"),
                  "merging a branch-backed PR applies its files");
            const QString log = QString::fromUtf8(gitOutput({"log", "--format=%s"}));
            check(log.contains("bb: add one") && log.contains("bb: add two"),
                  "merging a branch-backed PR preserves every authored commit");
            PullRequest merged;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == bn)
                    merged = p;
            check(merged.status == "merged",
                  "the branch-backed PR is marked merged");
            check(merged.patch.contains("bb1.txt"),
                  "a merged branch-backed PR's diff stays viewable via the snapshot");
        }

        // --- PullStore agent edit: multi-file changes on the PR's branch -----
        // The "Fix all with AI" flow (adhoc #82): check out the PR's branch
        // with the PR applied, let an agent edit any number of files, then
        // commit the lot on the branch and regenerate the PR's patch.
        {
            const QString baseBranch = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            auto writeFile = [&](const QString &rel, const QString &text) {
                check(writeTestFile(tmp.path() + "/" + rel, text.toUtf8()),
                      "write agent-edit fixture file");
            };
            git({"checkout", "-q", "-b", "feat-ai"});
            writeFile("ai1.txt", "first draft\n");
            git({"add", "ai1.txt"});
            git({"commit", "-q", "-m", "ai: add draft"});
            git({"checkout", "-q", baseBranch});

            const int an = pulls.createPull("Agent editable", "body", baseBranch,
                                            "feat-ai", QString(), QString(),
                                            /*branchBacked=*/true, &err);
            check(an > 0, "createPull stores the agent-editable PR");

            // The user's own checkout is dirty and sitting on the base branch —
            // neither must matter (adhoc #437): the edit runs in a scratch
            // worktree checked out at the PR's branch.
            writeFile("local-wip.txt", "uncommitted work\n");
            check(pulls.startPullAgentEdit(an, &err),
                  "startPullAgentEdit opens the PR's branch with a dirty base tree");
            const QString editDir = pulls.agentEditWorkTree();
            check(!editDir.isEmpty() && QFile::exists(editDir + "/ai1.txt"),
                  "the agent edits in a worktree holding the PR's branch content");
            check(QString::fromUtf8(
                      gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed()) ==
                      baseBranch,
                  "the user's checkout stays on its own branch during an agent edit");
            check(QFile::exists(tmp.path() + "/local-wip.txt"),
                  "the user's uncommitted work is untouched by an agent edit");
            check(writeTestFile(editDir + "/ai1.txt", QByteArray("fixed draft\n")) &&
                      writeTestFile(editDir + "/ai2.txt", QByteArray("brand new\n")),
                  "write the agent's edits into the edit worktree");
            check(pulls.finishPullAgentEdit(
                      an, QStringLiteral("pull #%1: apply AI review fixes").arg(an),
                      &err),
                  "finishPullAgentEdit commits the edits and finalizes");
            check(pulls.agentEditWorkTree().isEmpty() && !QFile::exists(editDir),
                  "the edit worktree is torn down once the fixes are committed");
            check(QString::fromUtf8(
                      gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed()) ==
                      baseBranch,
                  "finishing an agent edit leaves the user's checkout where it was");
            check(!pulls.conflictMergeInProgress(),
                  "no am session is left open after an agent edit");
            check(QString::fromUtf8(gitOutput({"log", "--format=%s", "-1", "feat-ai"})
                                        .trimmed()) ==
                      QStringLiteral("pull #%1: apply AI review fixes").arg(an),
                  "the fixes land on the PR's own branch");
            PullRequest edited;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == an)
                    edited = p;
            check(edited.status == "open",
                  "the PR stays open after an agent edit");
            check(edited.patch.contains("fixed draft") &&
                      edited.patch.contains("ai2.txt"),
                  "the PR's patch regenerates with every agent-edited file");
            check(!edited.patch.contains("local-wip.txt"),
                  "the base tree's uncommitted files stay out of the PR");

            // An agent run that changes nothing must refuse to commit, drop its
            // worktree, and leave the PR's branch exactly where it was.
            const QString beforeNoOp =
                QString::fromUtf8(gitOutput({"rev-parse", "feat-ai"}).trimmed());
            check(pulls.startPullAgentEdit(an, &err),
                  "a second agent-edit session opens on the same PR");
            const QString noOpDir = pulls.agentEditWorkTree();
            check(!pulls.finishPullAgentEdit(an, QStringLiteral("no-op"), &err),
                  "an agent session with no changes refuses to commit");
            check(!noOpDir.isEmpty() && !QFile::exists(noOpDir),
                  "a no-op agent edit tears its worktree down");
            check(QString::fromUtf8(gitOutput({"rev-parse", "feat-ai"}).trimmed()) ==
                      beforeNoOp,
                  "a no-op agent edit leaves the PR's branch untouched");
            // A stored-patch PR has no branch to edit at, so the scratch
            // worktree starts at the base and replays the patch there — again
            // without needing the user's checkout to be clean.
            check(writeTestFile(tmp.path() + "/ap1.txt", "v1\n"),
                  "write patch-PR fixture file");
            git({"add", "ap1.txt"});
            git({"commit", "-q", "-m", "add ap1"});
            const QString patchAp =
                "diff --git a/ap1.txt b/ap1.txt\n"
                "index 000..111 100644\n--- a/ap1.txt\n+++ b/ap1.txt\n"
                "@@ -1 +1 @@\n-v1\n+v2\n";
            const int pn = pulls.createPull("Patch backed", "body", baseBranch,
                                            "feat-ai-patch", patchAp, QString(),
                                            /*branchBacked=*/false, &err);
            check(pn > 0, "createPull stores the patch-backed PR");
            check(pulls.startPullAgentEdit(pn, &err),
                  "startPullAgentEdit replays a patch-backed PR into its worktree");
            const QString patchDir = pulls.agentEditWorkTree();
            QFile replayed(patchDir + "/ap1.txt");
            check(!patchDir.isEmpty() && replayed.open(QIODevice::ReadOnly) &&
                      replayed.readAll() == QByteArray("v2\n"),
                  "the replayed patch is what the agent edits");
            replayed.close();
            check(writeTestFile(patchDir + "/ap1.txt", QByteArray("v3\n")),
                  "write the agent's edit to the replayed patch");
            check(pulls.finishPullAgentEdit(pn, QStringLiteral("pull: fix"), &err),
                  "finishPullAgentEdit commits a patch-backed PR's edits");
            PullRequest patched;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == pn)
                    patched = p;
            check(patched.patch.contains("+v3"),
                  "the patch-backed PR's diff picks up the agent's edit");

            // Conflict fixing uses the same isolation guarantee. Build a PR
            // whose branch and current base changed the same line, then leave
            // both tracked and untracked work in the user's checkout.
            check(writeTestFile(tmp.path() + "/agent-conflict.txt",
                                QByteArray("shared\n")),
                  "write the conflict-agent base fixture");
            git({"add", "agent-conflict.txt"});
            git({"commit", "-q", "-m", "add conflict-agent fixture"});
            git({"checkout", "-q", "-b", "feat-agent-conflict"});
            check(writeTestFile(tmp.path() + "/agent-conflict.txt",
                                QByteArray("from pull\n")),
                  "write the conflict-agent pull side");
            git({"add", "agent-conflict.txt"});
            git({"commit", "-q", "-m", "change conflict-agent fixture in pull"});
            git({"checkout", "-q", baseBranch});
            check(writeTestFile(tmp.path() + "/agent-conflict.txt",
                                QByteArray("from base\n")),
                  "write the conflict-agent base side");
            git({"add", "agent-conflict.txt"});
            git({"commit", "-q", "-m", "change conflict-agent fixture on base"});
            const int cn = pulls.createPull(
                "Agent conflict", "body", baseBranch, "feat-agent-conflict",
                QString(), QString(), /*branchBacked=*/true, &err);
            check(cn > 0, "createPull stores the conflict-agent PR");
            check(writeTestFile(tmp.path() + "/ap1.txt",
                                QByteArray("uncommitted local edit\n")),
                  "leave a tracked user edit before conflict-agent work");
            QStringList conflictFiles;
            bool conflictClean = false;
            check(pulls.startConflictAgentEdit(
                      cn, &conflictFiles, &conflictClean, &err),
                  "conflict agent starts while the user's tree is dirty");
            const QString conflictDir = pulls.agentEditWorkTree();
            check(!conflictClean && !conflictDir.isEmpty() &&
                      conflictFiles.contains("agent-conflict.txt"),
                  "the agent receives conflict markers in its scratch worktree");
            QFile localDirty(tmp.path() + "/ap1.txt");
            check(localDirty.open(QIODevice::ReadOnly) &&
                      localDirty.readAll() == QByteArray("uncommitted local edit\n"),
                  "starting conflict resolution leaves tracked local work untouched");
            localDirty.close();
            check(writeTestFile(conflictDir + "/agent-conflict.txt",
                                QByteArray("resolved by agent\n")),
                  "resolve the conflict inside the scratch worktree");
            check(pulls.finishConflictMerge(cn, &err),
                  "finishConflictMerge commits the isolated agent resolution");
            check(pulls.agentEditWorkTree().isEmpty() &&
                      !QFile::exists(conflictDir),
                  "finishing conflict resolution removes its scratch worktree");
            check(localDirty.open(QIODevice::ReadOnly) &&
                      localDirty.readAll() == QByteArray("uncommitted local edit\n"),
                  "finishing conflict resolution leaves tracked local work untouched");
            localDirty.close();
            PullRequest conflictResolved;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == cn)
                    conflictResolved = p;
            check(conflictResolved.status == "open" &&
                      conflictResolved.head ==
                          QStringLiteral("pull/%1-agent-fix").arg(cn) &&
                      conflictResolved.patch.contains("resolved by agent"),
                  "the resolved PR stays open on its isolated agent-fix branch");

            // Leave the tree clean again for the tests that follow.
            git({"checkout", "--", "ap1.txt"});
            QFile::remove(tmp.path() + "/local-wip.txt");
        }

        // --- A branch-backed PR survives a corrupt stored blob by rebuilding
        // --- its commits from the mirror's refs ------------------------------
        // Reproduces the "corrupt binary patch" failure: a PR pushed from
        // another node whose head branch has not landed in this working tree
        // falls back to the committed commits.mbox, which the inbox can deliver
        // truncated. Reconstructing the series from real git objects (the
        // mirror) sidesteps the damaged blob entirely.
        {
            auto writeBytes = [&](const QString &rel, const QByteArray &bytes) {
                check(writeTestFile(tmp.path() + "/" + rel, bytes),
                      "write binary-pull fixture file");
            };
            const QString cbBase = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            git({"checkout", "-q", "-b", "feat-bin"});
            QByteArray binary; // a genuine binary file → a "GIT binary patch"
            for (int i = 0; i < 256; ++i)
                binary.append(static_cast<char>(i));
            writeBytes("asset.bin", binary);
            git({"add", "asset.bin"});
            git({"commit", "-q", "-m", "bin: add asset"});
            git({"checkout", "-q", cbBase});

            PullStore plain(tmp.path(), QString(), &identity, "tester");
            const int pbn = plain.createPull("Binary change", "body", cbBase,
                                             "feat-bin", QString(), QString(),
                                             /*branchBacked=*/true, &err);
            check(pbn > 0,
                  "createPull stores a branch-backed PR carrying a binary file");
            PullRequest seen;
            for (const PullRequest &p : plain.loadAll())
                if (p.number == pbn)
                    seen = p;
            check(seen.commits.contains("GIT binary patch"),
                  "the binary file reconstructs as a real binary patch from refs");
            // The flat diff must also carry the literal binary delta (derived with
            // `git diff --binary`), not just "Binary files … differ" — otherwise
            // the `git apply` merge fallback and the checkMergeable dry-run can't
            // replay binary changes.
            check(seen.patch.contains("GIT binary patch"),
                  "the reconstructed flat patch embeds the binary delta, not just "
                  "a \"Binary files differ\" marker");

            // Mirror the repo, then make this node look like one that holds the
            // PR pointer but not its branch, with a truncated commits.mbox where
            // the inbox would have dropped one.
            QTemporaryDir mirrorRoot;
            const QString mirror = mirrorRoot.path() + "/mirror.git";
            {
                QProcess clone;
                clone.start("git",
                            {"clone", "--bare", "-q", tmp.path(), mirror});
                clone.waitForFinished(8000);
            }
            git({"branch", "-D", "feat-bin"});
            // Also drop the materialized PR ref (issue #399): deleting the
            // named branch alone no longer strands the commits (the ref keeps
            // them reachable independently), so simulating "no reachable refs
            // anywhere" needs both gone.
            git({"update-ref", "-d",
                QStringLiteral("refs/pr/%1/head").arg(pbn)});
            // pulls/ metadata lives on its own linked worktree, not tmp.path()
            // (issue #399).
            const QString metaDir = plain.metaWorkTree();
            auto writeMetaBytes = [&](const QString &rel, const QByteArray &bytes) {
                check(writeTestFile(metaDir + "/" + rel, bytes),
                      "write pull metadata fixture file");
            };
            writeMetaBytes(
                "pulls/" + QString::number(pbn) + "/commits.mbox",
                QByteArray(
                    "From 0000000000000000000000000000000000000000 Mon Sep 17 "
                    "00:00:00 2001\nFrom: x <x@x>\nDate: Thu, 1 Jan 1970 "
                    "00:00:00 +0000\nSubject: [PATCH] bin\n\n---\n asset.bin | "
                    "Bin\n\ndiff --git a/asset.bin b/asset.bin\nnew file mode "
                    "100644\nindex 0000000000000000000000000000000000000000.."
                    "1111111111111111111111111111111111111111\nGIT binary "
                    "patch\nliteral 256\nzcmZ!!CORRUPT!!\n\nliteral 0\n\n-- "
                    "\n2.0.0\n"));

            // With no reachable refs, the corrupt blob is the only source: fail.
            PullStore noMirror(tmp.path(), QString(), &identity, "tester");
            QString blobErr;
            check(!noMirror.mergePull(pbn, &blobErr),
                  "a branch-backed PR with no reachable refs and a corrupt blob "
                  "fails to apply");

            // With the mirror, the commits come from real objects and apply.
            PullStore viaMirror(tmp.path(), mirror, &identity, "tester");
            check(viaMirror.mergePull(pbn, &err),
                  "the PR merges once its commits are rebuilt from the mirror");
            QFile applied(tmp.path() + "/asset.bin");
            check(applied.open(QIODevice::ReadOnly) &&
                      applied.readAll() == binary,
                  "merging restores the binary file's exact bytes from the "
                  "mirror's commits");
        }

        // --- checkMergeable previews a branch-backed PR via a real ref-merge --
        // The mergeability check runs `git merge-tree` on the actual commits, so
        // it reports a clean merge or names the conflicting files with no patch
        // (and none of a `git apply --check`'s missing-blob / context-drift false
        // conflicts).
        {
            auto writeFile = [&](const QString &rel, const QString &text) {
                check(writeTestFile(tmp.path() + "/" + rel, text.toUtf8()),
                      "write merge-tree fixture file");
            };
            const QString mtBase = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            writeFile("mt.txt", "orig-1\norig-2\norig-3\n");
            git({"add", "mt.txt"});
            git({"commit", "-q", "-m", "mt: seed"});

            // A branch-backed PR that only adds a new file → merges cleanly.
            git({"checkout", "-q", "-b", "feat-mt-clean"});
            writeFile("mt-new.txt", "added\n");
            git({"add", "mt-new.txt"});
            git({"commit", "-q", "-m", "mt: add a separate file"});
            git({"checkout", "-q", mtBase});
            const int cleanN =
                pulls.createPull("mt clean", "body", mtBase, "feat-mt-clean",
                                 QString(), QString(), /*branchBacked=*/true, &err);
            check(cleanN > 0, "createPull stores the non-overlapping branch PR");
            bool clean = false;
            QStringList cf;
            check(pulls.checkMergeable(cleanN, &clean, &cf, &err) && clean,
                  "checkMergeable reports a non-overlapping branch-backed PR as "
                  "clean (via merge-tree, no patch)");

            // A branch-backed PR that rewrites line 2, then the base rewrites the
            // same line differently → a genuine content conflict merge-tree finds.
            git({"checkout", "-q", "-b", "feat-mt-conflict", mtBase});
            writeFile("mt.txt", "orig-1\nFEATURE-2\norig-3\n");
            git({"add", "mt.txt"});
            git({"commit", "-q", "-m", "mt: feature rewrites line 2"});
            git({"checkout", "-q", mtBase});
            writeFile("mt.txt", "orig-1\nBASE-2\norig-3\n");
            git({"add", "mt.txt"});
            git({"commit", "-q", "-m", "mt: base rewrites line 2"});
            const int confN =
                pulls.createPull("mt conflict", "body", mtBase, "feat-mt-conflict",
                                 QString(), QString(), /*branchBacked=*/true, &err);
            check(confN > 0, "createPull stores the overlapping branch PR");
            bool clean2 = true;
            QStringList cf2;
            check(pulls.checkMergeable(confN, &clean2, &cf2, &err) && !clean2,
                  "checkMergeable reports an overlapping branch-backed PR as not "
                  "clean");
            check(cf2.contains(QStringLiteral("mt.txt")),
                  "checkMergeable names the conflicting file from the ref-merge");
        }

        // --- CommitCommentStore round-trip -------------------------------
        const QByteArray head = gitOutput({"rev-parse", "HEAD"}).trimmed();
        CommitCommentStore comments(tmp.path(), QString(), &identity, "tester");
        check(comments.addComment(QString::fromUtf8(head), "great commit", &err),
              "commit addComment succeeds");
        const QList<CommitComment> loadedComments =
            comments.loadFor(QString::fromUtf8(head));
        check(loadedComments.size() == 1 && loadedComments.first().body == "great commit",
              "commit comment round-trips from commits/<sha>/NNNN-comment.md");

        // --- CoveStore round-trip ----------------------------------------
        // Create an encrypted cove, confirm the committed file is opaque, then
        // unlock it from a fresh envelope read and verify the documents.
        CoveStore coves(tmp.path(), QString(), &identity, "tester");
        check(coves.canWrite(), "cove store reports the temp work tree as writable");
        CoveDocument cdoc;
        cdoc.id = "doc-1";
        cdoc.name = "Prod secrets";
        cdoc.body = "DB_PASSWORD=hunter2";
        Cove created;
        check(coves.createCove("Launch plans", "team-shared-password", true,
                               {cdoc}, &created, &err),
              "createCove encrypts, writes and commits a .cove file");
        check(created.creator == identity.publicKey(),
              "the cove records its creator's public key");

        const QByteArray onDisk =
            gitOutput({"show", QStringLiteral("HEAD:") + created.relPath});
        check(!onDisk.isEmpty() && onDisk.contains("\"kind\""),
              "the committed cove is a JSON envelope");
        check(!onDisk.contains("hunter2") && !onDisk.contains("DB_PASSWORD"),
              "the committed cove never leaks the plaintext document body");
        check(!onDisk.contains("Launch plans"),
              "the committed cove never leaks the cove's name");
        check(!onDisk.contains(identity.publicKey().toUtf8()),
              "the committed cove never leaks the creator's public key");
        check(!onDisk.contains("\"creator\"") && !onDisk.contains("\"access\"") &&
                  !onDisk.contains("\"notifyOnOpen\"") &&
                  !onDisk.contains("\"createdAtMs\""),
              "the committed envelope carries no identifying metadata keys");
        check(onDisk.contains("\"grants\""),
              "the committed envelope carries uniform grant slots");
        const QByteArray coveLog = gitOutput(
            {"log", "-1", "--format=%an <%ae>", "--", created.relPath});
        check(coveLog.trimmed() == "forkmesh <coves@forkmesh.invalid>",
              "cove commits use a neutral git author, not the creator's identity");
        check(!created.slug.contains("launch", Qt::CaseInsensitive) &&
                  !created.relPath.contains("launch", Qt::CaseInsensitive),
              "the cove's repo filename is an obscure slug, not its name");

        Cove reloaded;
        check(coves.loadEnvelope(created.relPath, reloaded, &err),
              "the cove envelope reloads (metadata only)");
        check(!reloaded.unlocked && reloaded.documents.isEmpty(),
              "a reloaded cove starts locked with no decrypted documents");
        check(reloaded.name.isEmpty(),
              "a locked cove envelope exposes no name (it is encrypted)");
        check(reloaded.creator.isEmpty() && reloaded.creatorAccount.isEmpty() &&
                  reloaded.invitedAccounts.isEmpty() && reloaded.accessMode.isEmpty() &&
                  !reloaded.notifyOnOpen && reloaded.createdAtMs == 0,
              "a locked cove envelope exposes no creator, accounts, mode or times");
        check(!CoveStore::unlock(reloaded, "WRONG-password"),
              "unlock rejects the wrong cove password");
        check(CoveStore::unlock(reloaded, "team-shared-password"),
              "unlock accepts the correct cove password");
        check(reloaded.unlocked && reloaded.documents.size() == 1 &&
                  reloaded.documents.first().body == "DB_PASSWORD=hunter2",
              "the unlocked cove yields the original document");
        check(reloaded.name == "Launch plans",
              "unlock recovers the cove's name from the encrypted payload");
        check(reloaded.creator == identity.publicKey() && reloaded.notifyOnOpen &&
                  reloaded.accessMode == "password" && reloaded.createdAtMs > 0,
              "unlock recovers the creator, mode and notify flag from the payload");

        // Appending an access entry and saving carries the trail in the payload.
        CoveAccessEntry visit;
        visit.who = identity.publicKey();
        visit.name = "tester";
        visit.ts = 1700000000000LL;
        visit.action = "open";
        CoveStore::appendAccess(reloaded, visit);
        check(coves.save(reloaded, "team-shared-password", &err),
              "saving a cove with an appended access entry succeeds");
        Cove afterLog;
        check(coves.loadEnvelope(created.relPath, afterLog, &err) &&
                  CoveStore::unlock(afterLog, "team-shared-password") &&
                  afterLog.accessLog.size() == 1 &&
                  afterLog.accessLog.first().who == identity.publicKey(),
              "the encrypted access log round-trips through save + unlock");

        const QList<Cove> listed = coves.listCoves();
        check(listed.size() == 1 && listed.first().name.isEmpty(),
              "listCoves enumerates envelopes without leaking the cove name");

        // --- Account-scoped coves: anonymous v2 envelopes -----------------
        // The ACL lives inside the ciphertext; access is granted by unwrapping a
        // per-account key grant, and the envelope reveals no accounts, no mode
        // and no member count (grant slots are padded with decoys).
        Cove teamCove;
        check(coves.createAccountCove("Team plans", "Alice", {"bob"}, false,
                                      {cdoc}, &teamCove, &err),
              "createAccountCove encrypts, writes and commits a .cove file");

        const QByteArray teamDisk =
            gitOutput({"show", QStringLiteral("HEAD:") + teamCove.relPath});
        check(!teamDisk.contains("alice") && !teamDisk.contains("Alice") &&
                  !teamDisk.contains("bob"),
              "the committed account cove never leaks creator or invitee names");
        check(!teamDisk.contains("account") && !teamDisk.contains("\"access\"") &&
                  !teamDisk.contains(identity.publicKey().toUtf8()),
              "the committed account cove reveals neither its mode nor its creator");
        check(!teamDisk.contains("hunter2") && !teamDisk.contains("Team plans"),
              "the committed account cove keeps its body and name encrypted");

        Cove teamLoaded;
        check(coves.loadEnvelope(teamCove.relPath, teamLoaded, &err) &&
                  teamLoaded.grants.size() == 4,
              "account grant slots are padded with decoys to hide the member count");
        Cove asMallory = teamLoaded;
        check(!CoveStore::unlockForAccount(asMallory, "mallory"),
              "an uninvited account cannot unlock an account cove");
        Cove asBob = teamLoaded;
        check(CoveStore::unlockForAccount(asBob, "BOB"),
              "an invited account unlocks the cove (case-insensitively)");
        check(asBob.name == "Team plans" && asBob.accessMode == "account" &&
                  asBob.creatorAccount == "alice" &&
                  asBob.invitedAccounts.contains("bob") &&
                  asBob.documents.size() == 1 &&
                  asBob.documents.first().body == "DB_PASSWORD=hunter2",
              "unlockForAccount recovers the encrypted ACL, name and documents");
        check(CoveStore::accountCanAccess(asBob, "alice") &&
                  !CoveStore::accountCanAccess(asBob, "mallory"),
              "the decrypted ACL answers accountCanAccess after unlock");

        // Inviting another account re-wraps the content key; the newcomer can
        // then unlock while the envelope still names nobody.
        asBob.invitedAccounts << "carol";
        check(coves.saveAccountCove(asBob, &err),
              "saving an account cove with a new invitee succeeds");
        Cove asCarol;
        check(coves.loadEnvelope(teamCove.relPath, asCarol, &err) &&
                  CoveStore::unlockForAccount(asCarol, "carol"),
              "a newly invited account can unlock the re-sealed cove");
        check(!gitOutput({"show", QStringLiteral("HEAD:") + teamCove.relPath})
                   .contains("carol"),
              "the re-sealed envelope still never names the invited accounts");

        // --- Legacy v1 account coves still unlock (and upgrade on save) ---
        // v1 stored the ACL in plaintext and derived the secret from it; write
        // one by hand and confirm the modern reader still opens it.
        {
            const QString coveId = "11111111-2222-3333-4444-555555555555";
            const QByteArray salt = CoveCrypto::randomSalt();
            const int rounds = 2048; // keep the test fast; v1 honors stored rounds
            const QByteArray material =
                QStringLiteral("forkmesh-account-cove-v1\n%1\n%2\n%3\n%4")
                    .arg(coveId, identity.publicKey(), "alice", "alice\nbob")
                    .toUtf8();
            const QString secret = QString::fromLatin1(
                QCryptographicHash::hash(material, QCryptographicHash::Sha256)
                    .toBase64(QByteArray::Base64UrlEncoding |
                              QByteArray::OmitTrailingEquals));
            CoveCrypto legacyCrypto(secret, salt, rounds);
            const QJsonObject legacyCipher = legacyCrypto.encrypt(
                QJsonDocument(QJsonObject{{"name", "Old team cove"},
                                          {"documents", QJsonArray{}},
                                          {"accessLog", QJsonArray{}}})
                    .toJson(QJsonDocument::Compact));
            const QJsonObject legacyEnvelope{
                {"kind", "cove"},
                {"v", 1},
                {"id", coveId},
                {"creator", identity.publicKey()},
                {"access", QJsonObject{{"mode", "account"},
                                       {"creatorAccount", "alice"},
                                       {"invitedAccounts", QJsonArray{"alice", "bob"}}}},
                {"kdf", QJsonObject{{"algo", "pbkdf2-sha256"},
                                    {"rounds", rounds},
                                    {"salt", QString::fromLatin1(salt.toBase64())}}},
                {"cipher", legacyCipher}};
            const QString legacyRel = CoveStore::covesDirRel() + "/legacyv1.cove";
            QDir().mkpath(tmp.path() + "/" + CoveStore::covesDirRel());
            QFile legacyFile(tmp.path() + "/" + legacyRel);
            check(legacyFile.open(QIODevice::WriteOnly) &&
                      legacyFile.write(QJsonDocument(legacyEnvelope).toJson()) > 0,
                  "a legacy v1 account cove envelope writes to the coves dir");
            legacyFile.close();

            Cove legacy;
            check(coves.loadEnvelope(legacyRel, legacy, &err) &&
                      legacy.creatorAccount == "alice" &&
                      CoveStore::accountCanAccess(legacy, "bob"),
                  "a legacy v1 envelope still exposes its plaintext ACL on load");
            check(!CoveStore::unlockForAccount(legacy, "mallory"),
                  "a legacy v1 cove still rejects uninvited accounts");
            check(CoveStore::unlockForAccount(legacy, "bob") &&
                      legacy.name == "Old team cove",
                  "a legacy v1 cove still unlocks via the v1 derived secret");
            check(coves.saveAccountCove(legacy, &err),
                  "saving a legacy cove upgrades it to a v2 envelope");
            const QByteArray upgraded =
                gitOutput({"show", QStringLiteral("HEAD:") + legacyRel});
            check(!upgraded.contains("alice") && !upgraded.contains("bob") &&
                      !upgraded.contains("account") &&
                      !upgraded.contains(identity.publicKey().toUtf8()),
                  "the upgraded envelope no longer names accounts, mode or creator");
            Cove upgradedCove;
            check(coves.loadEnvelope(legacyRel, upgradedCove, &err) &&
                      CoveStore::unlockForAccount(upgradedCove, "bob") &&
                      upgradedCove.name == "Old team cove",
                  "the upgraded cove still unlocks for its invited accounts");
        }
    }

    // AgentJail (adhoc #236): jailed launches get a memory cap prepended to the
    // shell command and a private scratch environment; a zero/negative cap
    // leaves the command untouched so unjailed runs are byte-identical.
    {
        const QString cmd = QStringLiteral("exec claude --model 'opus'");
        check(AgentJail::wrapCommand(cmd, 0) == cmd,
              "jail off leaves the launch command unchanged");
        check(AgentJail::wrapCommand(cmd, -5) == cmd,
              "a negative memory cap means no jail");
        check(AgentJail::wrapCommand(QString(), 1024).isEmpty(),
              "an empty command stays empty even with a cap");
        check(AgentJail::wrapCommand(cmd, 2048) ==
                  QStringLiteral(
                      "ulimit -d 2097152 2>/dev/null; exec claude --model 'opus'"),
              "the cap is applied in KB via ulimit ahead of the exec");
        check(AgentJail::wrapCommand(QStringLiteral("run %1 %2"), 1) ==
                  QStringLiteral("ulimit -d 1024 2>/dev/null; run %1 %2"),
              "percent placeholders in the command survive wrapping");

        QTemporaryDir jailTmp;
        check(jailTmp.isValid(), "agent jail temp dir is valid");
        const QString jailDir = jailTmp.path() + QStringLiteral("/jail");
        const QStringList env = AgentJail::envEntries(jailDir);
        check(env.contains(QStringLiteral("TMPDIR=") + jailDir +
                           QStringLiteral("/tmp")),
              "jail env redirects TMPDIR into the jail");
        check(env.contains(QStringLiteral("XDG_CACHE_HOME=") + jailDir +
                           QStringLiteral("/cache")),
              "jail env redirects the cache into the jail");
        check(QDir(jailDir + QStringLiteral("/tmp")).exists() &&
                  QDir(jailDir + QStringLiteral("/cache")).exists(),
              "jail scratch directories are created up front");
        check(AgentJail::sessionJailDir(7).endsWith(
                  QStringLiteral("/forkmesh-agent-jails/s7")),
              "stream sessions get a per-session jail dir under temp");
    }

    // AgentStore persists a Claude Code session's stream-json transcript so it
    // survives an app restart (issue #41): events append one per line, reload in
    // order, and clearEvents starts a fresh run.
    {
        QTemporaryDir tmp;
        check(tmp.isValid(), "agent store temp dir is valid");
        AgentStore store(tmp.path());
        AgentSession session;
        session.owner = "octo";
        session.name = "demo";
        session = store.createSession(session);

        check(store.loadEvents(session).isEmpty(),
              "a fresh agent session has no persisted transcript events");

        store.appendEvent(session, QJsonObject{{"type", "_local_user"},
                                               {"text", "do the thing"}});
        store.appendEvent(session,
                          QJsonObject{{"type", "assistant"}, {"seq", 2}});
        const QList<QJsonObject> events = store.loadEvents(session);
        check(events.size() == 2, "appended transcript events reload from disk");
        check(!events.isEmpty() &&
                  events.first().value("type").toString() == "_local_user" &&
                  events.first().value("text").toString() == "do the thing",
              "the first reloaded event is the initial user prompt, intact");
        check(events.size() == 2 && events.at(1).value("seq").toInt() == 2,
              "transcript events reload in append order");

        // Reopening the store mimics an app restart: the transcript is still there.
        AgentStore reopened(tmp.path());
        const QList<AgentSession> sessions = reopened.loadAllSessions();
        check(sessions.size() == 1 &&
                  reopened.loadEvents(sessions.first()).size() == 2,
              "transcript events survive reopening the store (app restart)");

        store.clearEvents(session);
        check(store.loadEvents(session).isEmpty(),
              "clearEvents starts the next run with a clean transcript");
    }

    // The Claude Code run summary the CLI reports on finish ("done · N turns ·
    // Ms · $X") is stored on the session and survives a restart (issue #296).
    {
        QTemporaryDir tmp;
        check(tmp.isValid(), "run-summary store temp dir is valid");
        AgentStore store(tmp.path());
        AgentSession session;
        session.owner = "octo";
        session.name = "demo";
        session = store.createSession(session);
        session.numTurns = 71;
        session.durationMs = 828000;
        session.costUsd = 4.59;
        check(store.saveSession(session), "saving a session with a run summary succeeds");

        AgentStore reopened(tmp.path());
        const QList<AgentSession> sessions = reopened.loadAllSessions();
        check(sessions.size() == 1 && sessions.first().numTurns == 71 &&
                  sessions.first().durationMs == 828000 &&
                  qAbs(sessions.first().costUsd - 4.59) < 1e-9,
              "turns, duration and cost reload intact after restart");
    }

    {
        // Workflow variable substitution must expand the explicit
        // ${{ vars.NAME }} context form and any declared ${NAME} variables, but
        // must NOT touch a workflow's own shell variables — otherwise a release
        // step building "releases/${channel}/forkmesh-${os}-${arch}" from shell
        // variables collapses to "releases//forkmesh--" (the reported bug).
        QMap<QString, QString> vars;
        vars.insert(QStringLiteral("TOKEN"), QStringLiteral("s3cr3t"));

        const QString shellScript =
            QStringLiteral("channel=\"${RELEASE_CHANNEL:-latest}\"\n"
                           "os=linux; arch=x86_64\n"
                           "dest=\"releases/${channel}/forkmesh-${os}-${arch}\"");
        const QString out = ActionFile::substitute(shellScript, vars);
        check(out == shellScript,
              "substitute leaves unknown ${NAME} shell variables untouched");

        check(ActionFile::substitute(
                  QStringLiteral("auth ${{ vars.TOKEN }} and ${TOKEN}"), vars) ==
                  QStringLiteral("auth s3cr3t and s3cr3t"),
              "substitute expands declared vars via both ${{ vars.X }} and ${X}");

        check(ActionFile::substitute(
                  QStringLiteral("x=${{ vars.MISSING }}-end"), vars) ==
                  QStringLiteral("x=-end"),
              "substitute blanks unknown ${{ vars.X }} context references");
    }

    {
        // Workflow `on:` triggers parse into the predicates the runner gates on:
        // a release workflow fires on a published release, not on every push.
        const ActionWorkflow rel = ActionFile::parse(
            QStringLiteral(".forkmesh/release.yml"),
            QStringLiteral("name: Release\non: [release, workflow_dispatch]\n"
                           "jobs:\n  release:\n    steps:\n"
                           "      - run: echo hi\n"));
        check(rel.valid, "release workflow parses");
        check(rel.triggersOnRelease(), "on: release sets triggersOnRelease()");
        check(!rel.triggersOnPush(), "on: release does not trigger on push");
        check(rel.allowsManualRun(), "workflow_dispatch stays a manual trigger");

        const ActionWorkflow push = ActionFile::parse(
            QStringLiteral(".forkmesh/ci.yml"),
            QStringLiteral("name: CI\non: [push]\n"
                           "jobs:\n  test:\n    steps:\n"
                           "      - run: echo hi\n"));
        check(push.triggersOnPush(), "on: push sets triggersOnPush()");
        check(!push.triggersOnRelease(), "on: push does not trigger on release");
    }

    {
        // Dedicated nodes: `runs-on:` pins a workflow to named machines, so the
        // mesh can send tests to one node, Cloudflare deploys to a mirror, and
        // the iOS build to a Mac. Every other node must skip it entirely.
        const ActionWorkflow anywhere = ActionFile::parse(
            QStringLiteral(".forkmesh/ci.yml"),
            QStringLiteral("name: CI\non: [push]\n"
                           "jobs:\n  test:\n    steps:\n      - run: echo hi\n"));
        check(anywhere.runsOn.isEmpty(), "no runs-on leaves the workflow undedicated");
        check(anywhere.runsOnNode({QStringLiteral("mirror2")}),
              "an undedicated workflow runs on any node");

        const ActionWorkflow tests = ActionFile::parse(
            QStringLiteral(".forkmesh/tests.yml"),
            QStringLiteral("name: Tests\non: [push]\nruns-on: forkmesh\n"
                           "jobs:\n  test:\n    steps:\n      - run: ctest\n"));
        check(tests.runsOn == QStringList{QStringLiteral("forkmesh")},
              "top-level runs-on parses into one label");
        check(tests.runsOnNode({QStringLiteral("forkmesh"), QStringLiteral("linux")}),
              "the named node runs its dedicated workflow");
        check(!tests.runsOnNode({QStringLiteral("mirror2"), QStringLiteral("linux")}),
              "another node skips a workflow dedicated elsewhere");
        check(tests.runsOnNode({QStringLiteral("ForkMesh")}),
              "runs-on matching is case-insensitive");

        const ActionWorkflow deploy = ActionFile::parse(
            QStringLiteral(".forkmesh/deploy.yml"),
            QStringLiteral("name: Deploy\non: [push]\n"
                           "runs-on: [mirror2, mirror3]\n"
                           "jobs:\n  deploy:\n    steps:\n      - run: wrangler deploy\n"));
        check(deploy.runsOn.size() == 2 && deploy.runsOnNode({QStringLiteral("mirror3")}),
              "a runs-on list lets any listed node take the workflow");
        check(!deploy.runsOnNode({QStringLiteral("mac1")}),
              "a node outside the runs-on list stays out");

        const ActionWorkflow ios = ActionFile::parse(
            QStringLiteral(".forkmesh/ios.yml"),
            QStringLiteral("name: iOS\non: [workflow_dispatch]\n"
                           "jobs:\n  build:\n    runs-on:\n      - mac1\n"
                           "    steps:\n      - run: flutter build ios\n"));
        check(ios.runsOn == QStringList{QStringLiteral("mac1")},
              "job-level runs-on block lists parse too");
        check(!ios.runsOnNode({QStringLiteral("forkmesh")}),
              "a Linux node never picks up the Mac's iOS build");

        const ActionWorkflow any = ActionFile::parse(
            QStringLiteral(".forkmesh/any.yml"),
            QStringLiteral("name: Any\non: [push]\nruns-on: any\n"
                           "jobs:\n  j:\n    steps:\n      - run: echo hi\n"));
        check(any.runsOnNode({QStringLiteral("mirror2")}),
              "the reserved \"any\" label matches every node");

        // This node's own labels: node name, mirror-executor name, platform, and
        // whatever capability tags the operator typed in Settings.
        const QStringList labels = ActionFile::nodeLabels(
            QStringLiteral("Mac1"), QString(),
            QStringLiteral("ios, xcode  flutter,ios"));
        check(labels.contains(QStringLiteral("mac1")),
              "the machine node name is always a label, lower-cased");
        check(labels.contains(QStringLiteral("ios")) &&
                  labels.contains(QStringLiteral("xcode")) &&
                  labels.contains(QStringLiteral("flutter")),
              "configured labels split on commas and whitespace");
        check(labels.count(QStringLiteral("ios")) == 1,
              "duplicate labels are collapsed");
        check(ActionFile::nodeLabels(QString(), QStringLiteral("mirror2"),
                                     QString())
                  .contains(QStringLiteral("mirror2")),
              "a headless node answers to its mirror-executor node name");
        check(ActionFile::parseLabelList(QStringLiteral("  ")).isEmpty(),
              "an empty label list normalizes to nothing");
    }

    {
        // `needs:` orders one workflow behind another for the same commit, so a
        // deploy can trust the CI run instead of repeating its test suite.
        const ActionWorkflow deploy = ActionFile::parse(
            QStringLiteral(".forkmesh/deploy.yml"),
            QStringLiteral("name: Deploy\non: [push]\n"
                           "needs: [.forkmesh/ci.yml]\n"
                           "jobs:\n  deploy:\n    steps:\n"
                           "      - run: ./deploy.sh\n"));
        check(deploy.valid &&
                  deploy.needs == QStringList{QStringLiteral(".forkmesh/ci.yml")},
              "top-level needs parses into one dependency");

        const ActionWorkflow jobLevel = ActionFile::parse(
            QStringLiteral(".forkmesh/publish.yml"),
            QStringLiteral("name: Publish\non: [push]\n"
                           "jobs:\n  publish:\n    needs:\n      - CI tests\n"
                           "    steps:\n      - run: echo hi\n"));
        check(jobLevel.needs == QStringList{QStringLiteral("CI tests")},
              "job-level needs block lists parse too");

        // Jobs inside one file are flattened into a single step list, so a
        // GitHub-style dependency between sibling jobs must not be read as a
        // dependency on another workflow (which would never resolve).
        const ActionWorkflow siblings = ActionFile::parse(
            QStringLiteral(".forkmesh/build.yml"),
            QStringLiteral("name: Build\non: [push]\n"
                           "jobs:\n  compile:\n    steps:\n      - run: make\n"
                           "  test:\n    needs: compile\n    steps:\n"
                           "      - run: make test\n"));
        check(siblings.needs.isEmpty(),
              "needs naming a job in the same file is not a workflow dependency");

        check(ActionNeeds::matches(QStringLiteral("ci"), QStringLiteral("CI tests"),
                                   QStringLiteral(".forkmesh/ci.yml")) &&
                  ActionNeeds::matches(QStringLiteral("CI TESTS"),
                                       QStringLiteral("CI tests"),
                                       QStringLiteral(".forkmesh/ci.yml")) &&
                  ActionNeeds::matches(QStringLiteral(".forkmesh/ci.yml"),
                                       QStringLiteral("CI tests"),
                                       QStringLiteral(".forkmesh/ci.yml")),
              "a needs entry matches by name, path, or file name, case-insensitively");
        check(!ActionNeeds::matches(QStringLiteral("ci-qt"),
                                    QStringLiteral("CI tests"),
                                    QStringLiteral(".forkmesh/ci.yml")),
              "a needs entry does not match an unrelated workflow");

        ActionRun dependency;
        dependency.id = 1;
        dependency.owner = QStringLiteral("forkmesh");
        dependency.name = QStringLiteral("forkmesh");
        dependency.workflowPath = QStringLiteral(".forkmesh/ci.yml");
        dependency.workflowName = QStringLiteral("CI tests");
        dependency.commit = QStringLiteral("abc123");
        ActionRun dependent = dependency;
        dependent.id = 2;
        dependent.workflowPath = QStringLiteral(".forkmesh/deploy.yml");
        dependent.workflowName = QStringLiteral("Deploy Cloudflare Worker");
        const QStringList needs{QStringLiteral(".forkmesh/ci.yml")};

        dependency.status = ActionStatus::Running;
        check(ActionNeeds::resolve(dependent, needs, {dependency}) ==
                  ActionNeeds::State::Waiting,
              "a dependent run waits while its dependency is still running");
        dependency.status = ActionStatus::AwaitingApproval;
        check(ActionNeeds::resolve(dependent, needs, {dependency}) ==
                  ActionNeeds::State::Waiting,
              "an unapproved dependency keeps the dependent queued");
        dependency.status = ActionStatus::Success;
        check(ActionNeeds::resolve(dependent, needs, {dependency}) ==
                  ActionNeeds::State::Ready,
              "a succeeded dependency releases the dependent run");
        dependency.status = ActionStatus::Failed;
        QString detail;
        check(ActionNeeds::resolve(dependent, needs, {dependency}, &detail) ==
                  ActionNeeds::State::Blocked &&
                  detail.contains(ActionStatus::Failed),
              "a failed dependency blocks the dependent run and reports why");

        // A dependency at a different commit is a different result entirely; the
        // gate is per-commit, and a missing record must not wedge the queue.
        dependency.status = ActionStatus::Success;
        ActionRun otherCommit = dependency;
        otherCommit.commit = QStringLiteral("def456");
        otherCommit.status = ActionStatus::Failed;
        check(ActionNeeds::resolve(dependent, needs, {otherCommit}) ==
                  ActionNeeds::State::Ready,
              "runs of the dependency at other commits do not gate this one");
        check(ActionNeeds::resolve(dependent, needs, {}) ==
                  ActionNeeds::State::Ready,
              "a dependency that never ran on this node is not a barrier");
        check(ActionNeeds::resolve(dependent, {}, {dependency}) ==
                  ActionNeeds::State::Ready,
              "a workflow with no needs is always ready");

        // Re-running a workflow prepends a fresh record; the newest one wins.
        ActionRun rerun = dependency;
        rerun.id = 3;
        rerun.status = ActionStatus::Running;
        ActionRun superseded = dependency;
        superseded.status = ActionStatus::Cancelled;
        check(ActionNeeds::resolve(dependent, needs, {rerun, superseded}) ==
                  ActionNeeds::State::Waiting,
              "the newest run of a dependency decides the gate");
    }

    // --- Secret scanning -------------------------------------------------------
    {
        // Helper: init a fresh git repo, commit fileContent, then call
        // findSecretsInPush with no upstream ref (full tracked-file scan).
        const auto runSecretTest = [](const QByteArray &fileContent,
                                      const QString &expectedTitle) -> bool {
            QTemporaryDir td;
            if (!td.isValid())
                return false;
            const QString dir = td.path();
            const auto git = [&](const QStringList &args) {
                QProcess p;
                p.start(QStringLiteral("git"),
                        QStringList{QStringLiteral("-C"), dir} + args);
                p.waitForFinished(10000);
            };
            git({QStringLiteral("init")});
            git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@t")});
            git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")});
            check(writeTestFile(dir + QStringLiteral("/secret.env"), fileContent),
                  "write secret-scan fixture file");
            git({QStringLiteral("add"), QStringLiteral("secret.env")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("add")});
            const QList<RepoSecurityFinding> findings =
                RepoSecurity::findSecretsInPush(dir, {});
            for (const RepoSecurityFinding &f : findings)
                if (f.title == expectedTitle)
                    return true;
            return false;
        };

        // Per-provider checks (file-scan path)
        check(runSecretTest("GITHUB_TOKEN=ghp_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("GitHub token")),
              "secret scan detects GitHub PAT (ghp_)");
        check(runSecretTest("GITHUB_TOKEN=github_pat_AAAAAAAAAAAAAAAAAAAAAA\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("GitHub fine-grained PAT")),
              "secret scan detects GitHub fine-grained PAT (github_pat_)");
        check(runSecretTest("AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("AWS access key")),
              "secret scan detects AWS long-term access key (AKIA)");
        check(runSecretTest("KEY=ASIAQFI2EXAMPLE123456789012345\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("AWS temporary access key")),
              "secret scan detects AWS temporary STS key (ASIA)");
        check(runSecretTest(
                  "SLACK_TOKEN=xoxb-123456789012-123456789012-abcdefghijklmnopqrstuvwx\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("Slack token")),
              "secret scan detects Slack token (xoxb-)");
        check(runSecretTest(
                  "OPENAI_KEY=sk-proj-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("OpenAI API key")),
              "secret scan detects OpenAI project key (sk-proj-)");
        check(runSecretTest(
                  "OPENAI_KEY=sk-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuv\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("OpenAI API key")),
              "secret scan detects OpenAI legacy key (sk- + 48 chars)");
        check(runSecretTest(
                  "ANTHROPIC_KEY=sk-ant-api03-ABCDEFGHIJKLMNOPQRSTUVWXYZabcde\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("Anthropic API key")),
              "secret scan detects Anthropic API key (sk-ant-)");
        check(runSecretTest("STRIPE_KEY=sk_live_ABCDEFGHIJKLMNOPQRSTUVWXyz\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Stripe secret key")),
              "secret scan detects Stripe secret key (sk_live_)");
        check(runSecretTest("STRIPE_KEY=rk_test_ABCDEFGHIJKLMNOPQRSTUVWXyz\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Stripe restricted key")),
              "secret scan detects Stripe restricted key (rk_test_)");
        check(runSecretTest("GOOGLE_KEY=AIzaSyDOCAbC123dEf456GhI789jKl012-MnOAB\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Google API key")),
              "secret scan detects Google API key (AIza)");
        check(runSecretTest("GOOGLE_OAUTH=ya29.A0ARrda1ABCDEFGHIJKLMNOPQRSTUVWXYZ\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Google OAuth token")),
              "secret scan detects Google OAuth token (ya29.)");
        check(runSecretTest("GOOGLE_SECRET=GOCSPX-ABCDEFGHIJKLMNOPQRSTUVWXabcde\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Google OAuth client secret")),
              "secret scan detects Google OAuth client secret (GOCSPX-)");
        check(runSecretTest(
                  "SG=SG.AAAAAAAAAAAAAAAAAAAAAA.BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("SendGrid API key")),
              "secret scan detects SendGrid API key (SG.)");
        check(runSecretTest("TWILIO=SKabcdef1234567890abcdef1234567890\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("Twilio auth token")),
              "secret scan detects Twilio auth token (SK + 32 hex chars)");
        check(runSecretTest("NPM=npm_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("npm access token")),
              "secret scan detects npm access token (npm_)");
        check(runSecretTest("VAULT=hvs.CAESIABC123defGHI456jklMNO789pqr\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("HashiCorp Vault token")),
              "secret scan detects HashiCorp Vault service token (hvs.)");
        check(runSecretTest(
                  "CLOUDFLARE_API_TOKEN=aBcDeFgHiJkLmNoPqRsTuVwXyZaBcDeFgHiJkLMN\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("Cloudflare API token")),
              "secret scan detects Cloudflare API token (env-var anchored)");
        check(runSecretTest("-----BEGIN PRIVATE KEY-----\nMIIBIjANBg...\n"  // forkmesh-secret-scan:ignore-line
                            "-----END PRIVATE KEY-----\n",
                            QStringLiteral("PEM private key")),
              "secret scan detects PKCS#8 PEM private key block");
        check(runSecretTest("-----BEGIN RSA PRIVATE KEY-----\nMIIE...\n"  // forkmesh-secret-scan:ignore-line
                            "-----END RSA PRIVATE KEY-----\n",
                            QStringLiteral("PEM private key")),
              "secret scan detects RSA PEM private key block");
        check(runSecretTest(
                  "DATABASE_PASSWORD=\"s3cr3tPasswordThatIsLongEnough\"\n",  // forkmesh-secret-scan:ignore-line
                  QStringLiteral("Secret/token assignment")),
              "secret scan detects generic quoted secret assignment");

        // The generic assignment rule has no provider prefix to anchor it, so
        // recognisable placeholders (test fixtures, docs samples, template
        // holes) must not block a push.
        check(!runSecretTest(
                  "password: \"correct-horse-battery-staple\"\n",
                  QStringLiteral("Secret/token assignment")),
              "secret scan ignores the XKCD example password");
        check(!runSecretTest("API_TOKEN=\"your-token-goes-here-abcdef\"\n",
                             QStringLiteral("Secret/token assignment")),
              "secret scan ignores your-… placeholder token values");
        check(!runSecretTest("API_KEY=\"${FORKMESH_API_KEY_FROM_ENV}\"\n",
                             QStringLiteral("Secret/token assignment")),
              "secret scan ignores ${VAR} template holes");
        check(!runSecretTest("password=\"xxxxxxxxxxxxxxxxxxxxxxxx\"\n",
                             QStringLiteral("Secret/token assignment")),
              "secret scan ignores single-character filler runs");
        // A provider-prefixed hit stays high-confidence even next to
        // placeholder-ish wording.
        check(runSecretTest("EXAMPLE_TOKEN=ghp_BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n",  // forkmesh-secret-scan:ignore-line
                            QStringLiteral("GitHub token")),
              "secret scan still flags prefixed tokens in example wording");

        // Benign content must not trigger a false positive
        {
            QTemporaryDir td;
            const QString dir = td.path();
            const auto git = [&](const QStringList &args) {
                QProcess p;
                p.start(QStringLiteral("git"),
                        QStringList{QStringLiteral("-C"), dir} + args);
                p.waitForFinished(10000);
            };
            git({QStringLiteral("init")});
            git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@t")});
            git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")});
            check(writeTestFile(
                      dir + QStringLiteral("/readme.txt"),
                      "This is a benign file with no secrets.\nname: ForkMesh\n"),
                  "write benign secret-scan fixture file");
            git({QStringLiteral("add"), QStringLiteral("readme.txt")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("add")});
            check(RepoSecurity::findSecretsInPush(dir, {}).isEmpty(),
                  "secret scan produces no false positives on benign content");
        }

        // A line carrying the "forkmesh-secret-scan:ignore-line" marker is
        // skipped even though it matches a pattern — this is how the
        // scanner's own per-provider test fixtures (above) avoid flagging
        // themselves when this file is scanned as part of a tracked repo.
        {
            QTemporaryDir td;
            const QString dir = td.path();
            const auto git = [&](const QStringList &args) {
                QProcess p;
                p.start(QStringLiteral("git"),
                        QStringList{QStringLiteral("-C"), dir} + args);
                p.waitForFinished(10000);
            };
            git({QStringLiteral("init")});
            git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@t")});
            git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")});
            check(writeTestFile(dir + QStringLiteral("/fixtures.cpp"),
                                "AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE // forkmesh-secret-scan:ignore-line\n"),  // forkmesh-secret-scan:ignore-line
                  "write ignored secret-scan fixture file");
            git({QStringLiteral("add"), QStringLiteral("fixtures.cpp")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("add")});
            check(RepoSecurity::findSecretsInPush(dir, {}).isEmpty(),
                  "secret scan skips lines carrying the ignore-line marker");
        }

        // Diff-scan path: secrets introduced in HEAD commit are detected
        {
            QTemporaryDir td;
            const QString dir = td.path();
            const auto git = [&](const QStringList &args) {
                QProcess p;
                p.start(QStringLiteral("git"),
                        QStringList{QStringLiteral("-C"), dir} + args);
                p.waitForFinished(10000);
            };
            git({QStringLiteral("init")});
            git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@t")});
            git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")});
            // Base commit — clean
            check(writeTestFile(dir + QStringLiteral("/readme.txt"),
                                "placeholder\n"),
                  "write diff-scan base fixture file");
            git({QStringLiteral("add"), QStringLiteral("readme.txt")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("base")});
            // HEAD commit introduces a secret
            check(writeTestFile(dir + QStringLiteral("/creds.env"),
                                "AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n"),  // forkmesh-secret-scan:ignore-line
                  "write diff-scan secret fixture file");
            git({QStringLiteral("add"), QStringLiteral("creds.env")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("add creds")});
            const QList<RepoSecurityFinding> diffFindings =
                RepoSecurity::findSecretsInPush(dir, QStringLiteral("HEAD~1"));
            bool found = false;
            for (const RepoSecurityFinding &f : diffFindings)
                if (f.title == QStringLiteral("AWS access key"))
                    found = true;
            check(found, "diff-scan detects secrets added in pushed commits");
            check(RepoSecurity::findSecretsInPush(dir, QStringLiteral("HEAD")).isEmpty(),
                  "diff-scan reports no findings when HEAD has no new commits to push");
        }

        // Diff-scan path: secrets removed in a commit must NOT trigger
        {
            QTemporaryDir td;
            const QString dir = td.path();
            const auto git = [&](const QStringList &args) {
                QProcess p;
                p.start(QStringLiteral("git"),
                        QStringList{QStringLiteral("-C"), dir} + args);
                p.waitForFinished(10000);
            };
            git({QStringLiteral("init")});
            git({QStringLiteral("config"), QStringLiteral("user.email"),
                 QStringLiteral("t@t")});
            git({QStringLiteral("config"), QStringLiteral("user.name"),
                 QStringLiteral("T")});
            // Base commit WITH a secret (already in history)
            check(writeTestFile(dir + QStringLiteral("/creds.env"),
                                "AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n"),  // forkmesh-secret-scan:ignore-line
                  "write remediation base fixture file");
            git({QStringLiteral("add"), QStringLiteral("creds.env")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("base")});
            // HEAD commit removes the secret (remediation commit)
            check(writeTestFile(dir + QStringLiteral("/creds.env"),
                                "# credentials removed\n"),
                  "write remediation head fixture file");
            git({QStringLiteral("add"), QStringLiteral("creds.env")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("remove creds")});
            check(RepoSecurity::findSecretsInPush(dir, QStringLiteral("HEAD~1")).isEmpty(),
                  "diff-scan ignores secrets removed (not added) in commits");
        }
    }

    // --- Per-manifest dependency counts (adhoc #86) -----------------------
    {
        QTemporaryDir td;
        check(td.isValid(), "dependency-count temp repo created");
        const QString dir = td.path();
        const auto git = [&](const QStringList &args) {
            QProcess p;
            p.start(QStringLiteral("git"),
                    QStringList{QStringLiteral("-C"), dir} + args);
            p.waitForFinished(10000);
        };
        git({QStringLiteral("init")});
        git({QStringLiteral("config"), QStringLiteral("user.email"),
             QStringLiteral("t@t")});
        git({QStringLiteral("config"), QStringLiteral("user.name"),
             QStringLiteral("T")});
        // The scanner reads line-by-line (one "name": "version" entry per
        // line), so the fixture must be pretty-printed, not minified.
        check(writeTestFile(dir + QStringLiteral("/package.json"), R"JSON({
  "dependencies": {
    "left-pad": "1.0.0",
    "chalk": "*"
  },
  "devDependencies": {
    "jest": "^29.0.0"
  }
}
)JSON"),
              "write package manifest fixture file");
        check(writeTestFile(dir + QStringLiteral("/requirements.txt"),
                            "requests==2.31.0\nflask>=2.0\n# a comment\n\nclick\n"),
              "write requirements manifest fixture file");
        git({QStringLiteral("add"), QStringLiteral("package.json"),
             QStringLiteral("requirements.txt")});

        RepoSecurityInput input;
        input.owner = QStringLiteral("alice");
        input.name = QStringLiteral("project");
        input.localPath = dir;

        const RepoSecuritySnapshot snapshot = RepoSecurity::scan(input);
        check(snapshot.dependencyCounts.value(QStringLiteral("package.json")) == 3,
              "full scan counts every declared package.json dependency (prod + dev)");
        check(snapshot.dependencyCounts.value(QStringLiteral("requirements.txt")) == 3,
              "full scan counts requirements.txt entries, skipping blanks/comments");

        const RepoSecurityManifestScan pkgRescan =
            RepoSecurity::scanManifest(input, QStringLiteral("package.json"));
        check(pkgRescan.dependencyCount == 3,
              "single-manifest rescan matches the full scan's package.json count");
        bool flaggedChalk = false;
        for (const RepoSecurityFinding &f : pkgRescan.findings)
            if (f.title == QStringLiteral("chalk") && f.detail.contains(QLatin1Char('*')))
                flaggedChalk = true;
        check(flaggedChalk,
              "single-manifest rescan on package.json surfaces chalk's floating \"*\" version");

        const RepoSecurityManifestScan reqRescan =
            RepoSecurity::scanManifest(input, QStringLiteral("requirements.txt"));
        check(reqRescan.dependencyCount == 3,
              "single-manifest rescan matches the full scan's requirements.txt count");

        const RepoSecurityManifestScan missingRescan =
            RepoSecurity::scanManifest(input, QStringLiteral("does-not-exist.json"));
        check(missingRescan.dependencyCount == 0 && missingRescan.findings.isEmpty(),
              "single-manifest rescan on a missing/untracked path is a safe no-op");
    }

    {
        // Agent metadata is the only unsealed local session state. Provider
        // credentials are launch-time config and can never become workspace
        // metadata or a shared owner-sealed session payload by field alias.
        AgentSession session;
        session.id = 42;
        session.owner = QStringLiteral("alice");
        session.name = QStringLiteral("project");
        session.prompt = QStringLiteral("review the owner-sealed task");
        session.provider = QStringLiteral("claude-code");
        const QJsonObject serialized = session.toJson();
        for (const QString &forbidden :
             {QStringLiteral("accessToken"),
              QStringLiteral("refreshToken"),
              QStringLiteral("oauth"),
              QStringLiteral("apiKey"),
              QStringLiteral("anthropicApiKey"),
              QStringLiteral("openAiApiKey"),
              QStringLiteral("credentials")}) {
            check(!serialized.contains(forbidden),
                  qPrintable(QStringLiteral(
                      "agent metadata excludes provider credential field %1")
                                 .arg(forbidden)));
        }
    }

    {
        // Background-activity bus (adhoc #421): the footer strip is driven purely
        // by these tickets, so a lost or double-retired one leaves a spinner
        // running forever (or hides work that is still going).
        QStringList seen;
        forkmesh::BackgroundActivity::setListener(
            [&seen](quint64 id, const QString &kind, const QString &detail,
                    bool started) {
                seen.append(QStringLiteral("%1:%2:%3:%4")
                                .arg(started ? QStringLiteral("+")
                                             : QStringLiteral("-"))
                                .arg(id)
                                .arg(kind, detail));
            });
        const quint64 first =
            forkmesh::BackgroundActivity::begin(QStringLiteral("git"),
                                                QStringLiteral("git log"));
        const quint64 second =
            forkmesh::BackgroundActivity::begin(QStringLiteral("net"));
        check(first != second && first != 0 && second != 0,
              "each background ticket gets its own non-zero id");
        forkmesh::BackgroundActivity::end(first);
        forkmesh::BackgroundActivity::end(second);
        forkmesh::BackgroundActivity::end(0); // no-op guard for untracked work
        check(seen == QStringList({QStringLiteral("+:%1:git:git log").arg(first),
                                   QStringLiteral("+:%1:net:").arg(second),
                                   QStringLiteral("-:%1::").arg(first),
                                   QStringLiteral("-:%1::").arg(second)}),
              "every begin/end pair reaches the listener exactly once, in order");

        {
            const forkmesh::BackgroundScope scope(QStringLiteral("scan"));
            check(seen.size() == 5 && seen.last().startsWith(QLatin1Char('+')),
                  "a background scope opens its ticket on construction");
        }
        check(seen.size() == 6 && seen.last().startsWith(QLatin1Char('-')),
              "a background scope retires its ticket when it unwinds");

        forkmesh::BackgroundActivity::setListener(nullptr);
        forkmesh::BackgroundActivity::end(
            forkmesh::BackgroundActivity::begin(QStringLiteral("git")));
        check(seen.size() == 6,
              "a detached bus drops announcements instead of calling a dead "
              "listener");
    }

    {
        // Log outcome lines (adhoc #419): the ✓ / ✕ marker is what tells the user
        // which work actually made it into the background strip, so the threshold
        // it is derived from has to match the strip's own show delay.
        const QString ok = forkmesh::backgroundOkGlyph();
        const QString no = forkmesh::backgroundNotGlyph();
        check(forkmesh::backgroundOutcomeLine(QStringLiteral("git"), 1, 1400,
                                              QStringLiteral("git log")) ==
                  QStringLiteral("Background %1 git backgrounded (1.4s) - git log")
                      .arg(ok),
              "slow work logs a checkmark, its duration and the caller's note");
        check(forkmesh::backgroundOutcomeLine(QStringLiteral("git"), 24, 61,
                                              QString()) ==
                  QStringLiteral("Background %1 git %2%3 not backgrounded "
                                 "(longest 61ms)")
                      .arg(no)
                      .arg(QChar(0x00D7))
                      .arg(24),
              "a burst of too-fast tickets logs one red-x summary for the kind");
        check(forkmesh::backgroundOutcomeLine(
                  QString(), 1, forkmesh::kBackgroundShowAfterMs, QString())
                  .startsWith(QStringLiteral("Background %1 work").arg(ok)),
              "work exactly at the show delay counts as backgrounded, and an "
              "unnamed kind still reads as something");
        check(forkmesh::backgroundElapsedText(-5) == QStringLiteral("0ms") &&
                  forkmesh::backgroundElapsedText(999) ==
                      QStringLiteral("999ms") &&
                  forkmesh::backgroundElapsedText(1000) ==
                      QStringLiteral("1.0s"),
              "elapsed text stays short and never renders a negative clock skew");

        // Both log views paint the message body in one colour, so the marker is
        // recoloured after escaping — green for ✓, red for ✕, and only the first
        // of each so a note that happens to contain one can't smear the line.
        const QString painted = forkmesh::colorizeBackgroundMarker(
            QStringLiteral("Background %1 net %2").arg(no, ok));
        check(painted.contains(QStringLiteral("#f85149")) &&
                  painted.contains(QStringLiteral("#3fb950")),
              "the outcome marker is lifted into its own coloured span");
        check(forkmesh::colorizeBackgroundMarker(QStringLiteral("no marker")) ==
                  QStringLiteral("no marker"),
              "a line without a marker is passed through untouched");
    }

    if (failures) {
        qCritical("TESTS FAILED");
        return 1;
    }
    qInfo("ALL TESTS PASSED");
    return 0;
}
