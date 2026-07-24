#pragma once

#include "ActionStore.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace forkmesh::mirror_actions {

inline constexpr qsizetype kMaximumSummaryRuns = 20;
inline constexpr qsizetype kMaximumSummaryLogTailBytes = 16 * 1024;
inline constexpr qint64 kSummaryLeaseMilliseconds = 10 * 60 * 1000;

// Build the exact secret-free document exposed by an actions-status gateway.
// ActionStore logs are already variable-redacted by ActionRunner. This function
// takes only a bounded UTF-8 tail and never serializes workflow content, command
// text, variable data, workflow paths, or local filesystem paths.
QJsonObject buildSummary(const QString &node,
                         const QList<ActionRun> &runs,
                         const ActionStore &store,
                         qint64 nowMs);

// Atomically replace one actions-summary.json in an owner-only real directory.
// The resulting regular file is mode 0600 (or the platform equivalent).
bool writeSummaryFile(const QString &path,
                      const QString &node,
                      const QList<ActionRun> &runs,
                      const ActionStore &store,
                      qint64 nowMs);

} // namespace forkmesh::mirror_actions
