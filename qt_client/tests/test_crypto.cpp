#include "../src/ActionFile.h"
#include "../src/AgentStore.h"
#include "../src/BackoffNetworkAccessManager.h"
#include "../src/ClaudeAccountTransfer.h"
#include "../src/CommitCommentStore.h"
#include "../src/CoveCrypto.h"
#include "../src/CoveStore.h"
#include "../src/DiscussionInboxBackoff.h"
#include "../src/DiscussionStore.h"
#include "../src/ForkMeshIdentity.h"
#include "../src/IssueBurnup.h"
#include "../src/IssueStore.h"
#include "../src/MirrorCrypto.h"
#include "../src/NetworkBackoff.h"
#include "../src/ProjectStore.h"
#include "../src/PullAiReview.h"
#include "../src/PullReviewModel.h"
#include "../src/PullStore.h"
#include "../src/ReferenceLinks.h"
#include "../src/RepoSecurity.h"
#include "../src/RoomCrypto.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <openssl/evp.h>
#include <algorithm>
#include <cstring>

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

} // namespace

int main(int argc, char *argv[])
{
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
    }

    // --- Host-auth token canonical (must match the worker's verify_host_token).
    const QByteArray hostCanonical = "forkmesh-host-v1\nalice\nmyrepo\n1000";
    check(hostCanonical ==
              QByteArray("forkmesh-host-v1\nalice\nmyrepo\n1000"),
          "host-token canonical matches the cross-language vector");
    const QString hostSig = identity.signData(hostCanonical);
    check(!hostSig.isEmpty(), "host token is signed by the node key");
    check(verifyEd25519(identity.publicKey(), hostSig, hostCanonical),
          "host-token signature verifies against the node public key");
    check(!verifyEd25519(identity.publicKey(), hostSig,
                         QByteArray("forkmesh-host-v1\nalice\nmyrepo\n2000")),
          "host-token signature is bound to its timestamp");

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
            QDir(tmp.path()).filePath(QStringLiteral(".forkmesh/issues/1/issue-1.json"));
        check(QFileInfo::exists(issueJsonPath),
              "createIssue writes .forkmesh/issues/1/issue-1.json");
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
        check(QDir(QDir(tmp.path()).filePath(QStringLiteral(".forkmesh/issues/1")))
                  .entryList(QStringList{QStringLiteral("*.md")}, QDir::Files)
                  .isEmpty(),
              "issue folder contains no markdown event files");
        check(repo.setStatus(n, "closed", &err), "setStatus succeeds");
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

        // Fast "regular" delete: a tombstone hides the issue from every list but
        // leaves its history intact in git (and recoverable).
        const int tomb = repo.createIssue("Tombstone me", "body", {}, QString(),
                                          0, {}, {}, &err);
        check(repo.tombstoneIssue(tomb, &err), "tombstoneIssue succeeds");
        const QList<Issue> afterTombstone = repo.loadAll();
        check(!afterTombstone.isEmpty() &&
                  std::none_of(afterTombstone.begin(), afterTombstone.end(),
                               [&](const Issue &i) { return i.number == tomb; }),
              "tombstoned issue no longer loads but others remain");
        check(!gitOutput({"log", "--all", "--",
                          QStringLiteral(".forkmesh/issues/%1").arg(tomb)})
                   .trimmed()
                   .isEmpty(),
              "tombstoned issue is preserved in git history");

        check(repo.deleteIssue(n, &err), "deleteIssue succeeds");
        const QList<Issue> afterDelete = repo.loadAll();
        check(std::none_of(afterDelete.begin(), afterDelete.end(),
                           [&](const Issue &i) { return i.number == n; }),
              "deleted issue no longer loads");
        check(gitOutput({"log", "--all", "--",
                         QStringLiteral(".forkmesh/issues/%1").arg(n)})
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
            "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -0,0 +1 @@\n+hi\n",
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
                QFile f(tmp.path() + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(text.toUtf8());
                f.close();
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

        // --- PullStore branch-backed PRs keep the diff out of the repo -------
        // A PR whose head is a real branch stores only the signed pull.md
        // pointer; its diff and full commit series are reconstructed from
        // base..head, and the branch's own commits (with authorship) drive the
        // merge — nothing about the diff is committed into the repo.
        {
            const QString baseBranch = QString::fromUtf8(
                gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed());
            auto writeFile = [&](const QString &rel, const QString &text) {
                QFile f(tmp.path() + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(text.toUtf8());
                f.close();
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
                QFile f(tmp.path() + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(text.toUtf8());
                f.close();
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
            check(pulls.startPullAgentEdit(an, &err),
                  "startPullAgentEdit opens the PR's branch");
            check(QString::fromUtf8(
                      gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed()) ==
                      QLatin1String("feat-ai"),
                  "the PR's branch is left checked out for the agent");
            writeFile("ai1.txt", "fixed draft\n");
            writeFile("ai2.txt", "brand new\n");
            check(pulls.finishPullAgentEdit(
                      an, QStringLiteral("pull #%1: apply AI review fixes").arg(an),
                      &err),
                  "finishPullAgentEdit commits the edits and finalizes");
            check(QString::fromUtf8(
                      gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed()) ==
                      baseBranch,
                  "finishing an agent edit returns to the original branch");
            check(!pulls.conflictMergeInProgress(),
                  "no am session is left open after an agent edit");
            PullRequest edited;
            for (const PullRequest &p : pulls.loadAll())
                if (p.number == an)
                    edited = p;
            check(edited.status == "open",
                  "the PR stays open after an agent edit");
            check(edited.patch.contains("fixed draft") &&
                      edited.patch.contains("ai2.txt"),
                  "the PR's patch regenerates with every agent-edited file");

            // An agent run that changes nothing must refuse to commit and
            // restore the original branch (the work branch tears down).
            check(pulls.startPullAgentEdit(an, &err),
                  "a second agent-edit session opens on the same PR");
            check(!pulls.finishPullAgentEdit(an, QStringLiteral("no-op"), &err),
                  "an agent session with no changes refuses to commit");
            check(QString::fromUtf8(
                      gitOutput({"rev-parse", "--abbrev-ref", "HEAD"}).trimmed()) ==
                      baseBranch,
                  "a no-op agent edit restores the original branch");
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
                QFile f(tmp.path() + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(bytes);
                f.close();
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
                QFile f(metaDir + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(bytes);
                f.close();
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
                QFile f(tmp.path() + "/" + rel);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write(text.toUtf8());
                f.close();
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
            {
                QFile f(dir + QStringLiteral("/secret.env"));
                f.open(QIODevice::WriteOnly);
                f.write(fileContent);
            }
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
            {
                QFile f(dir + QStringLiteral("/readme.txt"));
                f.open(QIODevice::WriteOnly);
                f.write("This is a benign file with no secrets.\nname: ForkMesh\n");
            }
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
            {
                QFile f(dir + QStringLiteral("/fixtures.cpp"));
                f.open(QIODevice::WriteOnly);
                f.write("AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE // forkmesh-secret-scan:ignore-line\n");  // forkmesh-secret-scan:ignore-line
            }
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
            {
                QFile f(dir + QStringLiteral("/readme.txt"));
                f.open(QIODevice::WriteOnly);
                f.write("placeholder\n");
            }
            git({QStringLiteral("add"), QStringLiteral("readme.txt")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("base")});
            // HEAD commit introduces a secret
            {
                QFile f(dir + QStringLiteral("/creds.env"));
                f.open(QIODevice::WriteOnly);
                f.write("AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n");  // forkmesh-secret-scan:ignore-line
            }
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
            {
                QFile f(dir + QStringLiteral("/creds.env"));
                f.open(QIODevice::WriteOnly);
                f.write("AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n");  // forkmesh-secret-scan:ignore-line
            }
            git({QStringLiteral("add"), QStringLiteral("creds.env")});
            git({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("base")});
            // HEAD commit removes the secret (remediation commit)
            {
                QFile f(dir + QStringLiteral("/creds.env"));
                f.open(QIODevice::WriteOnly);
                f.write("# credentials removed\n");
            }
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
        {
            QFile f(dir + QStringLiteral("/package.json"));
            f.open(QIODevice::WriteOnly);
            // The scanner reads line-by-line (one "name": "version" entry per
            // line), so the fixture must be pretty-printed, not minified.
            f.write(R"JSON({
  "dependencies": {
    "left-pad": "1.0.0",
    "chalk": "*"
  },
  "devDependencies": {
    "jest": "^29.0.0"
  }
}
)JSON");
        }
        {
            QFile f(dir + QStringLiteral("/requirements.txt"));
            f.open(QIODevice::WriteOnly);
            f.write("requests==2.31.0\nflask>=2.0\n# a comment\n\nclick\n");
        }
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
        // Claude Code account transfer: bundle round-trips and installs onto a
        // host's ~/.claude/.credentials.json (issue: move the owner's login onto
        // hosts so they can take agent requests).
        QJsonObject oauth;
        oauth.insert("accessToken", "tok-secret-1234");
        oauth.insert("refreshToken", "refresh-abcd");
        oauth.insert("subscriptionType", "max");
        oauth.insert("expiresAt", double(4102444800000LL));

        const QString encoded =
            ClaudeAccountTransfer::encodeBundle(oauth, "sk-ant-key", "owner-node");
        check(!encoded.contains("tok-secret-1234"),
              "encoded bundle is base64, not plaintext token");

        QJsonObject roundOauth;
        QString roundKey, err;
        check(ClaudeAccountTransfer::parseBundle(encoded.toUtf8(), roundOauth,
                                                 roundKey, err),
              "base64 bundle parses back");
        check(roundOauth.value("accessToken").toString() == "tok-secret-1234" &&
                  roundKey == "sk-ant-key",
              "bundle round-trips the oauth token and API key");

        // Raw JSON (an exported keyfile) is accepted too.
        const QByteArray rawJson =
            ClaudeAccountTransfer::buildBundle(oauth, "sk-ant-key", "owner-node");
        QJsonObject jsonOauth;
        QString jsonKey, jsonErr;
        check(ClaudeAccountTransfer::parseBundle(rawJson, jsonOauth, jsonKey,
                                                 jsonErr) &&
                  jsonOauth.value("accessToken").toString() == "tok-secret-1234",
              "raw-JSON bundle parses too");

        // Junk / foreign JSON is rejected.
        QJsonObject junkOauth;
        QString junkKey, junkErr;
        check(!ClaudeAccountTransfer::parseBundle("{\"kind\":\"nope\"}", junkOauth,
                                                  junkKey, junkErr),
              "a non-ForkMesh bundle is rejected");

        // Install writes ~/.claude/.credentials.json under a temp home and
        // preserves any sibling keys already there.
        QTemporaryDir homeDir;
        check(homeDir.isValid(), "temp home for install test");
        QDir().mkpath(homeDir.path() + "/.claude");
        QFile pre(homeDir.path() + "/.claude/.credentials.json");
        check(pre.open(QIODevice::WriteOnly), "seed an existing creds file");
        pre.write("{\"other\":\"keep-me\"}");
        pre.close();

        QString installErr;
        check(ClaudeAccountTransfer::installOauth(homeDir.path(), roundOauth,
                                                  installErr),
              "installOauth writes credentials.json");
        check(ClaudeAccountTransfer::hasCredentials(homeDir.path()),
              "installed home now reports a Claude Code login");
        QFile post(homeDir.path() + "/.claude/.credentials.json");
        check(post.open(QIODevice::ReadOnly), "read back installed creds");
        const QJsonObject installed =
            QJsonDocument::fromJson(post.readAll()).object();
        post.close();
        check(installed.value("claudeAiOauth").toObject().value("accessToken")
                      .toString() == "tok-secret-1234",
              "installed creds carry the transferred token");
        check(installed.value("other").toString() == "keep-me",
              "install merges rather than clobbering sibling keys");

        const QString desc = ClaudeAccountTransfer::describeOauth(oauth);
        check(!desc.contains("tok-secret-1234") && desc.contains("1234"),
              "describeOauth masks the token to its tail");
    }

    if (failures) {
        qCritical("TESTS FAILED");
        return 1;
    }
    qInfo("ALL TESTS PASSED");
    return 0;
}
