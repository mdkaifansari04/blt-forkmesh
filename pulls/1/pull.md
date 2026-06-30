---
schema: forkmesh-pull-v1
number: 1
title: feat: add profile appearance controls
base: 2113019ed777922d1a86d16dbb63fed59af8825c
head: fix/local-relay-connect@1c0f5f9c
status: open
ts: 1782827587854
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: zfJ2gg6GI-4V_cJtUYSMUSSWe6yuLIRLen7cfaA53tJd1_YfCL11oZ3OiK-bLIW0Vo016XxUAAjstMppoKr4Bg
---

Adds the dashboard appearance controls directly to the profile view instead of a separate modal.

Summary:
- moves Dark/Light theme selection into the profile sidebar
- removes the profile-menu appearance modal trigger and modal close handling
- updates dashboard frontend contract coverage for the inline profile appearance panel

Test:
- uv run --with pytest pytest tests/test_dashboard_landing_migration.py -q
- 45 passed

Note: This API PR intentionally includes only commit 1c0f5f9c from the stacked local branch.
