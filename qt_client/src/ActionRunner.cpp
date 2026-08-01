#include "ActionRunner.h"

#include "CrashHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <functional>

#ifndef Q_OS_WIN
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
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
constexpr qint64 kMaxLandedArtifactBytes = 1024LL * 1024 * 1024;







int actionCpuQuotaPercent()
{
    const int cores = qMax(1, QThread::idealThreadCount());
    return qBound(200, (cores - 1) * 100, 800);
}

bool truthy(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("1") ||
           normalized == QLatin1String("true") ||
           normalized == QLatin1String("yes") ||
           normalized == QLatin1String("on");
}

bool dangerousEnvironmentName(const QString &name)
{
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,127}$"));
    if (!valid.match(name).hasMatch())
        return true;
    const QString upper = name.toUpper();
    static const QSet<QString> exact{
        QStringLiteral("BASH_ENV"),       QStringLiteral("ENV"),
        QStringLiteral("HOME"),           QStringLiteral("IFS"),
        QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("LD_PRELOAD"),
        QStringLiteral("PATH"),           QStringLiteral("PYTHONHOME"),
        QStringLiteral("PYTHONPATH"),     QStringLiteral("SHELLOPTS"),
        QStringLiteral("BASHOPTS"),       QStringLiteral("TMPDIR"),
        QStringLiteral("XDG_CONFIG_HOME"), QStringLiteral("XDG_DATA_HOME"),
        QStringLiteral("XDG_STATE_HOME"), QStringLiteral("GIT_CONFIG"),
        QStringLiteral("GIT_CONFIG_GLOBAL"),
        QStringLiteral("GIT_CONFIG_SYSTEM"),
    };
    return exact.contains(upper) ||
           upper.startsWith(QLatin1String("GIT_CONFIG_KEY_")) ||
           upper.startsWith(QLatin1String("GIT_CONFIG_VALUE_"));
}

#ifndef Q_OS_WIN
void setLimit(int resource, rlim_t value)
{
    struct rlimit limit {
        value, value
    };
    ::setrlimit(resource, &limit);
}
#endif

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

}

ActionRunner::ActionRunner(ActionStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    m_stepTimer = new QTimer(this);
    m_stepTimer->setSingleShot(true);


    m_stepTimer->setTimerType(Qt::PreciseTimer);
    connect(m_stepTimer, &QTimer::timeout, this,
            [this] { terminateCurrentProcess(true); });
    m_jobTimer = new QTimer(this);
    m_jobTimer->setSingleShot(true);
    connect(m_jobTimer, &QTimer::timeout, this, [this] {
        m_jobTimedOut = true;
        terminateCurrentProcess(true);
    });
}

void ActionRunner::setSandboxLimitsForTesting(
    const ActionSandboxLimits &limits)
{
    if (m_busy)
        return;
    m_limits = limits;
}

bool ActionRunner::sandboxAvailable(QString *reason)
{
    if (reason)
        reason->clear();
#if defined(Q_OS_LINUX)
    if (::geteuid() == 0) {
        if (reason)
            *reason = QStringLiteral(
                "Actions refuse to run while the ForkMesh node is root");
        return false;
    }
    const QString bwrap =
        QStandardPaths::findExecutable(QStringLiteral("bwrap"));
    if (bwrap.isEmpty()) {
        if (reason)
            *reason = QStringLiteral(
                "bubblewrap (bwrap) is required for Actions isolation");
        return false;
    }
    const QString systemdRun =
        QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    const QString systemctl =
        QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemdRun.isEmpty() || systemctl.isEmpty()) {
        if (reason)
            *reason = QStringLiteral(
                "systemd-run and systemctl are required for Actions cgroup "
                "limits");
        return false;
    }
    QProcess manager;
    manager.start(systemctl,
                  {QStringLiteral("--user"),
                   QStringLiteral("show-environment")});
    if (!manager.waitForStarted(1000) ||
        !manager.waitForFinished(3000) ||
        manager.exitStatus() != QProcess::NormalExit ||
        manager.exitCode() != 0) {
        manager.kill();
        if (reason)
            *reason = QStringLiteral(
                "the unprivileged node user has no active systemd user "
                "manager");
        return false;
    }
    return true;
