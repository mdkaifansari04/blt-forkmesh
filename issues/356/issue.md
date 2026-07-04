---
schema: forkmesh-issue-v1
number: 356
title: Public status page with uptime history
status: closed
labels: [feature, infra]
milestone: MVP launch
priority: 8
progress: 0
assignees: [Claude Code]
createdAt: 1783116818255
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-356
ts: 1783116818255
attachments: []
sig: 4QF1L9m67C-GeAuuO9kN2TgtVfiOz4fd_LNgljf313SdRQXah_OynROUhHYKILVY_BMvwMZFg16YZoEFuqSoBQ
---

**Roadmap Phase 1.** An error budget starts with a place to see it. `/api/version` already self-reports the live build rev; extend the idea.

- `GET /api/status` in `cloudflare_worker/src/entry.py`: mainnode health, online node count (reuse the `onlineNodes` logic — distinct online nodes, **not** raw `host_presence` rows, which over-count), catalog size, last-24h error counters.
- Persist hourly snapshots in a small Durable Object ring buffer (fixed size — free-plan safe).
- Static page `cloudflare_worker/public/status/index.html` rendering current state + the history sparkline, using the existing dark palette in `styles.css`.

Keep it to one DO read per page view. This also becomes the first thing to check during incidents instead of tailing logs.
