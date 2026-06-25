---
schema: forkmesh-pull-v1
number: 24
title: feat: add relay federation deployment hardening
base: main
head: independent-v2-20260625-142017/02-relay-federation-hardening
status: open
ts: 1782378602060
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: rElg8asWtkqsBbhlpL_PMaxZos6PGIfctsC8yeiTBa_pifWbS7kYz8fpLym04wdU_2P00ixtfgQ0T9i7XICeDA
---

This is an independent ForkMesh rescue PR rebuilt from the meshed local main stack.

Independence note:
- Base label: main
- This PR was cherry-picked cleanly onto the shared local baseline.
- It does not intentionally depend on any other rescue PR being merged first.
- If another independent PR touching the same files lands first, normal reviewer conflict resolution may still be needed, but this patch is not part of a stacked dependency chain.

Placement:
- Concrete feature addition / closed-issue refinement

Issue mapping:
- #130

Summary:
- Adds relay federation deployment hardening. Issue closure metadata is excluded from this independent code PR.

Commits included:
- cba9c41 feat: harden relay federation deployment

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

