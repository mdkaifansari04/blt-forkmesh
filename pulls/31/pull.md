---
schema: forkmesh-pull-v1
number: 31
title: fix(auth): bind desktop key on login
base: main
head: mdkaifansari04:fix/login-bind-desktop-key
status: open
ts: 1782469842815
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: mdkaifansari04
sig: OQWz0VbfEQcsTwGJ6UVp9-GM8OasVWcvchz1vjEsQgLEXgiTc1qR-miiGx_XM1qrj6iIGxPm78ogZanAGoppCA
---

## Summary
- Bind an empty account pubkey to the desktop Ed25519 key after successful login
- Send the Qt client's public key during login and avoid treating cached password login as signed hosting auth
- Add a Worker regression test for login key binding

## Test Plan
- uvx pytest cloudflare_worker/tests -q
- cd qt_client && ./run.sh test

