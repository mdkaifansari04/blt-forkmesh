---
schema: forkmesh-pull-v1
number: 14
title: feat: support pasted images in issue markdown
base: main
head: independent-v2-20260625-142017/04-markdown-image-paste
status: open
ts: 1782378602144
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: n7IKWEDECbytPK9cLG0347NiN7fSSrZd6jhGVJ-ogKeTEzBg_qgiztBbrZapJND9XVdv05oXCb8q0cXRTprFCA
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
- #23 / #45

Summary:
- Supports directly pasted images in issue markdown editing.

Commits included:
- 613ef37 feat: support pasted images in issue markdown

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