#else
    if (reason)
        *reason = QStringLiteral(
            "this platform has no configured ForkMesh Actions sandbox");
    return false;
#endif
}

void ActionRunner::start(const ActionRun &run, const ActionWorkflow &workflow,
                         const QString &mirrorPath, const QString &workTreePath,
                         const QMap<QString, QString> &variables)
{
    if (m_busy)
        return;
    m_busy = true;
    m_stopping = false;
    m_run = run;
    m_workflow = workflow;
    m_mirror = mirrorPath;
    m_repoWorkTree = workTreePath;
    m_variables = variables;
    m_exposedVariables = explicitWorkflowVariables();
    m_networkAllowed =
        truthy(variables.value(QStringLiteral("FORKMESH_ACTIONS_ALLOW_NETWORK")));
    m_stepIndex = 0;
    m_processOutputBytes = 0;
    m_processOutputSuppressedBytes = 0;
    m_processOutputTail.clear();
    m_processOutputLineChars = 0;
    m_crashOutputTail.clear();
    m_currentStepLabel = QStringLiteral("Checkout");
    m_currentCommand.clear();
    m_processOutputTruncated = false;
    m_processTimedOut = false;
    m_jobTimedOut = false;
    m_processStdin.clear();
    forkmesh::setTerminationSignalSurvivalEnabled(true);

    m_secrets.clear();
    for (const QString &value : m_exposedVariables.values())
        if (!value.isEmpty())
            m_secrets.append(value);

    m_run.status = ActionStatus::Running;
    m_run.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    emit statusChanged(m_run.id, m_run.status);

    emitLog(QString::fromUtf8("==> \xF0\x9F\x9A\x80 Workflow: %1 (%2)")
                .arg(m_workflow.name, m_workflow.path));
    emitLog(QString::fromUtf8("==> \xF0\x9F\x93\x8D Commit:   %1 on %2")
                .arg(m_run.commit.left(12), m_run.ref));
    emitLog(QString::fromUtf8(
        "==> \xF0\x9F\x8C\xBF Checking out into a temporary worktree\xE2\x80\xA6"));





    if (m_mirror.trimmed().isEmpty() || !QDir(m_mirror).exists()) {
        complete(false,
                 QStringLiteral("The served mirror for %1/%2 is no longer "
                                "available at %3.")
                     .arg(m_run.owner, m_run.name,
                          m_mirror.isEmpty() ? QStringLiteral("(unset)")
                                             : m_mirror));
        return;
    }

    QString sandboxReason;
    if (!sandboxAvailable(&sandboxReason)) {
        complete(false, QStringLiteral("Actions isolation unavailable: %1.")
                            .arg(sandboxReason));
        return;
    }

    m_sandboxRoot =
        QDir::tempPath() + QStringLiteral("/forkmesh-run-") +
        QString::number(m_run.id) + QStringLiteral("-") +
        QString::number(QDateTime::currentMSecsSinceEpoch());
    m_worktree = m_sandboxRoot + QStringLiteral("/source");
    m_workspace = m_sandboxRoot + QStringLiteral("/workspace");
    m_sandboxHome = m_sandboxRoot + QStringLiteral("/home");
    m_sandboxTmp = m_sandboxRoot + QStringLiteral("/tmp");
    m_releaseStaging = m_sandboxRoot + QStringLiteral("/release-cas");
    if (!QDir().mkpath(m_sandboxRoot) ||
        !QDir().mkpath(m_sandboxHome) ||
        !QDir().mkpath(m_sandboxTmp) ||
        !QDir().mkpath(m_releaseStaging)) {
        complete(false, QStringLiteral("Could not create the Actions sandbox."));
        return;
    }
    QFile::setPermissions(
        m_sandboxRoot,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner);
    m_jobTimer->start(qMax(1000, m_limits.jobTimeoutMs));

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
    emitLog(QString::fromUtf8("==> \xF0\x9F\x9B\x91 Stop requested by user."));

    if (m_process && m_process->state() != QProcess::NotRunning) {
        terminateCurrentProcess(false);
        return;
    }
    complete(false, QStringLiteral("Stopped."));
}

void ActionRunner::terminateCurrentProcess(bool timedOut)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    if (timedOut)
        m_processTimedOut = true;
