---
schema: forkmesh-pull-v1
number: 20
title: feat: add features landing page waitlist
base: main
head: kaif/features-landing-waitlist-20260625-152306
status: open
ts: 1782381276813
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 2LGYoFLgw4DWCq7VgVZRIIuj8JLg9R_-PWz0PVTgjdnuJfcLPOGG2F5mreZsqCIZrggTC0l6PjWT01SRd9yvBg
---

Adds the public /features/ landing page and waitlist plumbing to the Worker site.

Summary:
- Adds the full features landing page content under cloudflare_worker/public/features/.
- Keeps /features redirecting to /features/ so the static asset path is canonical.
- Wires /api/waitlist to the existing waitlist handler.
- Adds the waitlist D1 schema in SCHEMA_STATEMENTS and a migration file.
- Updates Worker assets routing so /features reaches the Worker redirect before static assets.

Commits included:
- 1b215d2 feat: add features landing page waitlist
Verification:
- `python3 -m py_compile cloudflare_worker/src/entry.py` passed.
- `git diff --check main..HEAD` passed.
- Pull payload signature verified locally with OpenSSL.
- Patch applies from main and commits mbox replays with `git am --3way`.
- `cd cloudflare_worker && ./deploy.sh dry-run` was attempted but could not run in this shell because CLOUDFLARE_ACCOUNT_ID is not configured.

