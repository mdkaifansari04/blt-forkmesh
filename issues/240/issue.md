---
schema: forkmesh-issue-v1
number: 240
title: Branch port/clipboard-image-paste — needs merge into main
status: closed
labels: [enhancement]
milestone: 
priority: 14
progress: 0
assignees: []
createdAt: 1782356525339
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 2.00
bountyAddress: 
bountyStatus: open
type: open
id: open-240
ts: 1782356525339
attachments: []
sig: WbwigKG93SeOtNEC36ZgNw_BDUukneiMi69K7Q41ebQR7rrofpSmaBtB1qtnmYpxksgCgNH09LWiy5FCkVjIAg
---

Tracking issue for the **`port/clipboard-image-paste`** feature branch (auto-created so its work can be prioritized alongside the issue backlog).

**Summary:** Paste clipboard images directly into markdown editors.

**Why this priority:** Nice editor UX; touches MarkdownEditor.cpp. Low risk once conflicts resolved.

**Branch state (snapshot 2026-06-24):**
- Tip: `671d465 Merge branch 'main' into port/clipboard-image-paste`
- vs `main`: 615 ahead, 712 behind (counts inflated by ForkMesh sync churn).
- Local and `origin` copies are in sync (0 ahead / 0 behind).
- Merge into `main`: **CONFLICT** — must be resolved before it can land.

**Code conflicts vs main (need real review):**
- `qt_client/src/MainWindow.cpp`
- `qt_client/src/MainWindow.h`
- `qt_client/src/MarkdownEditor.cpp`
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
