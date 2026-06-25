#pragma once

#include "PullStore.h"

#include <QHash>
#include <QList>
#include <QString>

struct PullReviewFileSummary {
    QString path;
    int totalThreads = 0;
    int unresolvedThreads = 0;
    int resolvedThreads = 0;
    int suggestions = 0;
};

struct PullReviewThread {
    QString id;
    QString path;
    QString side;
    int lineStart = 0;
    int lineEnd = 0;
    bool resolved = false;
    bool hasSuggestion = false;
    QString suggestionState;
    QString appliedCommit;
    QList<PullEvent> events;
};

struct PullReviewSnapshot {
    QString reviewSummary;
    int topLevelItems = 0;
    int totalThreads = 0;
    int unresolvedThreads = 0;
    int resolvedThreads = 0;
    int suggestions = 0;
    QList<PullReviewThread> threads;
    QHash<QString, PullReviewFileSummary> files;
};

PullReviewSnapshot buildPullReviewSnapshot(const PullRequest &pr);
