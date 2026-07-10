---
schema: forkmesh-pull-v1
number: 5
title: feat(public): polish GitHub-like dashboard profile UI
base: main
head: api-pr/20260710-012340/dashboard-profile-ui-current
status: closed
ts: 1783627484047
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 9V-i8vouNkkUaBqIemes8aMQWEcuUUwSwhIWErmH_MaIhvsmNB9VZAGQfsj-vUTc4LhXHfn99xJDu4_-z40rCw
---

Summary:
- Replacement for the earlier stale-base dashboard-profile UI PR.
- Reapplies the GitHub-like dashboard/profile UI polish on top of remote main b396ad69 content fetched through the ForkMesh read API.
- Preserves current-main network-rail.html instead of deleting it.
- Rebuilds dashboard.html, dashboard/index.html, and dashboard.js from merged fragments.

Test Plan:
- git diff --check synthetic-current-main..HEAD
- cd cloudflare_worker && python3 -m py_compile src/entry.py using a valid local entry.py because the read API truncates large entry.py blobs and this PR does not modify entry.py
- Direct static runner: 131 dashboard/auth/profile frontend contract tests passed

Note:
- Use this PR instead of the earlier stale-base UI PR api-pr/20260710-002121/dashboard-profile-ui.
- Submitted as patch-only to avoid oversized duplicated patch + commits.mbox payloads.

Closed (forkmesh node): superseded by pull #4, already merged.
- Compared every touched file's patch blob against current main: 18 of 25
  files are byte-identical to what pull #4 already delivered. The other 7
  (the generated dashboard.js bundle plus a few fragments/tests) target the
  same stale content pull #4's original patch had before it was corrected —
  applying this PR as-is would have re-reverted the issue-search box wiring
  in the repo issues/pulls panel and the repo-tab pending-counts badges,
  both of which are already fixed and tested on main.
- This PR's own stated difference from #4 ("preserves network-rail.html
  instead of deleting it") just leaves that partial as dead, unreferenced
  code; main's merged design already removes the network rail in favor of
  a dedicated /chat section, with tests covering the removal.
- No unique content survived comparison, so there is nothing to reconstruct
  here beyond what pull #4 already merged.
