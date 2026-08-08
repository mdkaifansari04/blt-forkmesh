#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

#include <functional>
#include <optional>

namespace RepoContributionSnapshotInternal {

inline constexpr int kMaxPayloadEncoded = 64 * 1024;
inline constexpr int kMaxDayRows = 2048;
inline constexpr int kMaxExtensions = 256;
inline constexpr int kMaxCount = 1000000;
inline constexpr int kMaxBranchLength = 120;
inline constexpr int kMaxHeadLength = 64;

struct DayCounts {
    int commits = 0;
    int issues = 0;
    int pulls = 0;
    int reviews = 0;
};

struct ExtensionCounts {
    int bytes = 0;
    int files = 0;
};

struct RemovedCoverage {
    bool commits = false;
    bool collaboration = false;
};

struct LanguageInspectionLimits {
    qsizetype maxBlobs = 65536;
    qint64 maxBlobBytes = 48 * 1024 * 1024;
};

class ScopedLanguageInspectionLimitsForTests
{
public:
    explicit ScopedLanguageInspectionLimitsForTests(
        const LanguageInspectionLimits &limits);
    ~ScopedLanguageInspectionLimitsForTests();

    ScopedLanguageInspectionLimitsForTests(
        const ScopedLanguageInspectionLimitsForTests &) = delete;
    ScopedLanguageInspectionLimitsForTests &operator=(
        const ScopedLanguageInspectionLimitsForTests &) = delete;

private:
    std::optional<LanguageInspectionLimits> m_previous;
};

using DayMap = QMap<QString, DayCounts>;
using ExtensionMap = QMap<QString, ExtensionCounts>;

QString dayActorKey(const QString &date, const QString &actor);
void splitDayActorKey(const QString &key, QString *date, QString *actor);
bool incrementBounded(int *value);
RemovedCoverage removeOldestDate(DayMap *days);
RemovedCoverage trimDayLimit(DayMap *days);
bool removeSmallestExtension(ExtensionMap *extensions);
bool trimExtensionLimit(ExtensionMap *extensions);
int encodedSize(const QByteArray &compact);
bool encodedPayloadFits(const QByteArray &compact);
bool appendBounded(QByteArray *destination, const QByteArray &chunk,
                   qsizetype maxBytes);
void setBeforePullMetadataRecheckHookForTests(std::function<void()> hook);

} // namespace RepoContributionSnapshotInternal
