---
schema: forkmesh-pull-v1
number: 17
title: Add Qt profile contribution snapshots and strict git reads
base: main
head: api-pr/20260714-035346/qt-profile-contribution-support
status: open
ts: 1783981717358
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: j21jsdYoCHpkxhXB-aVDvrZOex-KQtJwpxNp2tNzs9AEOUnYaJIGym0FeEZTk3nnb3McSqD_sNsPTM7PPHelAg
---

Adds Qt desktop support for profile contribution snapshots, strict git reads, account capability metadata, and contribution-aware UI wiring.

Includes CMake/test integration and passes the Qt headless test gate.

Submitted as a lightweight signed API PR because the full feature branch is large. Based on local main f760e13 because fetching origin/main failed with a ForkMesh mirror integrity check error, so remote freshness could not be proven.
