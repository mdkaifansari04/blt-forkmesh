#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace forkmesh::vm {

// The saved preference is deliberately separate from the boot-scoped active
// value. Changing the checkbox cannot move an already-running CLI across a VM
// boundary; the next ForkMesh process snapshots this setting in main().
inline constexpr auto kEnabledSetting = "runtime/kvmEnabled";
inline constexpr auto kLaunchDirectoryEnvironment =
    "FORKMESH_START_DIRECTORY";

struct LaunchCommand {
    QString program;
    QStringList arguments;
    QString error;
    bool isolated = false;
};

// Resolve the directory from which ForkMesh was originally launched. run.sh
// records its caller's directory before changing into qt_client; direct and
// installed launches naturally fall back to the process working directory.
QString launchDirectory();

// Snapshot the VM preference for this process. This may be called again by
// focused tests; production calls it once, before MainWindow is constructed.
void initialize(bool enabled, const QString &directory = QString(),
                const QString &limactlOverride = QString());

bool active();
QString workspaceRoot();
QString worktreeRoot();
QString instanceName();
QString limactlProgram();
QStringList mountedRoots();

// Empty means this Linux host has both limactl and usable /dev/kvm access.
// ForkMesh intentionally refuses QEMU's software-emulation fallback: this
// option promises a KVM boundary, not merely a slower process wrapper.
QString availabilityError();

bool pathIsShared(const QString &path);
QString pathError(const QString &path);

// Arguments for the preparation paths in Settings: inspect for the
// deterministic instance without implicitly creating it, then either start it
// or create it from the default Lima image with only the workspace, its linked
// Git metadata, and ForkMesh's worktree root writable in the guest.
QStringList listArguments();
QStringList startArguments();
QStringList createArguments();

// Wrap a provider process with `limactl shell`. stdin/stdout remain pipes, so
// the Claude stream-json and Codex app-server protocols work unchanged.
LaunchCommand isolateCommand(const QString &program,
                             const QStringList &arguments,
                             const QString &workingDirectory,
                             bool preserveEnvironment = true);

// Limit Lima's --preserve-env bridge to the credentials and agent controls
// ForkMesh deliberately put in the provider environment. Host PATH, HOME,
// desktop sockets, SSH agents, and unrelated secrets stay outside the guest.
void applyGuestEnvironmentPolicy(QProcessEnvironment &environment);

// Shell-quoted command line for TerminalWidget, which runs under a PTY. This is
// used both for the Settings VM shell and the Agents Claude/Codex terminals.
QString interactiveCommand(const QString &program = QString(),
                           const QString &workingDirectory = QString(),
                           const QStringList &arguments = QStringList());

} // namespace forkmesh::vm
