#include "ActionRunner.h"

#include "CrashHandler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <functional>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr qsizetype kActionProcessLogChunkBytes = 16 * 1024;
constexpr qsizetype kActionProcessLogMaxBytes = 1024 * 1024;
constexpr qsizetype kActionProcessLogTailBytes = 128 * 1024;
constexpr qsizetype kActionProcessLogMaxLineChars = 4096;
constexpr qsizetype kActionCrashOutputTailBytes = 12 * 1024;
constexpr qsizetype kActionCrashContextMaxChars = 12000;

QString oneLine(QString text)
{
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return text.simplified();
}

QString diagnosticSafeText(const QString &input, qsizetype maxChars)
{
    QString text = input;
    QString prefix;
    if (text.size() > maxChars) {
        text = text.right(maxChars);
        prefix = QStringLiteral("...(diagnostic text truncated; showing tail)...\n");
    }

    QString out;
    out.reserve(text.size() + prefix.size());
    qsizetype col = 0;
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            out += QLatin1Char('\n');
            col = 0;
            continue;
        }
        if (ch == QLatin1Char('\t')) {
            out += ch;
            col += 4;
        } else if (u < 0x20 || (u >= 0x7f && u <= 0x9f)) {
            continue;
        } else {
            out += ch;
            ++col;
        }
        if (col >= kActionProcessLogMaxLineChars) {
            out += QStringLiteral("\n...[long line wrapped for display]...\n");
            col = 0;
        }
    }
    return prefix + out;
}

} // namespace

