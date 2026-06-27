---
schema: forkmesh-issue-v1
number: 239
title: Branch port/durable-discussions — needs merge into main
status: closed
labels: [feature]
milestone: 
priority: 4
progress: 0
assignees: []
createdAt: 1782356525227
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-239
ts: 1782356525227
attachments: []
sig: BjYvPtsounGGoWi8MoET_t5uTEF9IYgXMREnhh58sNusUFa9tAdTa7dAPRsFYv_Kj70ratyaAHghWu5NdzQHDQ
---

Tracking issue for the **`port/durable-discussions`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Durable repository discussions (persisted discussion threads).

**Why this priority:** Adds a discussions feature with a worker inbox; note one commit already backs off an unsupported discussion inbox path.

**Branch state (snapshot 2026-06-24):**
- Tip: `8391c28 fix: back off unsupported discussion inbox`
- vs `main`: 582 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
- `cloudflare_worker/src/entry.py`
- `qt_client/src/MainWindow.cpp`
- `qt_client/src/MainWindow.h`
- `qt_client/src/Theme.h`
- `qt_client/tests/test_window_resize.cpp`

**ForkMesh metadata conflicts (mechanical — take the union of signed events / newer side):**
- `AGENTS.md`
- `issues/130/issue.md`
- `pulls/1/changes.patch`
- `pulls/1/pull.md`
- `pulls/2/changes.patch`
- `pulls/2/commits.mbox`
- `pulls/2/pull.md`
- `pulls/3/changes.patch`
- `pulls/3/pull.md`
- `pulls/5/changes.patch`
- `pulls/5/pull.md`

**Recommendation:** merge (or rebase) `main` into the branch, resolve the code conflicts above, rebuild, then open a ForkMesh PR. The metadata conflicts are signed append-only logs — keep both sides' events.
