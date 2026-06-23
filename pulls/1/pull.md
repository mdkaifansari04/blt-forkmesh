---
schema: forkmesh-pull-v1
number: 1
title: fix: re-clone when a mirror update can't fast-forward
base: main
head: issue-185-mirror-update
status: open
ts: 1782256760587
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
sig: 2YdFt_bGg9-hQH8ADyEKFDbem0EMn7tVzbXdMUkPQPDTOUKvCMMuyuVhgGQkmMeleaPoNSq-F1H-iCyi-2heDw
---

Updating from an install mirror ran `git pull --ff-only`, which aborts with "Not possible to fast-forward" when the local managed checkout has diverged from the mirror (or the baked-in origin is unreachable), leaving the update dead with no recovery (issue #185).

This adds an optional onFailure callback to runUpdateStep/runUpdateStepUser and uses it on the `pull --ff-only` step: on failure it drops the stale checkout and re-clones a fresh copy from the freshly resolved live mirror, then builds and relaunches -- matching what install.sh already does. The no-checkout path now shares the same re-clone helper.

Verify: build qt_client; create a divergence in the managed checkout (~/.local/share/ForkMesh/ForkMesh/src) and trigger update+rebuild+restart. The log shows the failed fast-forward, then "Could not fast-forward; re-cloning a fresh copy...", a clean clone, and a rebuild.
