---
schema: forkmesh-pull-v1
number: 3
title: Fix Qt startup without account login
base: main
head: fix/qt-client-startup-branch-name
status: open
ts: 1782218616802
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 8t0mLAeC5a4RAlndTltWWoUNi_rnCVnZX6nDCDeknnyLh9hP3nLbFlpNWukYnLpeVA0ZAHG1gnhC-WZiknRwAA
---

## Summary
- Allow Start to open the app shell without forcing account login/signup first.
- Keep Join the network on the authenticated account flow.
- Block publishing/host startup until an active account session exists.
- Make quick update pull origin/current-branch when a branch has no upstream.

## Test Plan
- cd qt_client && ./run.sh test
