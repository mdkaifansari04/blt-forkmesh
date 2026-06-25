---
schema: forkmesh-pull-v1
number: 22
title: feat: add durable repository discussions
base: main
head: kaif/durable-repository-discussions-20260625-161434
status: merged
ts: 1782407932197
author: F4rlozoS0ls2-Evs6Q7mLsSsZR1TVwMAsan0tdgaJoM
authorName: Md Kaif Ansari
sig: sYnZS5nhyci681-0bm9rI4KTjWfSN5hINBar3DtlpCGiovNLPnB62MhBbNF08Ro9ZNOLIsejhFAtVrL5U7hgCQ
---

Adds durable repository discussions across the Worker, public repo page, and Qt desktop node.

Summary:
- Adds signed discussion open/comment events and encrypted discussion inbox relay support.
- Adds the discussion_inbox D1 migration/schema and Worker route /api/repo/{owner}/{repo}/discussions.
- Adds public web discussion tab loading from durable discussions/ files.
- Adds Qt DiscussionStore, discussion inbox backoff, Discussions repo tab, create/comment UI, and inbox draining.
- Preserves updated main Worktrees/Cove/private sharing/PullReviewModel paths while adding the new tab.

Commits included:
- ca2b962 feat: add durable repository discussions
Verification:
- `git diff --check main..HEAD` passed.
- `python3 -m py_compile cloudflare_worker/src/entry.py` passed.
- `python3 cloudflare_worker/tests/test_crypto.py` passed.
- `cd qt_client && ./run.sh test` passed.
- Signature verified locally; patch applies from main; commits mbox replays with git am --3way.
