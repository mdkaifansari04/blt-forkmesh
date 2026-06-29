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

} // namespace SystemStats
