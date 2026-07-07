---
schema: forkmesh-pull-v1
number: 19
title: Add press kit page
base: main
head: api-pr/20260708-001118/press-page
status: merged
ts: 1783449942608
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: k88oTtYIAmBto--MJMpMDHA7sMESaOYKQJ9NvVUtq2EvKctJqN5woymXqHZm3ZaXWszpgfk52K48sqaNS-27Bg
---

Adds the public press kit page, shared footer discoverability, canonical static routing, and the homepage navbar label update.

Test plan:
python3 tests/test_press_page_frontend.py; static route contract functions; python3 -m py_compile src/entry.py

This ForkMesh PR was rebuilt independently from current origin/main and can be reviewed without the other local branch slices.
