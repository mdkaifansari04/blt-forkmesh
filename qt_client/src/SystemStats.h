#pragma once

#include <QString>
#include <QtGlobal>






namespace SystemStats {


qint64 residentBytes();


qint64 totalMemoryBytes();





qint64 availableMemoryBytes();



qint64 diskTotalBytes(const QString &path);
qint64 diskFreeBytes(const QString &path);







double hostCpuPercent();







double cpuPercent();


QString formatBytes(qint64 bytes);


struct DescendantLoad {
    int count = 0;
    qint64 residentBytes = 0;
};





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
