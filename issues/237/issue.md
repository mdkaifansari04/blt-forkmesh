---
schema: forkmesh-issue-v1
number: 237
title: Branch port/polish-repo-actions-tab — needs merge into main
status: closed
labels: [enhancement]
milestone: 
priority: 12
progress: 0
assignees: []
createdAt: 1782356524976
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-237
ts: 1782356524976
attachments: []
sig: MwvxRac43oQF3BjPvGC3XvoFKxNd1m0mEdzdFdlpkYzVIBKb8S0RSyZeXmEt5ocNDTihpR-UY_YECGOFWVlYAw
---

Tracking issue for the **`port/polish-repo-actions-tab`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Visual polish of the repository Actions tab.

**Why this priority:** Pure UI polish; low risk, land after higher-value features.

**Branch state (snapshot 2026-06-24):**
- Tip: `aceec87 Merge branch 'main' into port/polish-repo-actions-tab`
- vs `main`: 615 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
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
