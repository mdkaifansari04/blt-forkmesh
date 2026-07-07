---
schema: forkmesh-pull-v1
number: 21
title: Refresh feature blog content
base: main
head: api-pr/20260708-001118/feature-blog-content
status: open
ts: 1783449942745
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: HkJZFCRrGhayErmFL_pMMS1zDInbouNfjh-Mv8aspVVm-VoCxMMZSn8BU1QxwZwRcZ-r2nFXoRB6AkFAdduQAQ
---

Rewrites generated feature blog article bodies and archive/search snippets so each blog title serves title-specific ForkMesh content.

Test plan:
git diff --check; python3 tests/test_blog_frontend.py; python3 -m py_compile src/entry.py

This ForkMesh PR was rebuilt independently from current origin/main and can be reviewed without the other local branch slices.
