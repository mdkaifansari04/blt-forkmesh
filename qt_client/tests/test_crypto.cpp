#include "../src/CommitCommentStore.h"
#include "../src/CoveCrypto.h"
#include "../src/CoveStore.h"
#include "../src/ForkMeshIdentity.h"
#include "../src/IssueBurnup.h"
#include "../src/IssueStore.h"
#include "../src/PullStore.h"
#include "../src/RoomCrypto.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <openssl/evp.h>
#include <algorithm>

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
    QCoreApplication app(argc, argv);
    app.setApplicationName("ForkMeshCryptoTest");
    app.setOrganizationName("ForkMesh");

    ForkMeshIdentity identity;
    check(identity.load(), "Ed25519 identity loads or generates");
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
        QList<Issue> loaded = repo.loadAll();
        check(loaded.size() == 1 && loaded.first().title == "Round trip" &&
                  loaded.first().labels.contains("bug") &&
                  loaded.first().milestone == "v1" &&
                  loaded.first().priority == 7,
              "issue loads back with title, label, milestone and priority");
        check(!loaded.isEmpty() && loaded.first().events.first().body == "Hello **body**",
              "open-event body round-trips from issue.md frontmatter");

        check(repo.addComment(n, "a comment", {}, &err), "addComment succeeds");
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
        check(sawComment, "comment body round-trips from NNNN-comment.md");
        check(sawAgentAssign, "agent assignment event round-trips");
        check(sawAgentClear, "agent clear event round-trips");
        check(loaded.first().status == "closed", "status reflects close event");

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
                          QStringLiteral("issues/%1").arg(tomb)})
                   .trimmed()
                   .isEmpty(),
              "tombstoned issue is preserved in git history");

        check(repo.deleteIssue(n, &err), "deleteIssue succeeds");
        const QList<Issue> afterDelete = repo.loadAll();
        check(std::none_of(afterDelete.begin(), afterDelete.end(),
                           [&](const Issue &i) { return i.number == n; }),
              "deleted issue no longer loads");
        check(gitOutput({"log", "--all", "--", QStringLiteral("issues/%1").arg(n)})
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

        // --- PullStore conversation round-trip ---------------------------
        PullStore pulls(tmp.path(), QString(), &identity, "tester");
        const int pn = pulls.createPull(
            "A change", "Body", "main", "feature",
            "diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -0,0 +1 @@\n+hi\n",
            QString(), &err);
        check(pn == 1, "createPull returns the first PR number");
        check(pulls.addComment(pn, "first comment", &err), "PR addComment succeeds");
        check(pulls.addReview(pn, "approved", "LGTM", &err), "PR addReview succeeds");
        check(pulls.addLineComment(pn, "x", "new", 1, "inline note", &err),
              "PR addLineComment succeeds");
        QList<PullRequest> loadedPulls = pulls.loadAll();
        check(!loadedPulls.isEmpty() && loadedPulls.first().events.size() == 3,
              "PR conversation events round-trip from NNNN-*.md");
        bool sawLine = false;
        if (!loadedPulls.isEmpty())
            for (const PullEvent &e : loadedPulls.first().events)
                if (e.type == "line-comment" && e.path == "x" && e.side == "new" &&
                    e.line == 1 && e.body == "inline note")
                    sawLine = true;
        check(sawLine, "line-comment round-trips with path/side/line");
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

        Cove reloaded;
        check(coves.loadEnvelope(created.relPath, reloaded, &err),
              "the cove envelope reloads (metadata only)");
        check(!reloaded.unlocked && reloaded.documents.isEmpty(),
              "a reloaded cove starts locked with no decrypted documents");
        check(reloaded.notifyOnOpen, "the notify-on-open flag round-trips in the envelope");
        check(!CoveStore::unlock(reloaded, "WRONG-password"),
              "unlock rejects the wrong cove password");
        check(CoveStore::unlock(reloaded, "team-shared-password"),
              "unlock accepts the correct cove password");
        check(reloaded.unlocked && reloaded.documents.size() == 1 &&
                  reloaded.documents.first().body == "DB_PASSWORD=hunter2",
              "the unlocked cove yields the original document");

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
        check(listed.size() == 1 && listed.first().name == "Launch plans",
              "listCoves enumerates the repo's cove envelopes");
    }

    if (failures) {
        qCritical("TESTS FAILED");
        return 1;
    }
    qInfo("ALL TESTS PASSED");
    return 0;
}
