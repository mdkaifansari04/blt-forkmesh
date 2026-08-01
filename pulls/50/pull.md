---
schema: forkmesh-pull-v1
number: 50
title: test: refresh Office world desktop snapshot
base: api-pr/20260725011143/office-meetings-code
head: api-pr/20260725011143/office-world-desktop-snapshot
status: closed
ts: 1784923300103
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: phKEnnHuY2YnqctfE6k8qEiOrqkFc3cpcjzX6EVf_LZ2-nsWIvS1wD864hT9YttpjVGNBi1Cnw3vUeIe5bqBAQ
---

Refreshes the desktop browser snapshot for the Office world entry point.

Dependency:
- Apply after: api-pr/20260725011143/office-meetings-code

Verification:
- git diff --check api-pr/20260725011143/office-meetings-code..api-pr/20260725011143/office-world-desktop-snapshot
- Reconstructed proof tree matches feat/chat-world exactly when all snapshot asset PRs are applied.

Payload note: patch-only signed payload to avoid duplicating binary snapshots in commits.mbox.
