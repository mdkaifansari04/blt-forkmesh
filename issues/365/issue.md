---
schema: forkmesh-issue-v1
number: 365
title: Agent provenance: attributable, signed agent authorship in the review UI
status: closed
labels: [feature]
milestone: v1
priority: 40
progress: 0
assignees: [Claude Code]
createdAt: 1783116818264
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-365
ts: 1783116818264
attachments: []
sig: OFFQi84YNmi7czpVPahMe67kWbWE6Esc-VYZIe8AAH4yjUBonbI2Z-3qD1At-u5v1tzyDWvTYWd62JKv5Bm5BA
---

**Roadmap Phase 5 (agent-native forge).** Agents open PRs today but their commits are indistinguishable from human ones. Attributable machine authorship is table stakes for reviewing at agent volume.

- `qt_client/src/AgentRunner.cpp`: add a `ForkMesh-Agent: <tool>/<model>` commit trailer (same mechanism as `Co-Authored-By`) to agent-authored commits.
- The auto-created PR (the harness already opens PRs from the worktree diff on session completion) records agent metadata in its signed event.
- Review UI: badge agent-authored commits and PRs in `MainWindowPulls.cpp` / `PullReviewModel.cpp` ("agent-authored — Claude Code"), and let Files-changed filter by authorship.

Trust model note: everything stays signed by the node key as today — the trailer is provenance metadata, not a new trust root, so no protocol change is needed.
