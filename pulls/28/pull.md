---
schema: forkmesh-pull-v1
number: 28
title: feat: add repository auto-publish controls
base: main
head: independent-v2-20260625-142017/06-auto-publish-controls
status: open
ts: 1782378602218
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: w94_IuOIJff5uVPxuYGqtav1ZHL9AM_ICPRRMdVCMosUknJ-eM-eT3PAdtgr6MtLwig2lTkhdo5SPUAEyCvADg
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
- #175

Summary:
- Adds repository auto-publish controls/settings.

Commits included:
- da5c3d6 feat: add repository auto-publish setting

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

