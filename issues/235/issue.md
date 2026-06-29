---
schema: forkmesh-issue-v1
number: 235
title: Branch port/issue-175-auto-publish — needs merge into main
status: closed
labels: [feature]
milestone: 
priority: 10
progress: 0
assignees: []
createdAt: 1782356524736
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-235
ts: 1782356524736
attachments: []
sig: DHi53RGlumodTFItbfKZIT3Lu_C7NTArImhHp0Ngux9HhJiA0n5k-VHlt6vq7kKcpqrRX_T7NdJwcgiU4IojCQ
---

Tracking issue for the **`port/issue-175-auto-publish`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Per-repository auto-publish setting (publish/mirror automatically when clean).

**Why this priority:** Related to #193 (auto-push new issues when clean). Decide whether these two unify into one setting.

**Related:** Related to #193.

**Branch state (snapshot 2026-06-24):**
- Tip: `71d55d8 feat: add repository auto-publish setting`
- vs `main`: 581 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
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