ActionRunner::ActionRunner(ActionStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

void ActionRunner::start(const ActionRun &run, const ActionWorkflow &workflow,
                         const QString &mirrorPath, const QString &workTreePath,
                         const QMap<QString, QString> &variables)
{
    m_busy = true;
    m_stopping = false;
    m_run = run;
    m_workflow = workflow;
    m_mirror = mirrorPath;
    m_repoWorkTree = workTreePath;
    m_variables = variables;
    m_stepIndex = 0;
    m_processOutputBytes = 0;
    m_processOutputSuppressedBytes = 0;
    m_processOutputTail.clear();
    m_processOutputLineChars = 0;
    m_crashOutputTail.clear();
    m_currentStepLabel = QStringLiteral("Checkout");
    m_currentCommand.clear();
    m_processOutputTruncated = false;
    forkmesh::setTerminationSignalSurvivalEnabled(true);

    m_secrets.clear();
    for (const QString &value : variables.values())
        if (!value.isEmpty())
            m_secrets.append(value);

    m_run.status = ActionStatus::Running;
    m_run.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    emit statusChanged(m_run.id, m_run.status);

    emitLog(QString::fromUtf8("==> \xF0\x9F\x9A\x80 Workflow: %1 (%2)") // 🚀
                .arg(m_workflow.name, m_workflow.path));
    emitLog(QString::fromUtf8("==> \xF0\x9F\x93\x8D Commit:   %1 on %2") // 📍
                .arg(m_run.commit.left(12), m_run.ref));
    emitLog(QString::fromUtf8(
        "==> \xF0\x9F\x8C\xBF Checking out into a temporary worktree\xE2\x80\xA6")); // 🌿 …

    m_worktree =
        QDir::tempPath() + QStringLiteral("/forkmesh-run-") +
        QString::number(m_run.id) + QStringLiteral("-") +
        QString::number(QDateTime::currentMSecsSinceEpoch());

    launch(Phase::Checkout, QStringLiteral("git"),
           {QStringLiteral("-C"), m_mirror, QStringLiteral("worktree"),
            QStringLiteral("add"), QStringLiteral("--detach"), m_worktree,
            m_run.commit},
           QString());
}

void ActionRunner::stop()
{
    if (!m_busy)
        return;
    m_stopping = true;
    emitLog(QString());
    emitLog(QString::fromUtf8("==> \xF0\x9F\x9B\x91 Stop requested by user.")); // 🛑

    if (m_process && m_process->state() != QProcess::NotRunning) {
#ifndef Q_OS_WIN
        // setsid() in launch() made the child its own process-group leader, so
        // its pid is the group id; the negative pid signals the whole group.
        const qint64 pid = m_process->processId();
        if (pid > 0)
            ::kill(static_cast<pid_t>(-pid), SIGTERM);
#endif
        m_process->terminate();
        if (!m_process->waitForFinished(1500)) {
#ifndef Q_OS_WIN
            const qint64 pid = m_process->processId();
            if (pid > 0)
                ::kill(static_cast<pid_t>(-pid), SIGKILL);
#endif
            m_process->kill();
        }
        return; // finished() → onProcessFinished → complete()
    }
    complete(false, QStringLiteral("Stopped."));
}

void ActionRunner::launch(Phase phase, const QString &program,
                          const QStringList &args, const QString &workingDir)
{
    m_phase = phase;
    m_processOutputBytes = 0;
    m_processOutputSuppressedBytes = 0;
    m_processOutputTail.clear();
    m_processOutputLineChars = 0;
    m_processOutputTruncated = false;
    if (m_currentStepLabel.isEmpty())
        m_currentStepLabel = phase == Phase::Checkout
                                 ? QStringLiteral("Checkout")
                                 : QStringLiteral("Step %1").arg(m_stepIndex + 1);
    m_currentCommand = program;
    for (const QString &arg : args)
        m_currentCommand += QLatin1Char(' ') + arg;
    updateCrashContext();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDir.isEmpty())
        m_process->setWorkingDirectory(workingDir);

#ifndef Q_OS_WIN
    // Put each child in its own process group so stop() can signal the whole
    // tree: a step's shell may fork cargo/wrangler/etc. that would otherwise
    // outlive a kill of just the shell.
    m_process->setChildProcessModifier([] { ::setsid(); });
#endif

    // Inherit the system environment, add the global variables (so wrangler sees
    // CLOUDFLARE_API_TOKEN), and make sure user-local tool dirs are on PATH so
    // uvx/cargo-installed tools resolve.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it)
        env.insert(it.key(), it.value());
    // Release runs check out a tag's commit detached, so the tag name isn't
    // recoverable from git inside the worktree. Surface it as FORKMESH_TAG (the
    // env var release workflows read to name the published artifact).
    if (m_run.ref.startsWith(QLatin1String("refs/tags/")))
        env.insert(QStringLiteral("FORKMESH_TAG"),
                   m_run.ref.mid(QStringLiteral("refs/tags/").size()));
    // The release workflow stages artifact bytes into a content-addressed store
    // (the CAS) and the serving node streams them back from <mirror>/
    // forkmesh-releases (RepoHost::streamReleaseBlob). Point FORKMESH_RELEASE_CAS
    // there so the bytes land where they're served — NOT in the throwaway
    // worktree's default .forkmesh/release-blobs, which cleanupWorktree() deletes
    // (which is why no prebuilt binary was ever downloadable).
    if (!m_mirror.isEmpty())
        env.insert(QStringLiteral("FORKMESH_RELEASE_CAS"),
                   QDir(m_mirror).absoluteFilePath(
                       QStringLiteral("forkmesh-releases")));
    // Release workflows publish artifacts and write release.json; expose the
    // owner/name there so the manifest records which repo owns the blobs. Do
    // not leak this into ordinary CI jobs: install.sh treats FORKMESH_REPO as an
    // explicit clone-source override, and worker installer tests must exercise
    // live mirror selection instead.
    if (isReleaseRun() && !m_run.owner.isEmpty() && !m_run.name.isEmpty())
        env.insert(QStringLiteral("FORKMESH_REPO"),
                   m_run.owner + QLatin1Char('/') + m_run.name);
    const QString home = QDir::homePath();
    const QString extraPath = home + QStringLiteral("/.local/bin:") + home +
                              QStringLiteral("/.cargo/bin");
    env.insert(QStringLiteral("PATH"),
               extraPath + QLatin1Char(':') +
                   env.value(QStringLiteral("PATH")));
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        emitProcessOutput(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) {
                onProcessFinished(exitCode);
            });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_process)
                    emitLog(QString::fromUtf8("!! \xE2\x9A\xA0\xEF\xB8\x8F  ") + // ⚠️
                            m_process->errorString());
            });

    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->start();
}

