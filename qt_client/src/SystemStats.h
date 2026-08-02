#pragma once

#include <QString>
#include <QtGlobal>

// Lightweight, dependency-free sampling of THIS process's resource usage. Added
// for issue #287 so a durable headless daemon can show how much CPU and memory
// it is consuming while it serves its mirrors. Linux reads /proc/self; other
// Unix falls back to getrusage(). Every metric is best-effort: an unavailable
// value returns 0 (bytes) or a negative percentage.
namespace SystemStats {

// Resident set size of this process in bytes (0 if unknown).
qint64 residentBytes();

// Total physical RAM on the host in bytes (0 if unknown).
qint64 totalMemoryBytes();

// Physical RAM the host can still hand out without swapping, in bytes (0 if
// unknown). Linux reads MemAvailable from /proc/meminfo (the kernel's own
// estimate, better than free+cached); other Unix falls back to free pages.
// Host used RAM == totalMemoryBytes() - availableMemoryBytes().
qint64 availableMemoryBytes();

// Size / free space of the filesystem that holds `path`, in bytes (0 if the
// path can't be stat'd). Used space == diskTotalBytes() - diskFreeBytes().
qint64 diskTotalBytes(const QString &path);
qint64 diskFreeBytes(const QString &path);

// Whole-host CPU utilization since the previous call, as a percentage across
// all cores (0..100; 100 == every core fully busy). Like cpuPercent() the first
// call establishes a baseline and returns 0.0; later calls report the busy
// fraction since the call before. Returns -1.0 when the platform exposes no
// system-wide CPU accounting. State is process-global, so call it from a single
// place (the mirrors heartbeat) for a meaningful reading.
double hostCpuPercent();

// CPU used by this process since the previous call, as a percentage of one core
// (100.0 == one core fully busy; >100 on multi-core). The first call has no
// interval to average over and returns 0.0, establishing the baseline; each
// later call reports the busy fraction since the call before it. Returns -1.0
// when the platform exposes no CPU accounting. State is process-wide, so call it
// from a single place (the mirrors view) for a meaningful "recent load" reading.
double cpuPercent();

// Human-readable byte size, e.g. "134.0 MB" / "1.62 GB" ("0 B" for <= 0).
QString formatBytes(qint64 bytes);

// How much of the host a process subtree's same-named workers are using.
struct DescendantLoad {
    int count = 0;             // matching processes below the root
    qint64 residentBytes = 0;  // their combined RSS
};

// Counts the processes in `rootPid`'s subtree (children, grandchildren, …) whose
// command name is exactly `comm` — e.g. how many `cc1plus` compilers a running
// agent's build has spawned (adhoc #57) — and sums their resident memory. Linux
// only (walks /proc); returns an empty summary elsewhere or when rootPid <= 0.
DescendantLoad descendantsNamed(qint64 rootPid, const QString &comm);

// --- File descriptors -------------------------------------------------------
// Running out of descriptors is not a graceful failure here: glib calls g_error
// (fatal, SIGTRAP) the instant it cannot create the wakeup pipes for a new
// GMainContext, so a thread start turns into an unexplained crash inside
// g_main_context_new_with_flags. A long-lived node holds a lot of them — a
// socket per mainnode/event link, pipes per git child, inotify, an eventfd per
// thread — against a soft cap that is still 1024 on most distributions.

// Descriptors this process currently has open, including the one used to take
// the sample. 0 when the platform does not expose them (Linux reads
// /proc/self/fd).
int openFileCount();

// Current soft / hard caps on that count, 0 when unknown or unlimited.
int openFileSoftLimit();
int openFileHardLimit();

// Threads in this process (1 if unknown). Reported alongside the descriptor
// count because each live thread pins an event-dispatcher wakeup pipe.
int threadCount();

// Raise the soft descriptor cap to the hard cap (bounded by a sane target) and
// return the resulting soft cap. Call once at startup, before Qt spins up any
// thread. Children inherit the raised cap; workflow steps get their own,
// deliberately tighter RLIMIT_NOFILE from ActionRunner.
int raiseOpenFileLimit();

} // namespace SystemStats
