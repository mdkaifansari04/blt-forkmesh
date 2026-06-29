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
    // Fires when a release is published (a tag is drafted from the Releases
    // panel). Release-only workflows publish build artifacts and shouldn't run
    // on every push.
    bool triggersOnRelease() const
    {
        return on.contains(QStringLiteral("release"));
    }
    // Opts the workflow into manual ("workflow_dispatch") runs the user can
    // trigger by hand from the Actions tab, optionally against a chosen branch.
    bool allowsManualRun() const
    {
        return on.contains(QStringLiteral("workflow_dispatch"));
    }
};

// Parses a deliberately small YAML subset — enough for the workflow schema:
//   name: <scalar>
//   on: push | release | [release, workflow_dispatch] | block list
//       # release = fires when a release tag is drafted; workflow_dispatch = manual
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

    // Substitute variable references with values from vars. The explicit
    // ${{ vars.NAME }} context form is always expanded (unknown names become
    // empty strings, matching CI conventions). The bare ${NAME} form is only
    // expanded for declared variables; unknown ${NAME} is left untouched so a
    // workflow's own shell parameter expansions survive into the shell.
    static QString substitute(const QString &input,
                              const QMap<QString, QString> &vars);
};
