#pragma once

// Query matching for the Ctrl+K search overlay (MainWindowSearch.cpp), kept in
// its own GUI-free translation unit so the rules that decide what a search
// finds — and the parsers for the git output those rules run over — are
// unit-tested (tests/test_crypto.cpp) instead of only reachable through a live
// MainWindow.
//
// Everything here is pure: no Qt widgets, no processes, no disk. The overlay's
// worker threads call these; nothing else in the app does.

#include "DiscussionStore.h"
#include "IssueStore.h"
#include "ProjectStore.h"
#include "PullStore.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace forkmesh::search {

// Display order, icon and tint for every kind of hit the overlay can produce.
// The list is grouped in exactly this order, so the cheap identity matches
// (usually the thing you meant) sit above the long content lists.
struct Category {
    const char *kind;  // the GlobalSearchHit::kind this describes
    const char *title; // group header text
    const char *icon;  // octicon name
    const char *color; // icon tint
};

int categoryCount();
const Category &categoryAt(int index);
// Index of `kind` in display order. An unknown kind sorts last rather than
// vanishing from the list.
int categoryIndexOf(const QString &kind);

// Case-insensitive "does this field mention the query", with empty haystacks
// reading as no match.
bool containsFold(const QString &haystack, const QString &needle);

// A short single-line snippet of `haystack` around the first match of `needle`,
// so a body/comment hit shows where it landed instead of just "matched".
QString snippetAround(const QString &haystack, const QString &needle);

// Title-then-body match for the record kinds with no comment log. *where gets a
// short note naming the field that matched.
bool matchTitleBody(const QString &query, const QString &title,
                    const QString &body, QString *where);

// Search a signed record's title/body/comment log. Returns true and fills
// *where with a context note the first time the query is found.
bool matchIssue(const Issue &issue, const QString &query, QString *where);
bool matchPull(const PullRequest &pull, const QString &query, QString *where);
bool matchDiscussion(const Discussion &discussion, const QString &query,
                     QString *where);
bool matchProject(const Project &project, const QString &query, QString *where);

// One record of `git worktree list --porcelain`.
struct WorktreeRecord {
    QString path;
    QString branch; // short name; empty when detached or bare
    QString head;   // commit the worktree is parked on
    bool detached = false;
};
// Parse porcelain output into records. Blank separator lines are optional: a
// new "worktree " line always starts the next record.
QVector<WorktreeRecord> parseWorktreePorcelain(const QStringList &lines);

// One row of `git grep -n -I <rev>` output, which git prefixes with "<rev>:"
// when grepping a tree rather than the working copy.
struct CodeRow {
    QString path;
    int line = 0;
    QString text;
    bool valid = false;
};
CodeRow parseGrepRow(const QString &row, const QString &ref);

} // namespace forkmesh::search
