---
schema: forkmesh-pull-v1
number: 46
title: landing: update home route shell
base: main
head: feat/pr43-07-landing-home-core
status: open
ts: 1782673430682
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: qmET9LnqFMa0yR4zeiDq_p1pQ6iprQmwXYgKPGYARnoYGZYeHsxvdZPMEs4ba20WkJldSU0Q37ckCE1Pq8RmAw
---

## Summary
- Home page, public routes, Worker route config, Qt dashboard link, and landing page route tests without large binary assets.

Split from local landing PR #43 into an independently reviewable signed patch.

## Tests
- Original combined branch passed: python3 -m py_compile src/entry.py
