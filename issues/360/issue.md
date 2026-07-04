---
schema: forkmesh-issue-v1
number: 360
title: Search across issues, PRs, and repo code
status: closed
labels: [feature]
milestone: v1
priority: 22
progress: 0
assignees: [Claude Code]
createdAt: 1783116818259
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-360
ts: 1783116818259
attachments: []
sig: nW0hk2bzrCnJdCSYw77qFxL_pgQGXjjmnS048-QoE2UmTzyogEZ0F7vnNJwF2cLaqDvvjpgwMT6pjCY0rFH0BA
---

**Roadmap Phase 3.** Nothing is searchable today beyond per-table filters.

- Desktop: `IssueStore.cpp` / `PullStore.cpp` already hold parsed events in memory — add a global search (Ctrl+K) over titles, bodies, and comments, plus `git grep` over the selected repo's mirror for code (run via `runOffThread`, never on the GUI thread).
- Website: `GET /api/repo/<owner>/<repo>/search?q=` in `cloudflare_worker/src/entry.py`. Issues live in the git tree (`issues/<N>/issue.md`), so the DO can gather over the host tunnel the same way the batched `/blobs` endpoint does. Cap result counts and payload sizes hard — the per-record fan-out rate-limit incident is the cautionary tale.
- Scope: repo-scoped search only for v1. Full-mesh code search is explicitly out (cost/DO limits).

Note: open issue #333 (search PR text in the review pane) is a subset of this; whichever lands first should reference the other.
