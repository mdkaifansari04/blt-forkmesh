---
schema: forkmesh-pull-v1
number: 48
title: test: add Office meeting browser snapshots
base: api-pr/20260725011143/office-meetings-code
head: api-pr/20260725011143/office-meeting-snapshots
status: closed
ts: 1784923300043
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: wk0a5nWaXM93pD9Z92b5Lk_D5HRySOyQrjsh8jGoepnzmdq68mtE6up0meYenb-uZYss0Q6nFVGgSUbOqipxAw
---

Adds browser snapshot PNGs for the multiplayer Office meeting journeys.

Dependency:
- Apply after: api-pr/20260725011143/office-meetings-code

Verification:
- git diff --check api-pr/20260725011143/office-meetings-code..api-pr/20260725011143/office-meeting-snapshots
- Reconstructed proof tree matches feat/chat-world exactly when all snapshot asset PRs are applied.

Payload note: patch-only signed payload to avoid duplicating binary snapshots in commits.mbox.