void ActionRunner::onProcessFinished(int exitCode)
{
    // Drain any tail output before tearing the process down.
    if (m_process) {
        const QByteArray tail = m_process->readAllStandardOutput();
        if (!tail.isEmpty())
            emitProcessOutput(tail);
        emitSuppressedProcessOutputTail();
        m_process->deleteLater();
        m_process = nullptr;
    }

    // A user-requested stop overrides whatever exit code the killed process
    // reported; record the run as Cancelled rather than Failed.
    if (m_stopping) {
        complete(false, QStringLiteral("Stopped by user."));
        return;
    }

    if (m_phase == Phase::Checkout) {
        if (exitCode != 0) {
            complete(false, QStringLiteral("Checkout failed."));
            return;
        }
        runNextStep();
        return;
    }

    // A step finished.
    if (exitCode != 0) {
        complete(false, QStringLiteral("Step failed with exit code %1.")
                            .arg(exitCode));
        return;
    }
    ++m_stepIndex;
    runNextStep();
}

void ActionRunner::runNextStep()
{
    if (m_stepIndex >= m_workflow.steps.size()) {
        complete(true, QStringLiteral("All steps completed."));
        return;
    }

    const ActionStep &step = m_workflow.steps.at(m_stepIndex);
    const QString command = ActionFile::substitute(step.run, m_variables);
    const QString label =
        step.name.isEmpty() ? QStringLiteral("Step %1").arg(m_stepIndex + 1)
                            : step.name;
    m_currentStepLabel = label;
    m_currentCommand = command;
    updateCrashContext();
    emitLog(QString());
    emitLog(QString::fromUtf8("==> \xF0\x9F\x94\xA7 %1").arg(label)); // 🔧

#ifdef Q_OS_WIN
    launch(Phase::Step, QStringLiteral("cmd"),
           {QStringLiteral("/c"), command}, m_worktree);
#else
    const QString shell =
        QFile::exists(QStringLiteral("/bin/bash")) ? QStringLiteral("/bin/bash")
                                                   : QStringLiteral("/bin/sh");
    launch(Phase::Step, shell, {QStringLiteral("-c"), command}, m_worktree);
#endif
}

void ActionRunner::complete(bool ok, const QString &finalMessage)
{
    if (!ok && !m_stopping)
        logFailureDiagnostic(finalMessage);

    emitLog(QString());
    emitLog((m_stopping ? QString::fromUtf8("==> \xF0\x9F\x9B\x91 STOPPED: ") // 🛑
                        : ok ? QString::fromUtf8("==> \xE2\x9C\x85 SUCCESS: ") // ✅
                             : QString::fromUtf8("==> \xE2\x9D\x8C FAILED: ")) + // ❌
            finalMessage);

    // A release run writes its artifact metadata into the throwaway worktree;
    // harvest it into the working copy BEFORE cleanupWorktree() removes the dir,
    // otherwise the release would never show up (issue: no artifacts listed / no
    // prebuilt binary published).
    bool landed = false;
    if (ok && isReleaseRun())
        landed = landReleaseMetadata();

    cleanupWorktree();

    m_run.status = m_stopping ? ActionStatus::Cancelled
                              : ok ? ActionStatus::Success
                                   : ActionStatus::Failed;
    m_run.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    m_phase = Phase::Idle;
    m_busy = false;
    forkmesh::setCrashContext(QString());
    forkmesh::setTerminationSignalSurvivalEnabled(false);
    emit statusChanged(m_run.id, m_run.status);
    emit finished(m_run.id, ok);
    if (landed)
        emit releaseMetadataLanded(m_run.id);
}

bool ActionRunner::isReleaseRun() const
{
    return m_run.ref.startsWith(QLatin1String("refs/tags/"));
}

