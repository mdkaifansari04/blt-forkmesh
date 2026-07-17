#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <optional>

class ForkMeshIdentity;

struct RepoContributionSnapshot {
    QJsonObject payload;
    QByteArray compactPayload;
    QString error;
    bool complete = false;
};

struct RepoContributionSnapshotInput {
    QString workTreePath;
    QString mirrorPath;
    QString branch;
    QString head;
    QString publishingKey;
    qint64 capturedAtMs = 0;
    int retentionYears = 5;
};

struct RepoContributionPreparation {
    QString dependencyFingerprint;
    std::optional<RepoContributionSnapshot> rebuiltSnapshot;
};

class RepoContributionPublicationCache
{
public:
    enum class BeginResult { Started, Coalesced, CapacityExceeded };
    struct Completion {
        bool requestDialog = false;
        bool discarded = false;
    };

    explicit RepoContributionPublicationCache(int maxEntries = 64,
                                              int maxInFlight = 4);

    static QString key(const QString &owner, const QString &repo,
                       const QString &head, const QString &branch,
                       const QString &accountPublicKey,
                       const QString &dependencyFingerprint);

    std::optional<RepoContributionSnapshot> lookup(const QString &key,
                                                   qint64 nowMs) const;
    BeginResult begin(const QString &key, bool requestDialog);
    bool finish(const QString &key, RepoContributionSnapshot snapshot,
                qint64 nowMs);
    Completion complete(const QString &key);
    void store(const QString &key, RepoContributionSnapshot snapshot,
               qint64 nowMs);
    bool inFlight(const QString &key) const;
    void invalidate(const QString &key);
    bool claimStaleRetry(const QString &publishKey,
                         const QString &snapshotKey);
    void clearStaleRetry(const QString &publishKey);

private:
    void evictOldest();

    int m_maxEntries = 64;
    int m_maxInFlight = 4;
    QHash<QString, RepoContributionSnapshot> m_results;
    QHash<QString, qint64> m_storedAtMs;
    QSet<QString> m_inFlight;
    QSet<QString> m_dialogRequested;
    QSet<QString> m_discardOnFinish;
    QHash<QString, QString> m_staleRetrySnapshotKey;
};

RepoContributionSnapshot buildRepoContributionSnapshot(
    const RepoContributionSnapshotInput &input);

QString repoContributionDependencyFingerprint(
    const RepoContributionSnapshotInput &input);

RepoContributionPreparation prepareRepoContributionSnapshot(
    const RepoContributionSnapshotInput &input,
    const QString &expectedDependencyFingerprint,
    bool cachedSnapshotAvailable);

bool repoContributionResponseNeedsRefresh(const QJsonObject &response,
                                          bool contributionSubmitted);

QByteArray profileContributionCanonical(
    const QString &owner, const QString &repo, const QString &updatedAt,
    const QByteArray &compactPayload);

QJsonObject signedContributionFields(
    const RepoContributionSnapshot &snapshot, const QString &owner,
    const QString &repo, const QString &updatedAt,
    const ForkMeshIdentity &identity);
