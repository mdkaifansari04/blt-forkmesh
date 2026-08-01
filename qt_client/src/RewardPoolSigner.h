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



QString defaultPoolVaultPath();




QString poolAddressForKeyMaterial(const QByteArray &keyMaterial,
                                  QString *error = nullptr);




bool importPoolKey(const QByteArray &keyMaterial, const QString &passphrase,
                   const QString &vaultPath, QString *publicAddress = nullptr,
                   QString *error = nullptr);



QString poolVaultPublicAddress(const QString &vaultPath,
                               QString *error = nullptr);




bool parseRewardSigningJob(const QJsonObject &job,
                           const QString &expectedPoolAddress,
                           const QString &expectedNetwork,
                           const QDateTime &now,
                           RewardSigningJob *parsed,
                           QString *error = nullptr);





bool buildRewardTransactionIntent(const QJsonObject &job,
                                  const QString &expectedPoolAddress,
                                  const QString &expectedNetwork,
                                  const QString &recentBlockhash,
                                  quint64 lastValidBlockHeight,
                                  const QDateTime &now,
                                  QJsonObject *intent,
                                  QString *error = nullptr);



bool parseRewardTransferIntent(const QJsonObject &object,
                               const QString &expectedPoolAddress,
                               const QString &expectedNetwork,
                               const QDateTime &now,
                               RewardTransferIntent *intent,
                               QString *error = nullptr);




bool signRewardTransferIntent(const QJsonObject &object,
                              const QString &expectedNetwork,
                              const QString &passphrase,
                              const QString &vaultPath,
                              const QDateTime &now,
                              SignedRewardTransaction *signedTransaction,
                              QString *error = nullptr);



bool validatePublicRpcUrl(const QString &configured, QUrl *normalized = nullptr,
                          QString *error = nullptr);




QByteArray signingJobsFetchCanonical(const QString &signerAccount,
                                     const QString &timestampMs);
QByteArray signingJobSubmitCanonical(const QString &signerAccount,
                                     const QString &intentId,
                                     const QString &chainSignature,
                                     const QString &timestampMs);

QString encodeBase58(const QByteArray &bytes);
QByteArray decodeBase58(const QString &text, int expectedBytes = -1);
bool isValidSolanaSignature(const QString &signature);




QString redactRewardText(const QString &text,
                         const QStringList &exactSecrets = {});

void secureErase(QByteArray &bytes);
void secureErase(QString &text);

}
