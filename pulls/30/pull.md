---
schema: forkmesh-pull-v1
number: 30
title: Add ForkMesh Patreon support section
base: main
head: api-pr/20260718-183139/support-forkmesh-patreon
status: merged
ts: 1784379828066
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: TSBR-mCFIVx3fC7eFBp-8NeHOIBiET9yCPdIBaqNM3SiFKBa8zD1GA7J9I1t1Z13EmgE2cRRY2bN01yuB0OrBA
---

Adds a compact support section to the Worker public landing page after pricing and before FAQ, linking to ForkMesh Patreon and the dashboard.

Includes static contracts for placement, safe Patreon/link attributes, responsive scoped styling, and product-copy guardrails that avoid replacement or anti-platform framing.

Verification:
- git diff --check origin/main..HEAD
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- cd cloudflare_worker && custom Python harness ran 10 static test functions from tests/test_support_forkmesh_section.py, tests/test_feature_pricing_section.py, and tests/test_index_pricing_section.py
- cd cloudflare_worker && ./deploy.sh dry-run attempted; blocked by missing CLOUDFLARE_ACCOUNT_ID in local environment
