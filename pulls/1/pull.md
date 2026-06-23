---
schema: forkmesh-pull-v1
number: 1
title: Fix Qt shared mainnode chat room routing
base: main
head: fix/issue-147-shared-chat-room
status: closed
ts: 1782210482883
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: _b4ZSR1bEftafiUNSket83SrNVN_nLCpp8kdURm8VWU-4RMugSYVxNwXkE2ZbR3tfpkQhKrr50vFUtN4pCqCCQ
---

Summary:
- Route repo-specific mainnode chat URLs into the shared mainnode room.
- Keep host-only mainnode URLs preserving their explicit port.
- Add Qt crypto/server tests covering shared-room URL canonicalization.

Test Plan:
- cd qt_client && ./run.sh test

Commit:
- f51210d36ac09bba0c3b43368bdaceb184cb3b65

Base:
- main @ 83d4792ab6b0f7ac1dba930a536a3ef21eec6fcd
