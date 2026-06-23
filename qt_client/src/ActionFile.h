#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// A single step in a workflow: a shell command (one or more lines) with an
// optional display name. Mirrors a GitHub Actions "step" with a `run:` block.
struct ActionStep {
    QString name;
    QString run;
};

// One workflow parsed from a .forkmesh/*.yml|*.yaml file. The trust unit for
// the approval gate is `content` (the raw file text) — its SHA-256 is what the
// ActionStore records as approved.
struct ActionWorkflow {
    QString path;               // repo-relative path, e.g. ".forkmesh/deploy.yml"
    QString content;            // raw file text (hashed for approval, shown in diffs)
    QString name;               // display name (defaults to the file name)
    QStringList on;             // trigger events, e.g. {"push", "workflow_dispatch"}
    QMap<QString, QString> env; // workflow-level environment
    QList<ActionStep> steps;    // steps flattened across all jobs, in order
    bool valid = false;
    QString error;

    bool triggersOnPush() const { return on.contains(QStringLiteral("push")); }
    // Opts the workflow into manual ("workflow_dispatch") runs the user can
    // trigger by hand from the Actions tab, optionally against a chosen branch.
    bool allowsManualRun() const
    {
        return on.contains(QStringLiteral("workflow_dispatch"));
    }
};

// Parses a deliberately small YAML subset — enough for the workflow schema:
//   name: <scalar>
//   on: push | [push, workflow_dispatch] | block list   # workflow_dispatch = manual
//   env: { KEY: value, ... }
//   jobs: { <job>: { steps: [ { name, run }, ... ] } }
//   steps: [ ... ]          # flattened top-level form is also accepted
// Supports plain/quoted scalars, block and flow sequences, nested maps, and
// literal block scalars (`run: |`). Full-line `#` comments are ignored.
class ActionFile
{
public:
    static ActionWorkflow parse(const QString &relPath, const QString &content);

    // Find and parse every .forkmesh/*.yml|*.yaml under checkoutDir.
    static QList<ActionWorkflow> parseWorkflowsInDir(const QString &checkoutDir);

    // Replace ${{ vars.NAME }} and ${NAME} with values from vars (unknown names
    // become empty strings, matching CI conventions).
    static QString substitute(const QString &input,
                              const QMap<QString, QString> &vars);
};
