---
schema: forkmesh-issue-v1
number: 236
title: Branch port/repo-mirrors-health — needs merge into main
status: closed
labels: [infra]
milestone: 
priority: 11
progress: 0
assignees: []
createdAt: 1782356524859
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-236
ts: 1782356524859
attachments: []
sig: -HraMGOcujWa8z5XJrMMCbXSs0u-YIFfUKDtrGlWg1Cgn1Gac1yDuXR5h9pWnMWuUgnPjYCONc0x17YgrHGbAA
---

Tracking issue for the **`port/repo-mirrors-health`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Repository mirror health view (surface mirror freshness / failures).

**Why this priority:** Core observability for a mirror-based federation; helps diagnose stale peers (see #223).

**Related:** Related to #223.

**Branch state (snapshot 2026-06-24):**
- Tip: `4cb418d Merge branch 'main' into port/repo-mirrors-health`
- vs `main`: 617 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
- `cloudflare_worker/src/entry.py`
- `qt_client/src/MainWindow.cpp`
- `qt_client/src/MainWindow.h`
- `qt_client/src/Theme.h`

**ForkMesh metadata conflicts (mechanical — take the union of signed events / newer side):**
- `AGENTS.md`
- `issues/130/issue.md`
- `issues/191/issue.md`
- `issues/192/issue.md`
- `issues/193/issue.md`
- `issues/194/issue.md`
- `pulls/1/changes.patch`
- `pulls/1/pull.md`
- `pulls/2/changes.patch`
- `pulls/2/commits.mbox`
- `pulls/2/pull.md`
- `pulls/5/changes.patch`
- `pulls/5/pull.md`

**Recommendation:** merge (or rebase) `main` into the branch, resolve the code conflicts above, rebuild, then open a ForkMesh PR. The metadata conflicts are signed append-only logs — keep both sides' events.
