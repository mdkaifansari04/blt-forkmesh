---
schema: forkmesh-pull-v1
number: 4
title: feat(public): polish GitHub-like dashboard profile UI
base: main
head: api-pr/20260710-002121/dashboard-profile-ui
status: open
ts: 1783623402353
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: cbwjcqUXVHx01VmwehCmbknzgypBW5Iik2Dc7XsrgnNocbVrHNd-nSsS0Ls5OQPVu42jlzhm5-56IwKr2u4iAQ
---

Summary:
- Polishes the dashboard/profile shell into the GitHub-like layout from the redesign branch.
- Updates dashboard fragments, repository/profile UI behavior, mobile/sidebar chrome, and login avatar sync.
- Keeps the network rail drawer-only and updates dashboard/auth/profile frontend contracts.

Test Plan:
- git diff --check origin/main..HEAD
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- Ad-hoc Python runner for dashboard/auth/profile frontend contract tests

Merge note:
- Best reviewed after the profile follow API PR because the UI renders the new profile metadata when available.
- Submitted as patch-only because the 5-field payload with commits.mbox hit Cloudflare/Worker 1101; Worker accepts the 4-field PullStore-compatible signature form.
- Built from local origin/main because fetch from forkmesh.com returned a repository integrity-check failure.
