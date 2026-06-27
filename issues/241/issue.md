---
schema: forkmesh-issue-v1
number: 241
title: Branch port/pr-review-experience — needs merge into main
status: closed
labels: [feature]
milestone: 
priority: 15
progress: 0
assignees: []
createdAt: 1782356615737
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-241
ts: 1782356615737
attachments: []
sig: QGYzIqU_SZNUyXCo5CWHicBJ3AnRUDehUl__TuM2Yl_9vH8bq2MpnNNjislVISm9cOH_KnmOERFhjSAAbPMTBw
---

Tracking issue for the **`port/pr-review-experience`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** PR review experience: review-summary UI plus signed pull-review thread events.

**Why this priority:** Advances the PR review/merge workflow needed for launch; the branch already folds signed review-thread state and adds a pull review summary UI.

**Branch state (snapshot 2026-06-24):**
- Tip: `208983d feat: add pull review summary ui`
- vs `main`: 583 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
- `flutter_app/lib/services/inbox_service.dart`
- `qt_client/src/MainWindow.cpp`
- `qt_client/src/MainWindow.h`
- `qt_client/src/Theme.h`
- `qt_client/tests/test_window_resize.cpp`

**ForkMesh metadata conflicts (mechanical — keep the union of signed events):**
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

**Recommendation:** merge (or rebase) `main` into the branch, resolve the code conflicts above, rebuild, then open a ForkMesh PR.
