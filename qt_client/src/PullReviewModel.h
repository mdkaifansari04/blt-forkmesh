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

// Machine-authorship provenance for a PR, read from the `ForkMesh-Agent:
// <tool>/<model>` commit trailer that AgentRunner stamps on agent commits
// (issue #365). The trailer travels inside the signed commit series (commits.mbox
// is folded into the pull signature), so this attribution is verifiable and
// cross-node — no local AgentSession required.
struct PullAgentProvenance {
    bool isAgent = false;
    QString tool;   // e.g. "claude-code"
    QString model;  // e.g. "claude-opus-4-8"
    QString value;  // raw trailer value "<tool>/<model>"
};

// Fold the ForkMesh-Agent trailer out of a PR's commit series. Returns
// isAgent=false when no commit carries the trailer (a human-authored PR).
PullAgentProvenance pullAgentProvenance(const PullRequest &pr);

// Per-file authorship, keyed by changed-file path: true when at least one
// agent-stamped commit touched the file. Lets the Files-changed tab filter by
// authorship in a PR that mixes agent and human commits.
QHash<QString, bool> pullFileAuthorship(const PullRequest &pr);

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
