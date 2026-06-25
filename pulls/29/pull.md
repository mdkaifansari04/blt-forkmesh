---
schema: forkmesh-pull-v1
number: 29
title: fix: normalize terminal window log output
base: main
head: independent-v2-20260625-142017/08-window-log-output
status: open
ts: 1782378602254
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: -oQ6_jgVGmhu8iMvBplOGH3mAghBp9GBheuuu9g6uSAwPND3UyDrfqyoHihN53n8mDaJMKFWMyxzzB_qz8r_DQ
---

This is an independent ForkMesh rescue PR rebuilt from the meshed local main stack.

Independence note:
- Base label: main
- This PR was cherry-picked cleanly onto the shared local baseline.
- It does not intentionally depend on any other rescue PR being merged first.
- If another independent PR touching the same files lands first, normal reviewer conflict resolution may still be needed, but this patch is not part of a stacked dependency chain.

Placement:
- Concrete bug fix

Issue mapping:
- #149

Summary:
- Normalizes terminal/window log output formatting.

Commits included:
- 4be2ac7 fix: normalize window log output

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

