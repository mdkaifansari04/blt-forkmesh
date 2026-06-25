---
schema: forkmesh-pull-v1
number: 19
title: feat: add signed pull review thread events
base: main
head: independent-extra-20260625-150132/10-pr-review-thread-events
status: open
ts: 1782379919563
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: Gqku_7WHk1HsZOXS7oAug2dPAKnotrF0b2-4eG2R_syFRtW39VOGRnl9O0o5SH7N6UOoilQt6WEogkhBe3h1Dw
---

This is an independent ForkMesh rescue PR rebuilt from the meshed local main stack.

Independence note:
- Base label: main
- This PR cherry-picked cleanly onto the shared local baseline.
- It does not intentionally depend on the already submitted rescue PRs.

Placement:
- Protocol / PR review feature addition

Issue mapping:
- #136 refinement / GitHub-grade PR review experience

Summary:
- Adds signed pull review thread event support across Worker, Qt, Flutter inbox service, and tests.
- Adds PullReviewModel folding for review/thread state.
- Includes the design and implementation plan docs for review context.

Commits included:
- 3e69525 docs: design github-grade pr review experience
- 96b696d docs: plan github-grade pr review experience
- f4b6609 feat: add signed pull review thread events
- d713954 feat: fold pull review thread state
Verification:
- Clean cherry-pick onto baseline 9885c53.
- `git diff --check` passed.
- `cd cloudflare_worker && python3 -m py_compile src/entry.py` passed.
- `cd qt_client && ./run.sh test` passed.
- Signed with updated PullStore-compatible 5-field canonical string including commits mbox.