#if defined(Q_OS_LINUX)
    if (!m_systemdUnit.isEmpty()) {
        const QString systemctl =
            QStandardPaths::findExecutable(QStringLiteral("systemctl"));
        if (!systemctl.isEmpty()) {
            QProcess::execute(systemctl,
                              {QStringLiteral("--user"),
                               QStringLiteral("kill"),
                               QStringLiteral("--kill-whom=all"),
                               m_systemdUnit});
        }
    }
#endif
#ifndef Q_OS_WIN



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
    m_processTimedOut = false;
    if (m_currentStepLabel.isEmpty())
        m_currentStepLabel = phase == Phase::Checkout
                                 ? QStringLiteral("Checkout")
                                 : QStringLiteral("Step %1").arg(m_stepIndex + 1);
    if (phase != Phase::Step) {
        m_currentCommand = program;
        for (const QString &arg : args)
            m_currentCommand += QLatin1Char(' ') + arg;
    }
    updateCrashContext();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDir.isEmpty())
        m_process->setWorkingDirectory(workingDir);

#ifndef Q_OS_WIN



    const ActionSandboxLimits limits = m_limits;
    const bool sandboxedStep = phase == Phase::Step;
    m_process->setChildProcessModifier([limits, sandboxedStep] {
        ::setsid();
        ::umask(0077);
        setLimit(RLIMIT_CORE, 0);
        if (!sandboxedStep)
            return;
        setLimit(RLIMIT_AS, static_cast<rlim_t>(
                               qMax<qint64>(64 * 1024 * 1024,
                                            limits.maxMemoryBytes)));
        setLimit(RLIMIT_FSIZE, static_cast<rlim_t>(
                                  qMax<qint64>(1024 * 1024,
                                               limits.maxFileBytes)));
        setLimit(RLIMIT_NOFILE,
                 static_cast<rlim_t>(qMax(32, limits.maxOpenFiles)));
        setLimit(RLIMIT_CPU,
                 static_cast<rlim_t>(qMax(10, limits.maxCpuSeconds)));
    });
#endif




    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"),
               phase == Phase::Step
                   ? QStringLiteral(
                         "/home/forkmesh/.local/bin:/home/forkmesh/.cargo/bin:"
                         "/usr/local/bin:/usr/bin:/bin")
                   : QStringLiteral("/usr/local/bin:/usr/bin:/bin"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("CI"), QStringLiteral("true"));
    env.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), QStringLiteral("/dev/null"));
    if (phase == Phase::Step) {
#if defined(Q_OS_LINUX)
        const QString runtimeDir =
            QStringLiteral("/run/user/%1").arg(::getuid());
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtimeDir);
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
                   QStringLiteral("unix:path=%1/bus").arg(runtimeDir));
