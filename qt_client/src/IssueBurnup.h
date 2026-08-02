#pragma once

#include "IssueStore.h"

#include <QList>

struct IssueBurnupPoint {
    qint64 timestampMs = 0;
    int openCount = 0;
    int closedCount = 0;
};

// Reconstruct the number of open and closed issues at evenly spaced points in
// a time range. Status events are folded in timestamp order so closing and
// reopening an issue are both represented in the chart.
QList<IssueBurnupPoint> buildIssueBurnupSeries(const QList<Issue> &issues,
                                               qint64 startMs, qint64 endMs,
                                               int intervals);

// Earliest known issue creation/open timestamp, or fallbackMs for an empty
// collection. Used by the chart's All time range.
qint64 firstIssueHistoryTimestamp(const QList<Issue> &issues, qint64 fallbackMs);
