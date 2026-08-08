#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct ActionStep {
    QString name;
    QString run;
};

// One workflow parsed from a.forkmesh/*.yml|*.yaml file. The trust unit for
// the approval gate is `content` (the raw file text) — its SHA-256 is what the
// ActionStore records as approved.
struct ActionWorkflow {
    QString path;               // repo-relative path, e.g. ".forkmesh/deploy.yml"
    QString content;            // raw file text (hashed for approval, shown in diffs)
    QString name;               // display name (defaults to the file name)
    QStringList on;             // trigger events, e.g. {"push", "workflow_dispatch"}
    QMap<QString, QString> env; // workflow-level environment
    QStringList runsOn;
    QStringList needs;
    QList<ActionStep> steps;    // steps flattened across all jobs, in order
    bool valid = false;
    QString error;

    bool triggersOnPush() const { return on.contains(QStringLiteral("push")); }
    bool triggersOnRelease() const
    {
        return on.contains(QStringLiteral("release"));
    }
    bool allowsManualRun() const
    {
        return on.contains(QStringLiteral("workflow_dispatch"));
    }
    bool runsOnNode(const QStringList &nodeLabels) const;
};

class ActionFile
{
public:
    static ActionWorkflow parse(const QString &relPath, const QString &content);

    static QList<ActionWorkflow> parseWorkflowsInDir(const QString &checkoutDir);

    static QStringList nodeLabels(const QString &machineNode,
                                  const QString &mirrorNode,
                                  const QString &configured);

    static QStringList parseLabelList(const QString &configured);

    static QString pinnedNode(const QStringList &pins, const QString &path);
    static QStringList setPinnedNode(const QStringList &pins,
                                     const QStringList &paths,
                                     const QString &node);

    static QString substitute(const QString &input,
                              const QMap<QString, QString> &vars);
};
