---
schema: forkmesh-pull-v1
number: 12
title: chore: fix blogs path and added the updated images
base: main
head: fix/blog-page
status: merged
ts: 1782772109492
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: JNaM-wUJHJkGu_2J3i1J_VbtdXFsMRj9UytRIYTNU1h4QCvY879rZ8bxBay4bta2KAMcOV_81kN5x-_pKjI9Cg
---

Updates the public blog image paths to optimized WebP assets, fixes the blog detail hero markup, and keeps the blog frontend contract test aligned with the shipped assets. The WebP assets were resized to keep the signed PR inbox payload under API storage limits.

Validation:
- python3.12 -m py_compile cloudflare_worker/src/entry.py
- python3.12 direct run of cloudflare_worker/tests/test_blog_frontend.py (14 tests)
- ./deploy.sh dry-run attempted, blocked by missing CLOUDFLARE_ACCOUNT_ID in this environment

Note: submitted as a patch-only ForkMesh PR because including commits.mbox duplicates binary asset data and exceeded the Worker inbox path.

Commits:

0b80e823 chore: fix blogs path and added the updated images
