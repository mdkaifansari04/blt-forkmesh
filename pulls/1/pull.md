---
schema: forkmesh-pull-v1
number: 1
title: docs: clarify ForkMesh pillar 1 trust model
base: main
head: api-pr/20260709-231001/pillar1-public-clarity
status: open
ts: 1783618833342
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: Jprk_56w-epgzrsr6fIM-fQA0vcn8UtdPhPVwGF7Nj7ImRfRsaZyIiErtu4wJRI5XJznVRcRlvQK8e3Jrn-HDw
---

## Summary
- Clarifies ForkMesh Pillar 1 trust model across README, protocol docs, homepage, and public docs.
- Adds compatibility, centralization, money/rewards, threat-model, and GitHub-refugee public docs copy.
- Reframes reward/pricing copy as experimental instead of guaranteed payout language.
- Adds static contract tests pinning the public copy and /docs canonical route behavior.

## Verification
- Rebased on current origin/main; origin/main is an ancestor of this head.
- git diff --check origin/main..HEAD
- cd cloudflare_worker && python3 -m py_compile src/entry.py
- cd cloudflare_worker && direct Python fallback executed 15 touched test functions because pytest is not installed in this environment.

## Notes
- Lightweight PR: one commit, no pulls/* metadata, no new mock UI flow.

