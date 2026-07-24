#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// Optional launch "jail" for agent processes (adhoc #236). When enabled, every
// agent starts in its own scratch environment — a private per-session TMPDIR
// and cache directory instead of the shared system ones — with a memory cap
// applied through the shell before the CLI is exec'd. It's a resource fence,
// not a security sandbox: the agent still sees the filesystem, but it can't
// grow past the cap or leak scratch state across sessions.
namespace AgentJail {

// Prefix `command` (a `bash -lc` payload) so the agent runs with its data
// memory capped at memoryMb megabytes. Uses `ulimit -d` (RLIMIT_DATA), which
// on Linux ≥4.7 covers anonymous mmap/brk allocations — i.e. real memory use —
// while leaving huge *reserved* address-space regions alone (`ulimit -v` would
// kill Node/V8 outright, which reserves multi-GB virtual ranges it never
// backs). The rlimit is inherited by every child the shell execs, so the cap
// applies per process across the agent and its subprocesses. Shells that
// reject the flag just run uncapped thanks to the redirect. memoryMb <= 0
// returns the command unchanged (jail off).
inline QString wrapCommand(const QString &command, int memoryMb)
{
    if (memoryMb <= 0 || command.trimmed().isEmpty())
        return command;
    const qint64 kb = qint64(memoryMb) * 1024; // ulimit takes kilobytes
    return QStringLiteral("ulimit -d %1 2>/dev/null; %2")
        .arg(QString::number(kb), command);
}

// "KEY=VALUE" entries pointing the agent's scratch state (TMPDIR/TMP/TEMP and
// the XDG cache) into `jailDir`, creating the directories. The format matches
// the extraEnv convention of ClaudeStreamSession/CodexAppServerSession.
inline QStringList envEntries(const QString &jailDir)
{
    const QString tmp = jailDir + QStringLiteral("/tmp");
    const QString cache = jailDir + QStringLiteral("/cache");
    QDir().mkpath(tmp);
    QDir().mkpath(cache);
    return {QStringLiteral("TMPDIR=") + tmp, QStringLiteral("TMP=") + tmp,
            QStringLiteral("TEMP=") + tmp,
            QStringLiteral("XDG_CACHE_HOME=") + cache};
}

// Per-session jail directory for transcript (stream) sessions, kept under the
// system temp location beside the worktrees so it never pollutes the repo.
inline QString sessionJailDir(int sessionId)
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/forkmesh-agent-jails/s%1").arg(sessionId);
}

} // namespace AgentJail
