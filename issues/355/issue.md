---
schema: forkmesh-issue-v1
number: 355
title: Chaos test for mirror failover in the worker test suite
status: closed
labels: [infra]
milestone: MVP launch
priority: 6
progress: 0
assignees: [Claude Code]
createdAt: 1783116818254
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-355
ts: 1783116818254
attachments: []
sig: xWeVA2OOnPYxzjMA9jMJgRpVWw6_0DagCOx_k8SwE4GoMQzVX6yivzgqe0nyLtWtYLufcmPk2PDxn4wwAo6ABA
---

**Roadmap Phase 1.** The presence/failover chain was fixed piecemeal after real outages (presence self-refresh, phantom tunnels, stale presence windows). Lock the behavior in with a test that breaks things on purpose.

All in `cloudflare_worker/src/entry.py`: presence expiry on disconnect, the `_source_has_live_host` liveness probe, `_forward_to_node` in-place mirror serving (same URL, no 302s), `clone_sticky` pinning (both clone requests must hit the same node), and source-attested integrity pins (`clone_state_pins` / `repo_state_history`).

Add a pytest in `cloudflare_worker/tests/` that simulates:

1. Source node online, repo published, mirror synced.
2. Source disconnects mid-presence-window → assert browse **and** a full two-request clone (`info/refs` then the `git-upload-pack` POST, `GIT_PACK_RE` ~line 124) are served from the mirror with integrity pins intact.
3. Source returns → assert it reclaims serving.
4. No mirror exists → assert the correct no-live-host error (not a hang).
