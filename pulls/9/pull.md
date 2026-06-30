---
schema: forkmesh-pull-v1
number: 9
title: Worker: dashboard profile, notifications, and signup polish
base: main
head: kaif/split-full-relay-20260701-000610/worker-dashboard-account
status: open
derive: branch
ts: 1782844686747
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: parc5q4d0bfoVgDLOCtke2GzVjLUdeZvFvoBosNosvRiCctN86v2RKiJGCHtRYFyA_HKPMbXZdwvp6Pzdzb_Dg
---

Independent rescue PR rebuilt from the full fix/local-relay-connect branch.

Source full branch: kaif/full-fix-local-relay-connect-pr-20260630-235732
Rebuilt branch: kaif/split-full-relay-20260701-000610/worker-dashboard-account
Head SHA: c11f4a055dbc3af38c225902f8b699dc8bd437aa
Commit count: 2
Validation gate: worker
Payload mode: branch-backed (diff/commits reconstructed from base..head).
This split is intentionally independent from the other rescue PRs; it was rebuilt from origin/main, not stacked.

Resolved against current main: the author's commit was replayed with `git am --3way`;
the only conflict (cloudflare_worker/public/dashboard.js) was taken from this PR's
version, which supersedes issue #270's open-issues-by-default with its own
`is:issue is:open` default. A follow-up commit relaxes the favicon metadata test to
accept the dashboard's new light/dark color-scheme.

