---
schema: forkmesh-issue-v1
number: 234
title: Branch port/issue-154-reference-links — needs merge into main
status: closed
labels: [feature]
milestone: 
priority: 9
progress: 0
assignees: []
createdAt: 1782356524605
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-234
ts: 1782356524605
attachments: []
sig: -qwtXlBXuhgcC6cqaiQJK7Jd6GvO63nWQ1MR3vr4ayarOyhAqbAov-BWAy1oyxwMyzRZINdqVcXl-QY8vhpODA
---

Tracking issue for the **`port/issue-154-reference-links`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Reference links between issues, PRs, and commits (#123 mentions become links; paste a PR/commit to link it).

**Why this priority:** Implements issue #154. Should land together with / supersede manual work on #154.

**Related:** Implements #154.

**Branch state (snapshot 2026-06-24):**
- Tip: `da0593d Merge branch 'main' into port/issue-154-reference-links`
- vs `main`: 614 ahead, 712 behind (counts inflated by ForkMesh sync churn).
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
