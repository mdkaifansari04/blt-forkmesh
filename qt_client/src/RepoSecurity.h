#pragma once

#include "ActionFile.h"
#include "ActionStore.h"
#include "IssueStore.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

enum class RepoSecuritySeverity {
    Pass = 0,
    Info,
    Warning,
    High,
    Critical,
};

struct RepoSecuritySignal {
    QString key;
    QString title;
    RepoSecuritySeverity severity = RepoSecuritySeverity::Info;
    QString summary;
    QString detail;
    QString actionLabel;
    QString actionTarget;
    // Optional itemised list rendered beneath the summary. Each entry is a
    // repo-relative path the UI turns into a "check" link (e.g. dependency
    // manifests). Empty for signals that only need summary/detail text.
    QStringList items;
};

struct RepoSecurityFinding {
    QString id;
    QString category;
    RepoSecuritySeverity severity = RepoSecuritySeverity::Info;
    QString title;
    QString detail;
    QString path;
    int line = 0;
    QString recommendedAction;
};

struct RepoSecurityInput {
    QString owner;
    QString name;
    QString localPath;
    QString mirrorPath;
    QString ref;
    bool publishToNetwork = false;
    bool isPrivate = false;
    bool previewOnly = false;
    bool actionsEnabled = true;
    bool integrityWarning = false;
    QList<Issue> issues;
    QList<ActionWorkflow> workflows;
    QList<ActionRun> actionRuns;
};

struct RepoSecuritySnapshot {
    QString repoKey;
    QString ref;
    qint64 generatedAtMs = 0;
    QList<RepoSecuritySignal> signalList;
    QList<RepoSecurityFinding> findings;
    // Number of dependencies declared in each dependency manifest (keyed by
    // repo-relative path), shown as a "Dependencies" column on the
    // dependency-scan card. Best-effort per manifest format, not a full parse.
    QHash<QString, int> dependencyCounts;
};

// Result of rescanning a single dependency manifest (the dependency card's
// per-row "Run scan" button), as opposed to RepoSecurity::scan()'s full
// repo-wide pass. Kept separate so a single-manifest rescan stays cheap: it
// reads only that one file instead of every tracked file in the repo.
struct RepoSecurityManifestScan {
    int dependencyCount = 0;
    QList<RepoSecurityFinding> findings;
};

class RepoSecurity
{
public:
    static RepoSecuritySnapshot scan(const RepoSecurityInput &input);
    // Rescans a single dependency manifest (used by the dependency card's
    // per-row "Run scan" button) instead of every tracked file.
    static RepoSecurityManifestScan scanManifest(const RepoSecurityInput &input,
                                                 const QString &manifestPath);
    static QString severityText(RepoSecuritySeverity severity);
    static RepoSecuritySeverity highestSeverity(const RepoSecuritySnapshot &snapshot);
    // Scan commits about to be pushed for secrets.  upstreamRef is the remote
    // tracking ref (e.g. "refs/remotes/origin/main"); if empty, all tracked
    // text files in localPath are scanned instead.
    // To intentionally include a fake/example credential (e.g. a test
    // fixture), append a comment containing "forkmesh-secret-scan:ignore-line"
    // to that line; matches on that line are skipped.
    static QList<RepoSecurityFinding> findSecretsInPush(const QString &localPath,
                                                        const QString &upstreamRef);
};

// Local quality evidence for the Quality tab: check-run health, code volume,
// documentation/tests presence, TODO markers, file-size health, commit
// activity and issue hygiene. Shares the signal/finding shapes (and the
// snapshot container) with RepoSecurity so both tabs render the same way.
class RepoQuality
{
public:
    static RepoSecuritySnapshot scan(const RepoSecurityInput &input);
};