#endif
        env.insert(QStringLiteral("HOME"), QStringLiteral("/home/forkmesh"));
        env.insert(QStringLiteral("TMPDIR"), QStringLiteral("/tmp"));
        env.insert(QStringLiteral("XDG_CACHE_HOME"),
                   QStringLiteral("/home/forkmesh/.cache"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"),
                   QStringLiteral("/home/forkmesh/.config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"),
                   QStringLiteral("/home/forkmesh/.local/share"));
        env.insert(QStringLiteral("XDG_STATE_HOME"),
                   QStringLiteral("/home/forkmesh/.local/state"));





        env.insert(QStringLiteral("FORKMESH_ACTIONS_CPUS"),
                   QString::number(qMax(1, actionCpuQuotaPercent() / 100)));
        env.insert(QStringLiteral("FORKMESH_ACTIONS_MEMORY_MB"),
                   QString::number(
                       qMax<qint64>(64, m_limits.maxMemoryBytes / (1024 * 1024))));
        for (auto it = m_exposedVariables.constBegin();
             it != m_exposedVariables.constEnd(); ++it)
            env.insert(it.key(), it.value());
        for (auto it = m_workflow.env.constBegin();
             it != m_workflow.env.constEnd(); ++it) {
            if (!dangerousEnvironmentName(it.key()))
                env.insert(it.key(),
                           ActionFile::substitute(it.value(),
                                                  m_exposedVariables));
        }
        if (m_run.ref.startsWith(QLatin1String("refs/tags/")))
            env.insert(QStringLiteral("FORKMESH_TAG"),
                       m_run.ref.mid(QStringLiteral("refs/tags/").size()));
        if (isReleaseRun()) {
            env.insert(QStringLiteral("FORKMESH_RELEASE_CAS"),
                       QStringLiteral("/release-cas"));
            if (!m_run.owner.isEmpty() && !m_run.name.isEmpty())
                env.insert(QStringLiteral("FORKMESH_REPO"),
                           m_run.owner + QLatin1Char('/') + m_run.name);
        }
    } else {
        env.insert(QStringLiteral("HOME"), m_sandboxHome);
        env.insert(QStringLiteral("TMPDIR"), m_sandboxTmp);
    }
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
                    emitLog(QString::fromUtf8("!! \xE2\x9A\xA0\xEF\xB8\x8F  ") +
                            m_process->errorString());
            });
    connect(m_process, &QProcess::started, this, [this] {
        if (m_process && !m_processStdin.isEmpty()) {
            m_process->write(m_processStdin);
            m_process->closeWriteChannel();
            m_processStdin.clear();
        }
    });

    m_process->setProgram(program);
    m_process->setArguments(args);
    m_process->start();
    const int timeoutMs =
        phase == Phase::Step
            ? qMax(250, m_limits.stepTimeoutMs)
            : qMax(30000, qMin(m_limits.jobTimeoutMs, 5 * 60 * 1000));
    m_stepTimer->start(timeoutMs);
}

QMap<QString, QString> ActionRunner::explicitWorkflowVariables() const
{
    QSet<QString> referenced;
    static const QList<QRegularExpression> references{
        QRegularExpression(
            QStringLiteral(
                "\\$\\{\\{\\s*vars\\.([A-Za-z_][A-Za-z0-9_]*)\\s*\\}\\}")),
        QRegularExpression(
            QStringLiteral("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}")),
        QRegularExpression(
            QStringLiteral("\\$([A-Za-z_][A-Za-z0-9_]*)")),
    };
    for (const QRegularExpression &expression : references) {
        auto matches = expression.globalMatch(m_workflow.content);
        while (matches.hasNext())
            referenced.insert(matches.next().captured(1));
    }
    for (const QString &value : m_workflow.env)
        for (const QRegularExpression &expression : references) {
            auto matches = expression.globalMatch(value);
            while (matches.hasNext())
                referenced.insert(matches.next().captured(1));
        }

    QMap<QString, QString> exposed;
    for (const QString &name : std::as_const(referenced)) {
        if (name == QLatin1String("FORKMESH_ACTIONS_ALLOW_NETWORK") ||
            dangerousEnvironmentName(name) || !m_variables.contains(name))
            continue;
        exposed.insert(name, m_variables.value(name));
    }
    return exposed;
}

