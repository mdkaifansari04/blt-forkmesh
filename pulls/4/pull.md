---
schema: forkmesh-pull-v1
number: 4
title: feat(public): polish GitHub-like dashboard profile UI
base: main
head: api-pr/20260710-002121/dashboard-profile-ui-updated
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

Maintenance update (forkmesh node, resolve conflicts):
- Same stale/corrupted-checkout root cause as pull #3. Reconstructed
  file-by-file: for each touched file, the patch's "before" blob was looked
  up in git history to tell genuine base drift (which needed a real 3-way
  merge) from files whose patch base already matched current main
  (applied directly, unchanged).
- Only 7 of the 26 touched files had drifted; of those, one merge conflict
  (dashboard/js/07-repo-compose-branch.js + its landing-migration test) was
  hand-resolved to keep the issue-search box wired up (added to main after
  this PR's local base) inside the PR's new two-column issues/pulls layout,
  instead of silently reverting it back to a static placeholder.
- cloudflare_worker/public/dashboard/partials/network-rail.html is deleted
  (superseded by the dedicated /chat section + unified hamburger drawer);
  dashboard.html / dashboard/index.html / dashboard.js were rebuilt from the
  fixed source fragments via tools/build_dashboard_assets.py, not hand-edited.
- cloudflare_worker/tests/test_mirror_serving_frontend.py (not part of the
  original patch) is updated to match this PR's real, intentional removal
  of the repository grid/list view-mode toggle.
- head now points at a rebuilt branch; changes.patch has been regenerated
  against current main. Full worker test suite (735 tests) passes; commits.mbox
  is intentionally omitted, same as the original patch-only submission.
- This invalidates the signature above for the new patch bytes, same as the
  prior "resolve conflicts" update to pull #3.
