---
schema: forkmesh-pull-v1
number: 3
title: feat(worker): add public profile follow metadata
base: main
head: api-pr/20260710-002121/profile-follow-api
status: open
ts: 1783623320946
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: E8t_WdJXbGIJXXPXeH0-ZAPBa5jTfa4M0aOuLm54yI1cqgAyXUbEsQ8J3WD-ABs3EInX7CxeTLFAS-vujfLoAQ
---

Summary:
- Adds profile follow storage and account/profile follow metadata.
- Adds profile about/location/timezone fields and public profile social counts.
- Adds follow/unfollow API routing and account/profile endpoint coverage.

Test Plan:
- git diff --check origin/main..HEAD
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- Ad-hoc Python runner for tests/test_account_profile_endpoints.py

Note:
- Built from local origin/main because fetch from forkmesh.com returned a repository integrity-check failure.