QStringList ActionRunner::sandboxArguments(const QString &shell,
                                           const QString &command) const
{
    Q_UNUSED(command)
    QStringList args{
        QStringLiteral("--die-with-parent"),
        QStringLiteral("--new-session"),
        QStringLiteral("--unshare-user"),
        QStringLiteral("--unshare-pid"),
        QStringLiteral("--unshare-ipc"),
        QStringLiteral("--unshare-uts"),
        QStringLiteral("--unshare-cgroup-try"),
    };
    if (!m_networkAllowed)
        args << QStringLiteral("--unshare-net");
    args << QStringLiteral("--uid") << QStringLiteral("65534")
         << QStringLiteral("--gid") << QStringLiteral("65534")
         << QStringLiteral("--cap-drop") << QStringLiteral("ALL")
         << QStringLiteral("--hostname") << QStringLiteral("forkmesh-action")
         << QStringLiteral("--proc") << QStringLiteral("/proc")
         << QStringLiteral("--dev") << QStringLiteral("/dev")
         << QStringLiteral("--dir") << QStringLiteral("/run")
         << QStringLiteral("--dir") << QStringLiteral("/var")
         << QStringLiteral("--tmpfs") << QStringLiteral("/var/tmp")
         << QStringLiteral("--dir") << QStringLiteral("/etc");

    const auto bindReadOnlyIfPresent = [&args](const QString &path) {
        if (QFileInfo::exists(path))
            args << QStringLiteral("--ro-bind") << path << path;
    };



    bindReadOnlyIfPresent(QStringLiteral("/usr"));
    bindReadOnlyIfPresent(QStringLiteral("/bin"));
    bindReadOnlyIfPresent(QStringLiteral("/lib"));
    bindReadOnlyIfPresent(QStringLiteral("/lib64"));
    bindReadOnlyIfPresent(QStringLiteral("/etc/passwd"));
    bindReadOnlyIfPresent(QStringLiteral("/etc/group"));
    bindReadOnlyIfPresent(QStringLiteral("/etc/nsswitch.conf"));
    bindReadOnlyIfPresent(QStringLiteral("/etc/ssl"));
    bindReadOnlyIfPresent(QStringLiteral("/etc/ca-certificates"));
    if (m_networkAllowed) {
        bindReadOnlyIfPresent(QStringLiteral("/etc/resolv.conf"));
        bindReadOnlyIfPresent(QStringLiteral("/etc/hosts"));
    }

    args << QStringLiteral("--ro-bind") << m_worktree
         << QStringLiteral("/source")
         << QStringLiteral("--bind") << m_workspace
         << QStringLiteral("/workspace")
         << QStringLiteral("--dir") << QStringLiteral("/home")
         << QStringLiteral("--bind") << m_sandboxHome
         << QStringLiteral("/home/forkmesh")
         << QStringLiteral("--bind") << m_sandboxTmp
         << QStringLiteral("/tmp")
         << QStringLiteral("--bind") << m_releaseStaging
         << QStringLiteral("/release-cas")
         << QStringLiteral("--unsetenv")
         << QStringLiteral("XDG_RUNTIME_DIR")
         << QStringLiteral("--unsetenv")
         << QStringLiteral("DBUS_SESSION_BUS_ADDRESS")
         << QStringLiteral("--chdir") << QStringLiteral("/workspace")
         << shell << QStringLiteral("-se");
    return args;
}

void ActionRunner::launchSandboxedStep(const QString &command)
{
#if defined(Q_OS_LINUX)
    const QString bwrap =
        QStandardPaths::findExecutable(QStringLiteral("bwrap"));
    const QString systemdRun =
        QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    const QString shell =
        QFile::exists(QStringLiteral("/bin/bash")) ? QStringLiteral("/bin/bash")
                                                   : QStringLiteral("/bin/sh");


    m_processStdin = command.toUtf8();
    if (!m_processStdin.endsWith('\n'))
        m_processStdin.append('\n');
    m_systemdUnit =
        QStringLiteral("forkmesh-action-%1-%2-%3.scope")
            .arg(QCoreApplication::applicationPid())
            .arg(m_run.id)
            .arg(m_stepIndex);
    const int runtimeSeconds =
        qMax(1, (m_limits.stepTimeoutMs + 999) / 1000);
    QStringList args{
        QStringLiteral("--user"),
        QStringLiteral("--scope"),
        QStringLiteral("--quiet"),
        QStringLiteral("--unit=") + m_systemdUnit,
        QStringLiteral("--property=TasksMax=%1")
            .arg(qMax(8, m_limits.maxProcesses)),
        QStringLiteral("--property=MemoryMax=%1")
            .arg(qMax<qint64>(64 * 1024 * 1024,
                              m_limits.maxMemoryBytes)),
        QStringLiteral("--property=MemorySwapMax=0"),
        QStringLiteral("--property=CPUQuota=%1%").arg(actionCpuQuotaPercent()),
        QStringLiteral("--property=CPUWeight=20"),



        QStringLiteral("--property=RuntimeMaxSec=%1").arg(runtimeSeconds + 60),
        QStringLiteral("--"),
        bwrap,
    };
    args += sandboxArguments(shell, command);
    launch(Phase::Step, systemdRun, args, QString());
#else
    Q_UNUSED(command)
    complete(false, QStringLiteral(
                        "Actions isolation is unavailable on this platform."));
#endif
}

