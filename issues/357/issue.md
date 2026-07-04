---
schema: forkmesh-issue-v1
number: 357
title: Performance budgets enforced in CI (startup, tab-open, relay p95)
status: closed
labels: [infra]
milestone: MVP launch
priority: 12
progress: 0
assignees: [Claude Code]
createdAt: 1783116818256
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-357
ts: 1783116818256
attachments: []
sig: rMbp0NL94z30ob3SAkR_HvCRYnziE3-yK3ncHaB5v84ul9kj-GfocCqy2VmdXTdbJ8tJdj8IgT7Nt-Bv4wDiCQ
---

**Roadmap Phase 2 (performance).** Startup, the Commits tab, and lazy list tabs were all found and fixed by feel. Machines should catch the next regression.

- Client: add timing assertions to the window tests (run via `cmake --build --target check`): construct `MainWindow` under `QT_QPA_PLATFORM=offscreen`, measure ctor→first-paint and each repo-detail tab switch with `QElapsedTimer`; fail over budget. Calibrate budgets against current numbers first, then ratchet down (target: startup < 1000 ms, tab switch < 150 ms in the CI environment).
- Worker: pytest timing checks for `/api/repositories` and the batched `/blobs` endpoint in `cloudflare_worker/tests/`.
- Keep budget tests in their own test binary: window-tests are known-flaky in some sandboxed environments, and a perf gate must be skippable per-environment without disabling functional coverage.
