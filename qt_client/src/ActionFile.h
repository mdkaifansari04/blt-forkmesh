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
    // Node labels this workflow is dedicated to (`runs-on:`, at the top level or
    // on any job). Empty means "any node that sees the push" — the historic
    // behaviour. Otherwise only a node carrying one of these labels executes it,
    // so a mesh can pin tests to one machine, Cloudflare deploys to a mirror,
    // and an iOS build to a Mac.
    QStringList runsOn;
    // Workflows that must already have succeeded for the same commit before this
    // one may start (`needs:`, at the top level or on any job). An entry names
    // another workflow by display name, repo-relative path, or file name;
    // entries naming a job inside this same file are ignored, because our jobs
    // are flattened into a single ordered step list. Lets a deploy trust the CI
    // run instead of repeating its test suite.
    QStringList needs;
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
    // True when this node may execute the workflow. An undedicated workflow runs
    // anywhere; a dedicated one needs one of its `runs-on` labels among this
    // node's labels (case-insensitive). The reserved label "any" matches every
    // node, so `runs-on: any` is an explicit way to say "wherever".
    bool runsOnNode(const QStringList &nodeLabels) const;
};

// Parses a deliberately small YAML subset — enough for the workflow schema:
//   name: <scalar>
//   on: push | release | [release, workflow_dispatch] | block list
//       # release = fires when a release tag is drafted; workflow_dispatch = manual
//   runs-on: mac1 | [mirror2, linux] | block list   # optional node dedication
//   needs: "CI tests" | [ci, deploy.yml] | block list  # optional run ordering
//   env: { KEY: value, ... }
//   jobs: { <job>: { runs-on: ..., steps: [ { name, run }, ... ] } }
//   steps: [ ... ]          # flattened top-level form is also accepted
// Supports plain/quoted scalars, block and flow sequences, nested maps, and
// literal block scalars (`run: |`). Full-line `#` comments are ignored.
class ActionFile
{
public:
    static ActionWorkflow parse(const QString &relPath, const QString &content);

    // Find and parse every .forkmesh/*.yml|*.yaml under checkoutDir.
    static QList<ActionWorkflow> parseWorkflowsInDir(const QString &checkoutDir);

    // The labels that identify THIS node to `runs-on:`. Always includes the
    // machine's node name, the mirror-executor node name on a headless node, and
    // the platform ("linux"/"macos"/"windows"), plus any operator-configured
    // extras (a free-form comma/space/newline separated list). Deduplicated and
    // lower-cased, because label matching is case-insensitive.
    static QStringList nodeLabels(const QString &machineNode,
                                  const QString &mirrorNode,
                                  const QString &configured);

    // Normalize one free-form label list into distinct lower-cased labels.
    static QStringList parseLabelList(const QString &configured);

    // The node pins the Actions tab's "Run on" dropdown stores on a repository,
    // as "<workflow path>\t<node label>" entries. A pin overrides the workflow's
    // own `runs-on:` on the node that set it.
    // pinnedNode: the label pinned to `path`, or empty when none is.
    static QString pinnedNode(const QStringList &pins, const QString &path);
    // setPinnedNode: `pins` with every path in `paths` pinned to `node`, or the
    // pin dropped when `node` is empty. Entries are sorted by path.
    static QStringList setPinnedNode(const QStringList &pins,
                                     const QStringList &paths,
                                     const QString &node);

    // Substitute variable references with values from vars. The explicit
    // ${{ vars.NAME }} context form is always expanded (unknown names become
    // empty strings, matching CI conventions). The bare ${NAME} form is only
    // expanded for declared variables; unknown ${NAME} is left untouched so a
    // workflow's own shell parameter expansions survive into the shell.
    static QString substitute(const QString &input,
                              const QMap<QString, QString> &vars);
};
