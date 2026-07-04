---
schema: forkmesh-issue-v1
number: 353
title: Eliminate remaining GUI-thread git calls (zero StallWatchdog events in normal use)
status: closed
labels: [enhancement]
milestone: MVP launch
priority: 4
progress: 0
assignees: [Claude Code]
createdAt: 1783116818252
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-353
ts: 1783116818252
attachments: []
sig: kEgA28bHeZtpt1SvV-KU5F0WFZU2mrw18hBejt3TRsjlMZscJzrUHw6k2ceZ6CWjtwFDtmy87vKPwbUfyz9fBw
---

**Roadmap Phase 1.** Every UI freeze we've debugged traced back to synchronous git on the GUI thread. The instrumentation already exists — finish the job.

- `qt_client/src/StallWatchdog.cpp` + `noteBlockingCall(...)` (`StallWatchdog.h:16`) name the slow command in each freeze; `~/.forkmesh/diagnostics/stalls.log` is the hotspot ledger (filter out window-tests records).
- The `runOffThread` helper (used in `RepoHost.cpp`, `MainWindowAgents.cpp`, `MainWindowSourceControl.cpp`) is the target pattern. `GitKeepAlive` (`qt_client/src/MainWindowInternal.h` ~5836) pumps the event loop for legacy paths — acceptable as a stopgap, but pumping invites re-entrancy; prefer moving the work off-thread.

Task: grep `MainWindow*.cpp` for `waitForFinished` and synchronous git invocations reachable from slots; convert each to `runOffThread` with a completion callback, or a skip-when-unchanged fast path. Acceptance: opening every repo-detail tab on a large repo produces zero stall records over 200 ms.
