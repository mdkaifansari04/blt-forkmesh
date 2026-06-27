---
schema: forkmesh-pull-v1
number: 38
title: Stop UI-thread git calls from freezing the agent window
base: main
head: agent/adhoc-50-fix-these-stalls-please-and-make-sure-the-agent
status: open
ts: 1782519404835
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: dev-2f782c1c
sig: NP6ALVhEpFOAL4lLXITOvmiva1nQFW_ChAoAPv-TH-83Rt5pqtnzVzzpLpXpn1eBRRyuN6aHP43OtfULrCYrDg
---

The StallWatchdog reported repeated multi-second event-loop stalls, all
ultimately blocked in `QProcess::waitForFinished()` (git) or layout run on
the GUI thread. This fixes the worst, most user-visible offenders so the
agent UI stays smooth to click through.

## Changes
- **Edited-files panel (streaming hot path).** `refreshAgentFilesPanel`
  ran a blocking `git diff --name-only` on *every* transcript event, which
  froze the window in ~1.5-2s bursts while an agent streamed. The
  in-memory tool-call files now render instantly; the working-tree diff is
  coalesced (400ms debounce) and run off the event loop.
- **Worktrees panel (worst stall, ~21s reported).** `loadWorktreesPanel`
  ran a synchronous `git status` per worktree row. Each row now shows a
  placeholder and fills its dirty/clean state in asynchronously; a
  generation tag drops stale callbacks when the table is rebuilt.
- Added a small reusable `runGitDetached()` helper (non-blocking QProcess
  that self-deletes and reports back on the main thread) backing both.
- **Better stall diagnostics.** A long stall is now re-sampled as it drags
  on (up to 6 distinct spots), so multi-phase blocks — back-to-back git
  calls, layout thrash — are attributed correctly instead of being blamed
  on whatever the single first sample happened to catch.

## Verify
- forkmesh-tests and forkmesh-window-tests pass (including the #272
  worktree-selection test that exercises the worktrees-panel rebuild).