bool ActionRunner::landReleaseMetadata()
{
    // Only the owner (who holds the working copy) can publish; a mirror-only node
    // has nowhere to commit and can't publish anyway.
    if (m_repoWorkTree.isEmpty() || !QDir(m_repoWorkTree).exists())
        return false;
    const QDir produced(m_worktree + QStringLiteral("/.forkmesh/releases"));
    if (!produced.exists())
        return false;

    // Mirror .forkmesh/releases/ from the worktree into the working copy. The metadata is a
    // few tiny text files (SHASUMS256.txt + release.json per channel); the binary
    // bytes are NOT here — they already live in the served CAS.
    const QDir dest(m_repoWorkTree + QStringLiteral("/.forkmesh/releases"));
    const QString destRoot = dest.absolutePath();
    QDir().mkpath(destRoot);
    const QFileInfoList entries = produced.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool copiedAny = false;
    std::function<void(const QString &, const QString &)> copyTree =
        [&](const QString &srcDir, const QString &dstDir) {
            QDir().mkpath(dstDir);
            const QFileInfoList items = QDir(srcDir).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &fi : items) {
                const QString to = dstDir + QLatin1Char('/') + fi.fileName();
                if (fi.isDir()) {
                    copyTree(fi.absoluteFilePath(), to);
                } else {
                    QFile::remove(to); // overwrite an older manifest
                    if (QFile::copy(fi.absoluteFilePath(), to))
                        copiedAny = true;
                }
            }
        };
    for (const QFileInfo &fi : entries) {
        const QString to = destRoot + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) {
            copyTree(fi.absoluteFilePath(), to);
        } else {
            QFile::remove(to);
            if (QFile::copy(fi.absoluteFilePath(), to))
                copiedAny = true;
        }
    }
    if (!copiedAny)
        return false;

    // Carry the version-header bump the release workflow made (project(ForkMesh
    // VERSION ...)) into the working copy too, so it lands in the same commit.
    // The path is only staged when the bump actually changed something, keeping
    // the release commit to .forkmesh/releases/ alone whenever the header is already in
    // sync (or the tag wasn't a clean semver).
    QStringList paths{QStringLiteral(".forkmesh/releases")};
    if (landVersionHeader())
        paths << QStringLiteral("qt_client/CMakeLists.txt");

    // Stage just those paths and commit only if they actually changed, so
    // re-cutting an identical release is a no-op and we never disturb unrelated
    // edits.
    auto git = [&](const QStringList &args) {
        QProcess p;
        p.setWorkingDirectory(m_repoWorkTree);
        p.start(QStringLiteral("git"), args);
        p.waitForFinished(30000);
        return p.exitCode();
    };
    git(QStringList{QStringLiteral("add"), QStringLiteral("--")} + paths);
    if (git(QStringList{QStringLiteral("diff"), QStringLiteral("--cached"),
                        QStringLiteral("--quiet"), QStringLiteral("--")} +
            paths) == 0) {
        emitLog(QString::fromUtf8(
            "==> \xE2\x84\xB9\xEF\xB8\x8F  Release metadata unchanged; nothing to "
            "publish.")); // ℹ️
        return false;
    }
    const QString tag = m_run.ref.mid(QStringLiteral("refs/tags/").size());
    const QString message =
        paths.contains(QStringLiteral("qt_client/CMakeLists.txt"))
            ? QStringLiteral("release: publish %1 artifacts, bump version header")
                  .arg(tag)
            : QStringLiteral("release: publish %1 artifacts").arg(tag);
    const int rc = git(QStringList{QStringLiteral("-c"),
                                   QStringLiteral("user.email=actions@forkmesh.local"),
                                   QStringLiteral("-c"),
                                   QStringLiteral("user.name=ForkMesh Actions"),
                                   QStringLiteral("commit"), QStringLiteral("-m"),
                                   message, QStringLiteral("--")} +
                       paths);
    if (rc != 0) {
        emitLog(QString::fromUtf8(
            "!! Could not commit release metadata into the working copy."));
        return false;
    }
    emitLog(QString::fromUtf8(
                "==> \xF0\x9F\x93\xA6 Attached release metadata for %1; "
                "publishing\xE2\x80\xA6") // 📦 …
                .arg(tag));
    return true;
}

