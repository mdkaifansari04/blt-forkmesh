---
schema: forkmesh-issue-v1
number: 354
title: Opt-in crash and stall telemetry upload
status: closed
labels: [infra]
milestone: MVP launch
priority: 5
progress: 0
assignees: [Claude Code]
createdAt: 1783116818253
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-354
ts: 1783116818253
attachments: []
sig: 0L9sCNJh9wmxoLe91TG5AacjPNUQdDc9A3X5obeCXj6-ZAcXwjWQWH6xTbIy4eBd8U36FHTy7HoDAiW08hP-CQ
---

**Roadmap Phase 1.** Bugs users hit must reach a triage queue instead of dying in a local log.

- Client: `qt_client/src/CrashHandler.cpp` already captures crashes and `StallWatchdog` writes `~/.forkmesh/diagnostics/stalls.log`. Add a Settings toggle (`MainWindowSettings.cpp`), **default OFF**. When enabled, on startup POST the previous session's crash summary and stall records to the mainnode.
- Worker: new `POST /api/telemetry` route in `cloudflare_worker/src/entry.py`; aggregate counters in a Durable Object (cap payload size hard — the isolate is small and this must never become an outage vector).
- Privacy: send app version, OS, and an anonymized node hash only. Scrub repo names and filesystem paths client-side before the payload is built.

Surfacing can start as a JSON admin endpoint; a dashboard panel can follow.
