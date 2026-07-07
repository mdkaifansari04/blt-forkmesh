---
schema: forkmesh-pull-v1
number: 22
title: Ignore local worktrees
base: main
head: api-pr/20260708-001118/ignore-local-worktrees
status: open
ts: 1783449942788
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: vdy9lWZ6o65LW1P3Fah34VoAWqoz8EyRLcpq3cq4x6z9BQdpwIs61DG2Q2kqXj8cQ3m2wvGo61KcPdMQr3OrCQ
---

Adds the local worktree scratch directory to the Worker gitignore so temporary agent lanes stay untracked.

Test plan:
git diff --check

This ForkMesh PR was rebuilt independently from current origin/main and can be reviewed without the other local branch slices.