bool ActionRunner::verifyWorkspaceSnapshot(QString *reason) const
{
    QString tree;
    QString digest;
    QString error;
    if (!ActionStore::repositoryStateDigest(
            m_workspace, QStringLiteral("HEAD"), &tree, &digest, &error)) {
        if (reason)
            *reason = error;
        return false;
    }
    if (tree != m_run.repositoryTree || digest != m_run.executionDigest) {
        if (reason)
            *reason = QStringLiteral(
                "the checked-out repository state no longer matches the "
                "approved execution digest");
        return false;
    }
    return true;
}

bool ActionRunner::workspaceWithinQuota(QString *reason) const
{
    qint64 bytes = 0;
    QDirIterator it(m_sandboxRoot,
                    QDir::Files | QDir::NoDotAndDotDot |
                        QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (!info.isFile())
            continue;
        const qint64 size = info.size();
        if (size < 0 ||
            bytes > m_limits.maxWorkspaceBytes - size) {
            if (reason)
                *reason = QStringLiteral(
                    "the Actions writable workspace exceeded its disk quota");
            return false;
        }
        bytes += size;
    }
    return true;
}

void ActionRunner::onProcessFinished(int exitCode)
{
    m_stepTimer->stop();
    m_systemdUnit.clear();

    if (m_process) {
        const QByteArray tail = m_process->readAllStandardOutput();
        if (!tail.isEmpty())
            emitProcessOutput(tail);
        emitSuppressedProcessOutputTail();
        m_process->deleteLater();
        m_process = nullptr;
    }



    if (m_stopping) {
        complete(false, QStringLiteral("Stopped by user."));
        return;
    }
    if (m_processTimedOut) {
        complete(false,
                 m_jobTimedOut
                     ? QStringLiteral("Workflow exceeded the total time limit.")
                     : QStringLiteral("%1 exceeded the process time limit.")
                           .arg(m_currentStepLabel));
        return;
    }

    if (m_phase == Phase::Checkout) {
        if (exitCode != 0) {
            complete(false, QStringLiteral("Checkout failed."));
            return;
        }
        m_currentStepLabel = QStringLiteral("Isolated workspace");
        emitLog(QString::fromUtf8(
            "==> \xF0\x9F\x94\x92 Creating a disposable writable clone; "
            "the approved source stays read-only."));
        launch(Phase::WorkspaceClone, QStringLiteral("git"),
               {QStringLiteral("clone"), QStringLiteral("--no-checkout"),
                QStringLiteral("--no-hardlinks"), QStringLiteral("--no-local"),
                m_mirror, m_workspace},
               QString());
        return;
    }
    if (m_phase == Phase::WorkspaceClone) {
        if (exitCode != 0) {
            complete(false, QStringLiteral(
                                "Could not create the isolated workspace."));
            return;
        }
        launch(Phase::WorkspaceCheckout, QStringLiteral("git"),
               {QStringLiteral("-C"), m_workspace,
                QStringLiteral("checkout"), QStringLiteral("--detach"),
                m_run.commit},
               QString());
        return;
    }
    if (m_phase == Phase::WorkspaceCheckout) {
        if (exitCode != 0) {
            complete(false, QStringLiteral(
                                "Could not check out the approved commit."));
            return;
        }
        QString reason;
        if (!verifyWorkspaceSnapshot(&reason)) {
            complete(false, QStringLiteral(
                                "Repository approval verification failed: %1.")
                                .arg(reason));
            return;
        }
        emitLog(QString::fromUtf8(
                    "==> \xE2\x9C\x85 Approval bound to tree %1 and state "
                    "SHA-256 %2.")
                    .arg(m_run.repositoryTree.left(12),
                         m_run.executionDigest.left(16)));
        emitLog(m_networkAllowed
                    ? QStringLiteral(
                          "==> Network access explicitly enabled by this "
                          "node's Actions policy.")
                    : QStringLiteral(
                          "==> Network namespace isolated (egress denied)."));
        runNextStep();
        return;
    }


    if (exitCode != 0) {
        complete(false, QStringLiteral("Step failed with exit code %1.")
                            .arg(exitCode));
        return;
    }
    QString quotaReason;
    if (!workspaceWithinQuota(&quotaReason)) {
        complete(false, quotaReason + QLatin1Char('.'));
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
    const QString command =
        ActionFile::substitute(step.run, m_exposedVariables);
    const QString label =
        step.name.isEmpty() ? QStringLiteral("Step %1").arg(m_stepIndex + 1)
                            : step.name;
    m_currentStepLabel = label;
    m_currentCommand = command;
    updateCrashContext();
    emitLog(QString());
    emitLog(QString::fromUtf8("==> \xF0\x9F\x94\xA7 %1").arg(label));

    launchSandboxedStep(command);
}

void ActionRunner::complete(bool ok, const QString &finalMessage)
{




    QString resultMessage = finalMessage;
    bool landed = false;
    if (ok && isReleaseRun()) {
        if (!landReleaseArtifacts()) {
            ok = false;
            resultMessage = QStringLiteral(
                "Release artifacts failed validation or could not be landed.");
        } else {
            landed = landReleaseMetadata();
        }
    }
    if (ok && !landActionArtifacts()) {
        ok = false;
        resultMessage = QStringLiteral(
            "Action artifacts failed validation or could not be landed.");
    }

    if (!ok && !m_stopping)
        logFailureDiagnostic(resultMessage);
    emitLog(QString());
    emitLog((m_stopping ? QString::fromUtf8("==> \xF0\x9F\x9B\x91 STOPPED: ")
                        : ok ? QString::fromUtf8("==> \xE2\x9C\x85 SUCCESS: ")
                             : QString::fromUtf8("==> \xE2\x9D\x8C FAILED: ")) +
            resultMessage);

    cleanupWorktree();

    m_run.status = m_stopping ? ActionStatus::Cancelled
                              : ok ? ActionStatus::Success
                                   : ActionStatus::Failed;
    m_run.finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_store->saveRun(m_run);
    m_phase = Phase::Idle;
    m_busy = false;
    m_stepTimer->stop();
    m_jobTimer->stop();
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


    if (m_repoWorkTree.isEmpty() || !QDir(m_repoWorkTree).exists())
        return false;
    const QDir produced(m_workspace + QStringLiteral("/.forkmesh/releases"));
    if (!produced.exists())
        return false;




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
                    QFile::remove(to);
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






    QStringList paths{QStringLiteral(".forkmesh/releases")};
    if (landVersionHeader())
        paths << QStringLiteral("qt_client/CMakeLists.txt");




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
            "publish."));
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
                "publishing\xE2\x80\xA6")
                .arg(tag));
    return true;
}

