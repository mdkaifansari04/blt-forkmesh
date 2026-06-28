---
schema: forkmesh-issue-v1
number: 238
title: Branch port/private-repo-sharing-ui — needs merge into main
status: closed
labels: [feature]
milestone: 
priority: 13
progress: 0
assignees: []
createdAt: 1782356525099
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-238
ts: 1782356525099
attachments: []
sig: nj82YzCj8O5IJXQuLVzli4cF-a6pj-rbTpjydrdN3khRcWB7LuwXb38wFORnmDdTkcCTzbsiXYg0z2n2vUrdCA
---

Tracking issue for the **`port/private-repo-sharing-ui`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** UI for sharing private repositories with selected nodes.

**Why this priority:** Adds private-repo sharing controls plus a worker test (cloudflare_worker/tests/test_share_repos.py).

**Branch state (snapshot 2026-06-24):**
- Tip: `4b6027b Merge branch 'main' into port/private-repo-sharing-ui`
- vs `main`: 614 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
- `cloudflare_worker/tests/test_share_repos.py`
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
