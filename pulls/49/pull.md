---
schema: forkmesh-pull-v1
number: 49
title: test: add Office world browser snapshots
base: api-pr/20260725011143/office-meetings-code
head: api-pr/20260725011143/office-world-new-snapshots
status: closed
ts: 1784923300065
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: w5g5-w68q_7F2tvXvAwmpFnPLogrGYg43RFJZ__p7Ke6ucrgptbX13uPIYgIttQRbe5N3z7zpYnpIPLohw7ECw
---

Adds browser snapshot PNGs for the new Office world views.

Dependency:
- Apply after: api-pr/20260725011143/office-meetings-code

Verification:
- git diff --check api-pr/20260725011143/office-meetings-code..api-pr/20260725011143/office-world-new-snapshots
- Reconstructed proof tree matches feat/chat-world exactly when all snapshot asset PRs are applied.

Payload note: patch-only signed payload to avoid duplicating binary snapshots in commits.mbox.
