#pragma once

#include "ActionStore.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace forkmesh::mirror_actions {

inline constexpr qsizetype kMaximumSummaryRuns = 20;
inline constexpr qsizetype kMaximumSummaryLogTailBytes = 16 * 1024;
inline constexpr qint64 kSummaryLeaseMilliseconds = 10 * 60 * 1000;
inline constexpr auto kSystemSummaryPath =
    "/var/lib/forkmesh-mirror/gateway/actions-summary.json";
inline constexpr auto kSystemStatePath =
    "/var/lib/forkmesh-mirror/gateway/actions-state.json";

namespace detail {

enum class SummaryWritePolicy {
    Reject,
    SameAccount,
    RootGatewayHandoff,
};




SummaryWritePolicy classifySummaryWritePolicy(
    const QString &path,
    quint64 effectiveUserId,
    quint64 parentUserId,
    quint64 parentGroupId,
    quint32 parentMode,
    bool parentIsProtectedRealDirectory,
    bool targetExists,
    bool targetIsRegular,
    bool targetIsSymbolicLink,
    quint64 targetUserId,
    quint64 targetGroupId,
    quint32 targetMode,
    quint64 targetLinkCount);

}





QJsonObject buildSummary(const QString &node,
                         const QList<ActionRun> &runs,
                         const ActionStore &store,
                         qint64 nowMs);





bool writeSummaryFile(const QString &path,
                      const QString &node,
                      const QList<ActionRun> &runs,
                      const ActionStore &store,
                      qint64 nowMs);



bool writeStateFile(const QString &path,
                    const QString &node,
                    const QString &state,
                    qint64 nowMs);

}
