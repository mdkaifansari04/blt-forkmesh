#pragma once

#include "ActionFile.h"
#include "ActionStore.h"
#include "IssueStore.h"

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
};

class RepoSecurity
{
public:
    static RepoSecuritySnapshot scan(const RepoSecurityInput &input);
    static QString severityText(RepoSecuritySeverity severity);
    static RepoSecuritySeverity highestSeverity(const RepoSecuritySnapshot &snapshot);
};
