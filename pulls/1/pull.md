---
schema: forkmesh-pull-v1
number: 1
title: fix(worker): canonicalize public routes and shared chrome
base: main
head: kaif/public-routes-shared-chrome
status: merged
ts: 1783173142154
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: f1zOV-oG_iyATG3Qzv4cATXKprCnx_gynpbO0AA8pIJ_TuJTMUiryVqsV3bVQ5-ANei01lnrtDfcrFvuRAFFDw
---

Canonicalizes public page routes, moves repo shortcuts into Worker routing, and adds shared public-site header/footer chrome.

Test plan:
- uv run --with pytest python -m py_compile src/entry.py src/static_routes.py
- uv run --with pytest python -m pytest tests/test_static_route_canonicalization.py tests/test_shared_header_frontend.py tests/test_legal_pages_frontend.py tests/test_auth_pages_frontend.py tests/test_blog_frontend.py tests/test_account_profile_endpoints.py tests/test_public_logo.py -q