bool ActionRunner::landVersionHeader()
{
    if (m_repoWorkTree.isEmpty() || m_worktree.isEmpty())
        return false;
    const QString rel = QStringLiteral("qt_client/CMakeLists.txt");
    QFile src(m_workspace + QLatin1Char('/') + rel);
    QFile dst(m_repoWorkTree + QLatin1Char('/') + rel);


    static const QRegularExpression versionLine(
        QStringLiteral("^(project\\(ForkMesh VERSION )([0-9]+\\.[0-9]+\\.[0-9]+)"),
        QRegularExpression::MultilineOption);

    if (!src.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QRegularExpressionMatch want =
        versionLine.match(QString::fromUtf8(src.readAll()));
    src.close();
    if (!want.hasMatch())
        return false;
    const QString version = want.captured(2);

    if (!dst.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QString text = QString::fromUtf8(dst.readAll());
    dst.close();
    const QRegularExpressionMatch have = versionLine.match(text);
    if (!have.hasMatch() || have.captured(2) == version)
        return false;
    text.replace(have.capturedStart(2), have.capturedLength(2), version);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const bool wrote = dst.write(text.toUtf8()) >= 0;
    dst.close();
    return wrote;
}

bool ActionRunner::copySandboxTree(const QString &source,
                                   const QString &destination,
                                   qint64 maxBytes, QString *error)
{
    if (error)
        error->clear();
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists())
        return true;
    if (!sourceInfo.isDir() || sourceInfo.isSymLink()) {
        if (error)
            *error = QStringLiteral("artifact source is not a real directory");
        return false;
    }
    const QFileInfo destinationInfo(destination);
    if (destinationInfo.exists() &&
        (!destinationInfo.isDir() || destinationInfo.isSymLink())) {
        if (error)
            *error = QStringLiteral("artifact destination is unsafe");
        return false;
    }
    if (!QDir().mkpath(destination)) {
        if (error)
            *error = QStringLiteral("artifact destination could not be created");
        return false;
    }

    qint64 total = 0;
    QDir sourceDir(source);
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot |
                                QDir::System | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink()) {
            if (error)
                *error = QStringLiteral("artifact output contains a symlink");
            return false;
        }
        const QString relative = sourceDir.relativeFilePath(path);
        if (relative.isEmpty() || relative == QLatin1String(".") ||
            relative == QLatin1String("..") ||
            relative.startsWith(QLatin1String("../")) ||
            QDir::isAbsolutePath(relative)) {
            if (error)
                *error = QStringLiteral("artifact output escaped its directory");
            return false;
        }
        const QString target = QDir(destination).filePath(relative);
        if (info.isDir()) {
            const QFileInfo targetInfo(target);
            if ((targetInfo.exists() && targetInfo.isSymLink()) ||
                !QDir().mkpath(target)) {
                if (error)
                    *error = QStringLiteral("artifact directory is unsafe");
                return false;
            }
            continue;
        }
        if (!info.isFile()) {
            if (error)
                *error =
                    QStringLiteral("artifact output contains a special file");
            return false;
        }
        if (info.size() < 0 || info.size() > maxBytes - total) {
            if (error)
                *error = QStringLiteral("artifact output exceeds its size limit");
            return false;
        }
        total += info.size();
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("artifact output could not be read");
            return false;
        }
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() &&
            (!targetInfo.isFile() || targetInfo.isSymLink())) {
            if (error)
                *error = QStringLiteral("artifact target is unsafe");
            return false;
        }
        QSaveFile output(target);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("artifact target could not be written");
            return false;
        }
        while (!input.atEnd()) {
            const QByteArray chunk = input.read(256 * 1024);
            if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
                output.cancelWriting();
                if (error)
                    *error = QStringLiteral("artifact read failed");
                return false;
            }
            if (output.write(chunk) != chunk.size()) {
                output.cancelWriting();
                if (error)
                    *error = QStringLiteral("artifact write failed");
                return false;
            }
        }
        output.setPermissions(QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner);
        if (!output.commit()) {
            if (error)
                *error = QStringLiteral("artifact commit failed");
            return false;
        }
    }
    return true;
}

