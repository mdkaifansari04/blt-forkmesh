#include "ActionFile.h"
#include "ActionRunner.h"
#include "ActionStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *message)
{
    if (condition) {
        std::printf("  ok  %s\n", message);
    } else {
        std::fprintf(stderr, "  FAIL  %s\n", message);
        ++failures;
    }
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(data) == data.size();
}

QByteArray git(const QString &repository, const QStringList &arguments,
               bool *ok = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral("git"),
                  QStringList{QStringLiteral("-C"), repository} + arguments);
    const bool success =
        process.waitForStarted(3000) && process.waitForFinished(30000) &&
        process.exitStatus() == QProcess::NormalExit &&
        process.exitCode() == 0;
    if (ok)
        *ok = success;
    return process.readAllStandardOutput();
}

QString commitAll(const QString &repository, const QString &message)
{
    bool ok = false;
    git(repository,
        {QStringLiteral("add"), QStringLiteral("--all")}, &ok);
    if (!ok)
        return {};
    git(repository,
        {QStringLiteral("-c"),
         QStringLiteral("user.name=Action Security Test"),
         QStringLiteral("-c"),
         QStringLiteral("user.email=actions-test@forkmesh.local"),
         QStringLiteral("commit"), QStringLiteral("-m"), message},
        &ok);
    if (!ok)
        return {};
    return QString::fromLatin1(
               git(repository,
                   {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                   &ok))
        .trimmed();
}

struct RunResult {
    bool signalOk = false;
    bool finished = false;
    ActionRun persisted;
    QString log;
};

RunResult runWorkflow(const QString &repository, const QString &commit,
                      const QString &content, ActionStore *store,
                      int runId, const ActionSandboxLimits &limits)
{
    ActionRun run;
    run.id = runId;
    run.owner = QStringLiteral("security");
    run.name = QStringLiteral("sandbox");
    run.workflowPath = QStringLiteral(".forkmesh/security.yml");
    run.workflowName = QStringLiteral("Security test");
    run.workflowContent = content;
    run.commit = commit;
    run.ref = QStringLiteral("refs/heads/main");
    QString digestError;
    check(ActionStore::repositoryStateDigest(
              repository, commit, &run.repositoryTree,
              &run.executionDigest, &digestError),
          "test workflow repository snapshot can be hashed");

    const ActionWorkflow workflow =
        ActionFile::parse(run.workflowPath, content);
    check(workflow.valid, "test workflow parses");

    ActionRunner runner(store);
    runner.setSandboxLimitsForTesting(limits);
    QEventLoop loop;
    RunResult result;
    QObject::connect(
        &runner, &ActionRunner::finished, &loop,
        [&](int id, bool ok) {
            if (id == runId) {
                result.finished = true;
                result.signalOk = ok;
                loop.quit();
            }
        });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&] {
        runner.stop();
        loop.quit();
    });
    guard.start(60000);
    runner.start(run, workflow, repository, QString(),
                 QMap<QString, QString>{});
    if (!result.finished)
        loop.exec();
    guard.stop();
    const QList<ActionRun> runs = store->loadAllRuns();
    for (const ActionRun &saved : runs)
        if (saved.id == runId)
            result.persisted = saved;
    result.log = store->readLog(run);
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForkMeshTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("ActionSecurity-%1")
            .arg(QCoreApplication::applicationPid()));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QTemporaryDir temp;
    check(temp.isValid(), "temporary test directory is available");
    if (!temp.isValid())
        return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       temp.filePath(QStringLiteral("settings")));
    QSettings().clear();

    // Pre-v2 storage replaced every non [A-Za-z0-9.-] character with "_".
    // These two distinct repositories therefore shared both run and approval
    // storage. The v2 key must split their records without copying that
    // ambiguous trust decision to either repository.
    ActionRun collisionA;
    collisionA.id = 11;
    collisionA.owner = QStringLiteral("team/a");
    collisionA.name = QStringLiteral("repo");
    collisionA.workflowPath = QStringLiteral(".forkmesh/collision.yml");
    collisionA.workflowContent = QStringLiteral("name: collision");
    collisionA.repositoryTree = QStringLiteral("tree-a");
    collisionA.executionDigest = QStringLiteral("digest-a");
    ActionRun collisionB = collisionA;
    collisionB.id = 12;
    collisionB.owner = QStringLiteral("team_a");
    ActionRun legacySolo = collisionA;
    legacySolo.id = 13;
    legacySolo.owner = QStringLiteral("legacy/team");
    legacySolo.name = QStringLiteral("solo");
    legacySolo.workflowPath = QStringLiteral(".forkmesh/solo.yml");
    legacySolo.workflowContent = QStringLiteral("name: solo");
    legacySolo.repositoryTree = QStringLiteral("tree-solo");
    legacySolo.executionDigest = QStringLiteral("digest-solo");

    check(collisionA.legacyRepoKey() == collisionB.legacyRepoKey(),
          "legacy repository key fixture reproduces the lossy collision");
    check(collisionA.repoKey() != collisionB.repoKey() &&
              QRegularExpression(QStringLiteral("^v2-[a-f0-9]{64}$"))
                  .match(collisionA.repoKey())
                  .hasMatch(),
          "length-delimited SHA-256 repository keys are path safe and collision resistant");

    const QString actionsRoot = temp.filePath(QStringLiteral("actions"));
    auto writeLegacyRun = [&](const ActionRun &run, const QByteArray &log) {
        const QString directory =
            actionsRoot + QStringLiteral("/runs/") + run.legacyRepoKey() +
            QLatin1Char('/') + QString::number(run.id);
        return QDir().mkpath(directory) &&
               writeFile(directory + QStringLiteral("/meta.json"),
                         QJsonDocument(run.toJson())
                             .toJson(QJsonDocument::Compact)) &&
               writeFile(directory + QStringLiteral("/log.txt"), log);
    };
    check(writeLegacyRun(collisionA, QByteArray("collision A log\n")) &&
              writeLegacyRun(collisionB, QByteArray("collision B log\n")) &&
              writeLegacyRun(legacySolo, QByteArray("solo log\n")),
          "legacy run fixtures are persisted under lossy keys");
    const QString legacyArtifact =
        actionsRoot + QStringLiteral("/artifacts/") +
        legacySolo.legacyRepoKey() + QStringLiteral("/13");
    check(QDir().mkpath(legacyArtifact) &&
              writeFile(legacyArtifact + QStringLiteral("/result.txt"),
                        QByteArray("artifact\n")),
          "legacy artifact fixture is persisted");

    auto approvalDocument = [](const ActionRun &run) {
        QJsonObject approvals;
        approvals.insert(
            run.workflowPath,
            QJsonObject{
                {QStringLiteral("schema"), 2},
                {QStringLiteral("workflowContent"), run.workflowContent},
                {QStringLiteral("repositoryTree"), run.repositoryTree},
                {QStringLiteral("executionDigest"), run.executionDigest},
            });
        return QJsonDocument(approvals).toJson(QJsonDocument::Compact);
    };
    QSettings migrationSettings;
    migrationSettings.setValue(
        QStringLiteral("actions/approved/") + collisionA.legacyRepoKey(),
        approvalDocument(collisionA));
    migrationSettings.setValue(
        QStringLiteral("actions/approved/") + legacySolo.legacyRepoKey(),
        approvalDocument(legacySolo));
    migrationSettings.sync();

    ActionStore migrationStore(actionsRoot);
    check(QFileInfo::exists(
              actionsRoot + QStringLiteral("/runs/") +
              collisionA.repoKey() + QStringLiteral("/11/meta.json")) &&
              QFileInfo::exists(
                  actionsRoot + QStringLiteral("/runs/") +
                  collisionB.repoKey() + QStringLiteral("/12/meta.json")) &&
              migrationStore.readLog(collisionA) ==
                  QStringLiteral("collision A log\n") &&
              migrationStore.readLog(collisionB) ==
                  QStringLiteral("collision B log\n"),
          "legacy colliding runs migrate into separate canonical directories");
    check(QFileInfo::exists(
              actionsRoot + QStringLiteral("/artifacts/") +
              legacySolo.repoKey() + QStringLiteral("/13/result.txt")),
          "legacy run artifacts migrate with their exact repository and run");
    check(!ActionStore::isApproved(
              collisionA.repoKey(), collisionA.workflowPath,
              collisionA.workflowContent, collisionA.repositoryTree,
              collisionA.executionDigest) &&
              !ActionStore::isApproved(
                  collisionB.repoKey(), collisionB.workflowPath,
                  collisionB.workflowContent, collisionB.repositoryTree,
                  collisionB.executionDigest),
          "ambiguous legacy approval collision fails closed for both repositories");
    check(!ActionStore::isApproved(
              legacySolo.repoKey(), legacySolo.workflowPath,
              legacySolo.workflowContent, legacySolo.repositoryTree,
              legacySolo.executionDigest) &&
              ActionStore::lastApprovedContent(
                  legacySolo.repoKey(), legacySolo.workflowPath) ==
                  legacySolo.workflowContent &&
              !QSettings().contains(
                  QStringLiteral("actions/approved/") +
                  legacySolo.legacyRepoKey()),
          "unambiguous legacy approval migrates as review-only content and fails closed");

    const QString repository = temp.filePath(QStringLiteral("repository"));
    QDir().mkpath(repository + QStringLiteral("/.forkmesh"));
    bool gitOk = false;
    git(repository, {QStringLiteral("init"), QStringLiteral("-q")}, &gitOk);
    check(gitOk, "test Git repository initializes");

    const QString workflowPath =
        repository + QStringLiteral("/.forkmesh/security.yml");
    const QString stableWorkflow =
        QStringLiteral("name: Bound approval\non: push\nsteps:\n"
                       "  - name: helper\n"
                       "    run: ./ci.sh\n");
    check(writeFile(workflowPath, stableWorkflow.toUtf8()),
          "workflow fixture is written");
    check(writeFile(repository + QStringLiteral("/ci.sh"),
                    QByteArray("#!/bin/sh\necho safe\n")),
          "helper fixture is written");
    QFile::setPermissions(repository + QStringLiteral("/ci.sh"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    const QString firstCommit =
        commitAll(repository, QStringLiteral("initial workflow"));
    check(!firstCommit.isEmpty(), "initial fixture commit is created");

    QString firstTree;
    QString firstDigest;
    QString digestError;
    check(ActionStore::repositoryStateDigest(
              repository, firstCommit, &firstTree, &firstDigest,
              &digestError),
          "initial repository execution state is hashed");
    const QString approvalScope = QStringLiteral("security-sandbox");
    ActionStore::approve(approvalScope, QStringLiteral(".forkmesh/security.yml"),
                         stableWorkflow, firstTree, firstDigest);
    check(ActionStore::isApproved(
              approvalScope, QStringLiteral(".forkmesh/security.yml"),
              stableWorkflow, firstTree, firstDigest),
          "exact workflow and repository state is approved");

    check(writeFile(repository + QStringLiteral("/ci.sh"),
                    QByteArray("#!/bin/sh\necho changed\n")),
          "helper changes without changing workflow YAML");
    QFile::setPermissions(repository + QStringLiteral("/ci.sh"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);
    const QString secondCommit =
        commitAll(repository, QStringLiteral("change helper only"));
    QString secondTree;
    QString secondDigest;
    check(ActionStore::repositoryStateDigest(
              repository, secondCommit, &secondTree, &secondDigest,
              &digestError),
          "changed repository execution state is hashed");
    check(firstDigest != secondDigest && firstTree != secondTree,
          "changing only ci.sh changes the bound execution state");
    check(!ActionStore::isApproved(
              approvalScope, QStringLiteral(".forkmesh/security.yml"),
              stableWorkflow, secondTree, secondDigest),
          "changed ci.sh cannot reuse workflow approval or receive secrets");

    QString sandboxReason;
    if (!ActionRunner::sandboxAvailable(&sandboxReason)) {
        std::printf("  SKIP sandbox integration: %s\n",
                    sandboxReason.toUtf8().constData());
    } else {
        const QString hostSecret =
            temp.filePath(QStringLiteral("host-secret.txt"));
        check(writeFile(hostSecret, QByteArray("must-not-be-readable")),
              "host secret fixture is written");
        qputenv("FORKMESH_HOST_ONLY_SECRET", "must-not-be-inherited");

        const QString hostileWorkflow = QStringLiteral(
            "name: Hostile boundary checks\non: push\nsteps:\n"
            "  - name: escape egress secret and process limits\n"
            "    run: |\n"
            "      set -eu\n"
            "      [ \"$(id -u)\" = \"65534\" ]\n"
            "      [ ! -e '%1' ]\n"
            "      [ ! -e /root ]\n"
            "      [ -z \"${FORKMESH_HOST_ONLY_SECRET:-}\" ]\n"
            "      if touch /source/escape 2>/dev/null; then exit 41; fi\n"
            "      printf isolated > /workspace/sandbox-marker\n"
            "      python3 - <<'PY'\n"
            "      import os\n"
            "      import socket\n"
            "      import time\n"
            "      children = []\n"
            "      for _ in range(64):\n"
            "          try:\n"
            "              pid = os.fork()\n"
            "          except OSError:\n"
            "              break\n"
            "          if pid == 0:\n"
            "              time.sleep(0.2)\n"
            "              os._exit(0)\n"
            "          children.append(pid)\n"
            "      assert len(children) < 64\n"
            "      for pid in children:\n"
            "          os.waitpid(pid, 0)\n"
            "      s = socket.socket()\n"
            "      s.settimeout(0.3)\n"
            "      assert s.connect_ex(('1.1.1.1', 53)) != 0\n"
            "      PY\n")
                                            .arg(hostSecret);
        check(writeFile(workflowPath, hostileWorkflow.toUtf8()),
              "hostile workflow fixture is written");
        const QString hostileCommit =
            commitAll(repository, QStringLiteral("hostile boundary workflow"));
        ActionStore store(temp.filePath(QStringLiteral("actions")));
        ActionSandboxLimits limits;
        limits.maxMemoryBytes = 512LL * 1024 * 1024;
        limits.maxFileBytes = 8LL * 1024 * 1024;
        limits.maxWorkspaceBytes = 128LL * 1024 * 1024;
        limits.maxProcesses = 32;
        limits.maxOpenFiles = 64;
        limits.maxCpuSeconds = 30;
        limits.stepTimeoutMs = 15000;
        limits.jobTimeoutMs = 45000;
        const RunResult hostile =
            runWorkflow(repository, hostileCommit, hostileWorkflow,
                        &store, 101, limits);
        check(hostile.finished && hostile.signalOk &&
                  hostile.persisted.status == ActionStatus::Success,
              "hostile escape, egress, fork ceiling, and secret-read checks "
              "remain contained");
        check(!QFile::exists(repository + QStringLiteral("/escape")),
              "read-only source mount was not modified");

        const QString timeoutWorkflow = QStringLiteral(
            "name: Timeout\non: push\nsteps:\n"
            "  - name: never finishes\n"
            "    run: sleep 30\n");
        check(writeFile(workflowPath, timeoutWorkflow.toUtf8()),
              "timeout workflow fixture is written");
        const QString timeoutCommit =
            commitAll(repository, QStringLiteral("timeout workflow"));
        ActionSandboxLimits timeoutLimits = limits;
        timeoutLimits.stepTimeoutMs = 350;
        const RunResult timed =
            runWorkflow(repository, timeoutCommit, timeoutWorkflow,
                        &store, 102, timeoutLimits);
        check(timed.finished && !timed.signalOk &&
                  timed.persisted.status == ActionStatus::Failed &&
                  timed.log.contains(QStringLiteral("time limit")),
              "stale workflow processes are killed at the configured deadline");

        const QString diskWorkflow = QStringLiteral(
            "name: Disk quota\non: push\nsteps:\n"
            "  - name: oversized file\n"
            "    run: dd if=/dev/zero of=too-large bs=1M count=4 status=none\n");
        check(writeFile(workflowPath, diskWorkflow.toUtf8()),
              "disk-limit workflow fixture is written");
        const QString diskCommit =
            commitAll(repository, QStringLiteral("disk workflow"));
        ActionSandboxLimits diskLimits = limits;
        diskLimits.maxFileBytes = 1024 * 1024;
        const RunResult disk =
            runWorkflow(repository, diskCommit, diskWorkflow,
                        &store, 103, diskLimits);
        check(disk.finished && !disk.signalOk &&
                  disk.persisted.status == ActionStatus::Failed,
              "oversized workflow files are stopped by RLIMIT_FSIZE");

        const QString outputWorkflow = QStringLiteral(
            "name: Output quota\non: push\nsteps:\n"
            "  - name: noisy\n"
            "    run: python3 -c \"import sys; sys.stdout.write('x' * 2097152)\"\n");
        check(writeFile(workflowPath, outputWorkflow.toUtf8()),
              "output-limit workflow fixture is written");
        const QString outputCommit =
            commitAll(repository, QStringLiteral("output workflow"));
        const RunResult noisy =
            runWorkflow(repository, outputCommit, outputWorkflow,
                        &store, 104, limits);
        check(noisy.finished && noisy.signalOk &&
                  noisy.persisted.status == ActionStatus::Success,
              "large output does not wedge the isolated runner");
        check(noisy.log.toUtf8().size() < 1400 * 1024 &&
                  noisy.log.contains(
                      QStringLiteral("Action output exceeded")),
              "workflow output is bounded and reports truncation");
    }

    if (failures == 0)
        std::printf("\nAll Action security tests passed.\n");
    else
        std::fprintf(stderr, "\n%d Action security test(s) failed.\n",
                     failures);
    return failures == 0 ? 0 : 1;
}
