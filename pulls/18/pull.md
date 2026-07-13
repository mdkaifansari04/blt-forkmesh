---
schema: forkmesh-pull-v1
number: 18
title: Cover contribution login key binding across Worker and Qt
base: main
head: api-pr/20260714-035346/login-key-binding-coverage
status: open
ts: 1783981843806
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: pn3C2UUw3HRwCSQzrkf70jCOgZU1ulA2sSEqbwWPr91sihH7_IYJ9xn4etiETC2H1G--M7dXwzX09UCXopgCCw
---

Adds the cross-stack login/key-binding test coverage for contribution identity proofs.

This is intentionally a small dependent follow-up: it should be reviewed/applied after the Worker profile contributions API PR and the Qt profile contribution support PR, because the test asserts behavior across both layers. Verified in an integrated proof worktree with those two PR commits applied first: tests/test_login_key_binding.py passed 17/17.

Submitted as a lightweight signed API PR. Based on local main f760e13 because fetching origin/main failed with a ForkMesh mirror integrity check error, so remote freshness could not be proven.
