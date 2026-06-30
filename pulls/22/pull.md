---
schema: forkmesh-pull-v1
number: 22
title: Metadata: preserve remaining full branch records
base: main
head: kaif/split-full-relay-20260701-000610/collaboration-metadata-remainder
status: open
ts: 1782845285109
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: 2WJMJ0RQb4csOzZ1bzXz1ajS_blicRgW7oFD0yWFJth7gbtpt0iJxvpjyq7kehFIYHHp2I4QNwwQjU5lmy-iDQ
---

Remainder payload to cover files from the full branch that were not included in the seven implementation/asset PRs.

This intentionally includes collaboration metadata and local planning docs because the maintainer asked that the entire branch be converted with nothing left out.
Includes: cloudflare_worker/docs/superpowers/*, issues/* records, and pulls/* records from the full branch.
Note: stored pulls/*/changes.patch files contain whitespace exactly as in the source branch; they were preserved rather than normalized.

