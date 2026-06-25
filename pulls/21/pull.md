---
schema: forkmesh-pull-v1
number: 21
title: feat: add admin sign-in page
base: main
head: kaif/admin-sign-in-20260625-160933
status: open
ts: 1782384199885
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: hz4KXLNkLqWddCagv-uWamWDU9d-qP_oUCX0x5BAOOrVSQL8NwznyQutcNhk2TsCPZ9rXvUbTJL3RJcVYNE0Cw
---

Adds a proper Worker admin sign-in page for the admin dashboard.

Summary:
- Makes /admin the canonical admin dashboard path while keeping ADMIN_PATH as an optional legacy admin-* alias.
- Replaces browser Basic Auth as the primary UI with an HTML sign-in page backed by ADMIN_USER / ADMIN_PASS.
- Sets a signed, HttpOnly, SameSite=Lax admin session cookie after login.
- Keeps HTTP Basic Auth as a fallback for existing tooling.
- Adds a sign-out action and routes /admin and /admin/* through the Worker before static assets.
- Makes ADMIN_PATH optional in deploy secret validation and documents the new /admin path.

Commits included:
- 989931d feat: add admin sign-in page
Verification:
- `python3 -m py_compile cloudflare_worker/src/entry.py` passed.
- Admin sign-in static tests passed via direct Python invocation.
- `git diff --check main..HEAD` passed.
- Signature verified locally; patch applies from main; commits mbox replays with git am --3way.

