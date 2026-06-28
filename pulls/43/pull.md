---
schema: forkmesh-pull-v1
number: 43
title: landing: refresh sign-in and signup pages
base: main
head: feat/pr43-auth-pages
status: open
ts: 1782673310434
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 2PsoYYbYhwrVAL1cu5QLJHCVPKGGHkHoLLF_heSf0EM5uWyAxqmBDgi4kD33qdL5C-AYfhKYcpcEsK7C7P5YAw
---

## Summary
- Refresh login/signup pages and client scripts, including key-binding coverage and auth-page frontend tests.

Split from local landing PR #43 into an independently reviewable signed patch.

## Tests
- Included targeted frontend/route coverage where relevant.
- Original combined branch passed: python3 -m py_compile src/entry.py

