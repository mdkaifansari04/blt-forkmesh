---
schema: forkmesh-pull-v1
number: 51
title: test: refresh Office world mobile snapshots
base: api-pr/20260725011143/office-meetings-code
head: api-pr/20260725011143/office-world-mobile-snapshots
status: closed
ts: 1784923300134
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: amMuTnn2oR42jr7SuFm5zXe7Jyfwmdgpr_DMoUUT7aY99LEkIgqc1RWFF49ZvhzU1YZnBwZDNb7F8U6a5RNODQ
---

Refreshes the landscape and portrait browser snapshots for the Office world entry point.

Dependency:
- Apply after: api-pr/20260725011143/office-meetings-code

Verification:
- git diff --check api-pr/20260725011143/office-meetings-code..api-pr/20260725011143/office-world-mobile-snapshots
- Reconstructed proof tree matches feat/chat-world exactly when all snapshot asset PRs are applied.

Payload note: patch-only signed payload to avoid duplicating binary snapshots in commits.mbox.
