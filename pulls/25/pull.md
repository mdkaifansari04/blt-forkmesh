---
schema: forkmesh-pull-v1
number: 25
title: feat: link references and development pull requests
base: main
head: independent-v2-20260625-142017/03-reference-development-links
status: open
ts: 1782378602106
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: FJYyvcAvJws9oO6fuCgs7_kWovXOBj_5_AyV2ac6i3wUEYYZpn_sfGuOQ6pbzfQKhawcX5KrcQ3yQq75Zjd7DA
---

This is an independent ForkMesh rescue PR rebuilt from the meshed local main stack.

Independence note:
- Base label: main
- This PR was cherry-picked cleanly onto the shared local baseline.
- It does not intentionally depend on any other rescue PR being merged first.
- If another independent PR touching the same files lands first, normal reviewer conflict resolution may still be needed, but this patch is not part of a stacked dependency chain.

Placement:
- Concrete feature addition

Issue mapping:
- #154 / #156

Summary:
- Links issue, PR, commit, and pasted references in conversations.
- Adds Development section linking between issues and pull requests.

Commits included:
- 0e2ad34 fix: link references in conversations
- c0a2cb5 docs: design issue 156 development links
- fed12e8 fix: link development pull requests to issues

Verification:
- Clean cherry-pick onto baseline 9885c53.
- Signed payload generated with PullStore-compatible 5-field canonical string including commits mbox.

