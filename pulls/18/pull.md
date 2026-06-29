---
schema: forkmesh-pull-v1
number: 18
title: feat: add repository mirror health view
base: main
head: independent-v2-20260625-142017/09-repo-mirrors-health
status: open
ts: 1782378602287
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: J4lULU-AJ_R400sDdv6QQb64AKKD9gDQ8thyjbjhShI8jE71gNIaPdNxi8gimdNm3-RxcKwYWpPGNXvSA-INDQ
---

This is an independent ForkMesh rescue PR rebuilt from the meshed local main stack.

Independence note:
- Base label: main
- This PR was cherry-picked cleanly onto the shared local baseline.
- It does not intentionally depend on any other rescue PR being merged first.
- If another independent PR touching the same files lands first, normal reviewer conflict resolution may still be needed, but this patch is not part of a stacked dependency chain.

Placement:
- High-impact feature addition

Issue mapping:
- #27 / #137 / #174 / #185

Summary:
- Adds repo mirror health API coverage, Worker endpoint hardening, Qt mirrors tab, and responsive repo tab polish.

Commits included:
- 720d77f docs: design repo mirrors health tab
- 56266bc docs: plan repo mirrors health implementation
- f47302c test: cover repo mirror health payload
- 0825295 feat: expose repo mirror health API
- 405b6e9 feat: add repo mirrors tab
- 657059c fix: keep repo tab content before sidebar on mobile
- d449eae fix: harden repo mirrors endpoint and loader

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