bool ActionRunner::landVersionHeader()
{
    if (m_repoWorkTree.isEmpty() || m_worktree.isEmpty())
        return false;
    const QString rel = QStringLiteral("qt_client/CMakeLists.txt");
    QFile src(m_worktree + QLatin1Char('/') + rel);
    QFile dst(m_repoWorkTree + QLatin1Char('/') + rel);

    // The version sits on a single line: project(ForkMesh VERSION X.Y.Z ...).
    static const QRegularExpression versionLine(
        QStringLiteral("^(project\\(ForkMesh VERSION )([0-9]+\\.[0-9]+\\.[0-9]+)"),
        QRegularExpression::MultilineOption);

    if (!src.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QRegularExpressionMatch want =
        versionLine.match(QString::fromUtf8(src.readAll()));
    src.close();
    if (!want.hasMatch())
        return false; // workflow left the header alone (e.g. no clean semver tag)
    const QString version = want.captured(2);

    if (!dst.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString text = QString::fromUtf8(dst.readAll());
    dst.close();
    const QRegularExpressionMatch have = versionLine.match(text);
    if (!have.hasMatch() || have.captured(2) == version)
        return false; // header missing here or already at the release version
    text.replace(have.capturedStart(2), have.capturedLength(2), version);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const bool wrote = dst.write(text.toUtf8()) >= 0;
    dst.close();
    return wrote;
}

void ActionRunner::cleanupWorktree()
{
    if (m_worktree.isEmpty())
        return;
    // Best-effort: detach the worktree from the mirror, then remove the dir.
    QProcess::execute(QStringLiteral("git"),
                      {QStringLiteral("-C"), m_mirror, QStringLiteral("worktree"),
                       QStringLiteral("remove"), QStringLiteral("--force"),
                       m_worktree});
    QDir(m_worktree).removeRecursively();
    m_worktree.clear();
}

void ActionRunner::emitLog(const QString &text)
{
    const QString safe = redact(text);
    m_store->appendLog(m_run, safe);
    emit logLine(m_run.id, safe);
}

void ActionRunner::emitProcessOutput(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;

    rememberCrashProcessOutput(bytes);
    updateCrashContext();

    if (m_processOutputBytes >= kActionProcessLogMaxBytes) {
        emitProcessOutputTruncationNotice();
        rememberSuppressedProcessOutput(bytes);
        return;
    }

    const qsizetype remainingBytes =
        kActionProcessLogMaxBytes - m_processOutputBytes;
    const qsizetype keepBytes = qMin(bytes.size(), remainingBytes);
    for (qsizetype offset = 0; offset < keepBytes;
         offset += kActionProcessLogChunkBytes) {
        const qsizetype chunkBytes =
            qMin(kActionProcessLogChunkBytes, keepBytes - offset);
        const QString safe =
            normalizeProcessOutputForLog(QString::fromUtf8(bytes.constData() + offset,
                                                           chunkBytes));
        if (!safe.isEmpty())
            emitLog(safe);
    }

    m_processOutputBytes += keepBytes;
    if (keepBytes < bytes.size()) {
        emitProcessOutputTruncationNotice();
        rememberSuppressedProcessOutput(bytes.sliced(keepBytes));
    }
}

void ActionRunner::emitProcessOutputTruncationNotice()
{
    if (m_processOutputTruncated)
        return;
    m_processOutputTruncated = true;
    emitLog(QStringLiteral(
                "\n!! Action output exceeded %1 KiB. Suppressing middle output "
                "to keep the app responsive; the last %2 KiB will be shown "
                "when this step exits.\n")
                .arg(kActionProcessLogMaxBytes / 1024)
                .arg(kActionProcessLogTailBytes / 1024));
}

void ActionRunner::rememberSuppressedProcessOutput(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;

    m_processOutputSuppressedBytes += bytes.size();
    m_processOutputTail.append(bytes);
    if (m_processOutputTail.size() > kActionProcessLogTailBytes)
        m_processOutputTail =
            m_processOutputTail.right(kActionProcessLogTailBytes);
}

void ActionRunner::emitSuppressedProcessOutputTail()
{
    if (!m_processOutputTruncated || m_processOutputTail.isEmpty())
        return;

    emitLog(QStringLiteral(
                "\n!! Showing the final %1 KiB of suppressed process output "
                "(%2 KiB omitted).\n")
                .arg((m_processOutputTail.size() + 1023) / 1024)
                .arg((m_processOutputSuppressedBytes + 1023) / 1024));
    m_processOutputLineChars = 0;
    for (qsizetype offset = 0; offset < m_processOutputTail.size();
         offset += kActionProcessLogChunkBytes) {
        const qsizetype chunkBytes =
            qMin(kActionProcessLogChunkBytes,
                 m_processOutputTail.size() - offset);
        const QString safe =
            normalizeProcessOutputForLog(QString::fromUtf8(
                m_processOutputTail.constData() + offset, chunkBytes));
        if (!safe.isEmpty())
            emitLog(safe);
    }
    emitLog(QStringLiteral("\n!! End of suppressed process output.\n"));
    m_processOutputTail.clear();
}

QString ActionRunner::normalizeProcessOutputForLog(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            out += QLatin1Char('\n');
            m_processOutputLineChars = 0;
            continue;
        }
        if (ch == QLatin1Char('\t')) {
            out += ch;
            m_processOutputLineChars += 4;
        } else if (u < 0x20 || (u >= 0x7f && u <= 0x9f)) {
            continue;
        } else {
            out += ch;
            ++m_processOutputLineChars;
        }
        if (m_processOutputLineChars >= kActionProcessLogMaxLineChars) {
            out += QStringLiteral("\n...[long line wrapped for display]...\n");
            m_processOutputLineChars = 0;
        }
    }
    return out;
}

