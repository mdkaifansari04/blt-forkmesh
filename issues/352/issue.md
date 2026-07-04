---
schema: forkmesh-issue-v1
number: 352
title: Nightly end-to-end mesh loop test: publish -> browse -> clone -> issue -> agent PR -> merge
status: closed
labels: [infra]
milestone: MVP launch
priority: 3
progress: 0
assignees: [Claude Code]
createdAt: 1783116818251
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-352
ts: 1783116818251
attachments: []
sig: CuhWVmhHG2QsGpqCBA0tNgA_QEGnq_feEzV5VdFvIfCDrTDET7UlgZm0tOqssWx7epOVHirdVC-HEr-yEE2gAA
---

**Roadmap Phase 1 (reliability).** Every past outage (relay OOM, stale presence, stalled releases) would have been caught by one continuously exercised full-product loop. This is the single highest-leverage test we can add.

What to build:

- Extend headless mode (`qt_client/src/HeadlessConsole.cpp`; headless account registration already works via `registerNodeAccountSilently` in `startSession`) to drive: publish a scratch repo, wait for it in `/api/repositories`, browse a file over HTTP, `git clone` through the relay (both requests: `info/refs` + `git-upload-pack` POST), file a signed issue via `POST /api/repo/<owner>/<repo>/issues`, drain it, run an agent, and merge the resulting PR.
- Use a stub agent binary for the agent step (assert the PR plumbing, not the model) so the run never depends on API quota.
- Reuse the request helpers in `cloudflare_worker/tests/` for the worker-side assertions.
- Wire it as a `forkmesh-e2e` ctest target runnable via `cmake --build --target check`.

The nightly scheduling itself is CI configuration (ops, not code); everything above is codebase work. Protocol reference for the inbox step: `issues/README.md` ("Cross-user").