bool ActionRunner::landReleaseArtifacts()
{
    QDir staged(m_releaseStaging);
    if (!staged.exists() ||
        staged.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        return false;
    const QString destination =
        QDir(m_mirror).absoluteFilePath(QStringLiteral("forkmesh-releases"));
    QString error;
    const bool ok = copySandboxTree(m_releaseStaging, destination,
                                    kMaxLandedArtifactBytes, &error);
    if (!ok)
        emitLog(QStringLiteral("!! %1").arg(error));
    return ok;
}

bool ActionRunner::landActionArtifacts()
{
    const QString staged =
        QDir(m_sandboxHome).absoluteFilePath(
            QStringLiteral("forkmesh-artifacts"));
    if (!QDir(staged).exists())
        return true;
    const QString destination =
        QDir(m_store->artifactsDir())
            .absoluteFilePath(m_run.repoKey() + QLatin1Char('/') +
                              QString::number(m_run.id));
    QString error;
    const bool ok = copySandboxTree(staged, destination,
                                    kMaxLandedArtifactBytes, &error);
    if (ok)
        emitLog(QStringLiteral("==> Artifacts landed at %1").arg(destination));
    else
        emitLog(QStringLiteral("!! %1").arg(error));
    return ok;
}

void ActionRunner::cleanupWorktree()
{
    if (m_sandboxRoot.isEmpty())
        return;

    QProcess::execute(QStringLiteral("git"),
                      {QStringLiteral("-C"), m_mirror, QStringLiteral("worktree"),
                       QStringLiteral("remove"), QStringLiteral("--force"),
                       m_worktree});
    QDir(m_worktree).removeRecursively();
    QDir(m_sandboxRoot).removeRecursively();
    m_sandboxRoot.clear();
    m_worktree.clear();
    m_workspace.clear();
    m_sandboxHome.clear();
    m_sandboxTmp.clear();
    m_releaseStaging.clear();
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
