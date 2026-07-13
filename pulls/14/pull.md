---
schema: forkmesh-pull-v1
number: 14
title: Add native profile contributions API and history schema
base: main
head: api-pr/20260714-035346/worker-profile-contributions-api
status: open
ts: 1783981690105
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: X_oY_7d4v0M6Ee1X9vzGU_ILuacblqCJrLQyDRhy2QWgI9aupxk3-HI2pVQDx2bX5Jw89Zv6PWuc7cXm2nS6AA
---

Adds Worker-side native profile contribution tracking based on local-first repository activity.

Includes the contribution schema/migration, profile contribution API helpers, catalog integration, and focused endpoint/private-repo tests.

Submitted as a lightweight signed API PR because the full feature branch is large. Based on local main f760e13 because fetching origin/main failed with a ForkMesh mirror integrity check error, so remote freshness could not be proven.
