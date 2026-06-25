---
schema: forkmesh-pull-v1
number: 11
title: fix: prevent issue deletion from freezing the app
base: main
head: independent-v2-20260625-142017/01-issue-141-delete-freeze
status: merged
ts: 1782378602029
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: vgjamB_xT3Oedby85T47MUZj2FidHKcFcOXJQ4n631Tyhv5L10Pi6KhuSA87W1TRHK51XMOyHsPi71yYk9klBA
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
- #141

Summary:
- Prevents issue history deletion from freezing the Qt app.

Commits included:
- 8caf0b6 fix: guard issue history deletion

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.