void ActionRunner::rememberCrashProcessOutput(const QByteArray &bytes)
{
    m_crashOutputTail.append(bytes);
    if (m_crashOutputTail.size() > kActionCrashOutputTailBytes)
        m_crashOutputTail = m_crashOutputTail.right(kActionCrashOutputTailBytes);
}

QString ActionRunner::phaseName() const
{
    switch (m_phase) {
    case Phase::Idle: return QStringLiteral("idle");
    case Phase::Checkout: return QStringLiteral("checkout");
    case Phase::Step: return QStringLiteral("step");
    }
    return QStringLiteral("unknown");
}

QString ActionRunner::crashContext() const
{
    QString out =
        QStringLiteral("action run id: %1\nworkflow: %2 (%3)\nrepo: %4/%5\n"
                       "commit: %6\nref: %7\nphase: %8\nstep: %9\ncommand: %10\n"
                       "worktree: %11\n")
            .arg(m_run.id)
            .arg(oneLine(m_workflow.name), oneLine(m_workflow.path),
                 oneLine(m_run.owner), oneLine(m_run.name), oneLine(m_run.commit),
                 oneLine(m_run.ref), phaseName(), oneLine(m_currentStepLabel),
                 oneLine(m_currentCommand), oneLine(m_worktree));
    const QString tail = diagnosticSafeText(
        redact(QString::fromUtf8(m_crashOutputTail)), 8000);
    if (!tail.trimmed().isEmpty())
        out += QStringLiteral("recent process output tail:\n") + tail + QLatin1Char('\n');
    if (out.size() > kActionCrashContextMaxChars)
        out = QStringLiteral("...(crash context truncated; showing tail)...\n") +
              out.right(kActionCrashContextMaxChars);
    return out;
}

void ActionRunner::updateCrashContext() const
{
    forkmesh::setCrashContext(crashContext());
}

void ActionRunner::logFailureDiagnostic(const QString &finalMessage) const
{
    forkmesh::logDiagnosticEvent(
        QStringLiteral("Action failed before UI refresh"),
        crashContext() + QStringLiteral("final message: ") + finalMessage);
}

QString ActionRunner::redact(QString text) const
{
    for (const QString &secret : m_secrets)
        if (!secret.isEmpty())
            text.replace(secret, QStringLiteral("***"));
    return text;
}
