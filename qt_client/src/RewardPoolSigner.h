#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

namespace forkmesh::rewards {

struct RewardTransfer {
    QString destinationWallet;
    QString nodeId;
    quint64 lamports = 0;
};

// Worker-authored public plan as returned by /api/rewards/signing-jobs. It
// contains no blockhash, signature, or private material; the desktop turns it
// into an exact transaction only after local review.
struct RewardSigningJob {
    QString intentId;
    QString kind;
    QString status;
    QString network;
    QString sourceWallet;
    QString policy;
    QString transactionSignature;
    qint64 createdAtMs = 0;
    qint64 expiresAtMs = 0;
    quint64 totalLamports = 0;
    QList<RewardTransfer> transfers;
};

// The exact transfer that was independently authored by the reward scheduler
// and then verified locally. Only a legacy, single-signature set of System
// Program transfers is accepted; arbitrary programs and extra instructions
// fail closed.
struct RewardTransferIntent {
    QString intentId;
    QString type;
    QString network;
    QString sourceWallet;
    QString destinationWallet;
    QString recentBlockhash;
    QString roundId;
    QString selectedNodeId;
    quint64 lamports = 0;
    quint64 totalLamports = 0;
    quint64 lastValidBlockHeight = 0;
    QList<RewardTransfer> transfers;
    QDateTime createdAt;
    QDateTime expiresAt;
    QByteArray unsignedTransaction;
    QByteArray message;
    int signatureOffset = -1;
};

struct SignedRewardTransaction {
    QString intentId;
    QString signerPublicKey;
    QString chainSignature;
    QString signedTransactionBase64;
    QString transactionSha256;
};

// Default encrypted-vault location under the application's private data
// directory. Callers may pass an explicit path for tests or managed installs.
QString defaultPoolVaultPath();

// Inspect an existing Solana CLI/desktop-wallet secret key without saving it.
// Only an exact 64-byte Ed25519 keypair is accepted (JSON byte array or base58),
// and its embedded public half must match the private seed.
QString poolAddressForKeyMaterial(const QByteArray &keyMaterial,
                                  QString *error = nullptr);

// Import an existing key into a scrypt + AES-256-GCM encrypted local vault.
// This function never generates a key. The vault file and its directory must be
// reducible to owner-only permissions or the operation fails without writing.
bool importPoolKey(const QByteArray &keyMaterial, const QString &passphrase,
                   const QString &vaultPath, QString *publicAddress = nullptr,
                   QString *error = nullptr);

// Reads only the clear public address from a structurally valid encrypted
// envelope. It never decrypts the private seed.
QString poolVaultPublicAddress(const QString &vaultPath,
                               QString *error = nullptr);

// Validate the existing Worker's public signing-job envelope/plan. Submitted
// jobs are accepted for reconciliation display; only pending_signature jobs can
// be turned into a transaction by buildRewardTransactionIntent().
bool parseRewardSigningJob(const QJsonObject &job,
                           const QString &expectedPoolAddress,
                           const QString &expectedNetwork,
                           const QDateTime &now,
                           RewardSigningJob *parsed,
                           QString *error = nullptr);

// Add a locally fetched recent blockhash to a verified pending job and build a
// size-bounded legacy transaction containing exactly its ordered System Program
// transfers. Large plans that do not fit one Solana transaction fail closed and
// must be chunked into separate Worker intents.
bool buildRewardTransactionIntent(const QJsonObject &job,
                                  const QString &expectedPoolAddress,
                                  const QString &expectedNetwork,
                                  const QString &recentBlockhash,
                                  quint64 lastValidBlockHeight,
                                  const QDateTime &now,
                                  QJsonObject *intent,
                                  QString *error = nullptr);

// Verify the Worker-authored chain intent against the locally configured pool
// address/network and the exact serialized Solana transaction.
bool parseRewardTransferIntent(const QJsonObject &object,
                               const QString &expectedPoolAddress,
                               const QString &expectedNetwork,
                               const QDateTime &now,
                               RewardTransferIntent *intent,
                               QString *error = nullptr);

// Re-validates the intent, unlocks the local vault, signs only the serialized
// Solana message, constructs the signed transaction, and scrubs private bytes
// before returning. No private material is returned.
bool signRewardTransferIntent(const QJsonObject &object,
                              const QString &expectedNetwork,
                              const QString &passphrase,
                              const QString &vaultPath,
                              const QDateTime &now,
                              SignedRewardTransaction *signedTransaction,
                              QString *error = nullptr);

// Reward signing deliberately accepts only a configured public HTTPS JSON-RPC
// origin: no credentials, query, fragment, or token-like path may be persisted.
bool validatePublicRpcUrl(const QString &configured, QUrl *normalized = nullptr,
                          QString *error = nullptr);

// Canonical forms already enforced by /api/rewards/signing-jobs. The Worker
// binds signerAccount to REWARD_SIGNER_ACCOUNT/INSTANCE_OWNER_ACCOUNT and
// verifies the existing ForkMesh owner identity signature plus timestamp.
QByteArray signingJobsFetchCanonical(const QString &signerAccount,
                                     const QString &timestampMs);
QByteArray signingJobSubmitCanonical(const QString &signerAccount,
                                     const QString &intentId,
                                     const QString &chainSignature,
                                     const QString &timestampMs);

QString encodeBase58(const QByteArray &bytes);
QByteArray decodeBase58(const QString &text, int expectedBytes = -1);
bool isValidSolanaSignature(const QString &signature);

// Defensive display/log filter for RPC/Worker errors. Private-key-shaped JSON
// fields, authorization headers, supplied exact secrets, controls, and excess
// output are removed.
QString redactRewardText(const QString &text,
                         const QStringList &exactSecrets = {});

void secureErase(QByteArray &bytes);
void secureErase(QString &text);

} // namespace forkmesh::rewards
