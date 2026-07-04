---
schema: forkmesh-issue-v1
number: 364
title: Second mainnode with catalog convergence
status: closed
labels: [feature, infra]
milestone: v1
priority: 31
progress: 0
assignees: [Claude Code]
createdAt: 1783116818263
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-364
ts: 1783116818263
attachments: []
sig: GVtMQIj2S8-QVkJcOZNk5m1-Z6Huf6Y1pyS4bpbAVOrjFHatk1dZzDlLAJfTeu0rPx1H3ADs8rZjSDwplynqBQ
---

**Roadmap Phase 4.** One mainnode is one SPOF — and one Cloudflare free-plan quota: a daily-quota exhaustion has already taken the whole network down once (site-wide 429s; client reconnect backoff exists but availability didn't).

Builds on #363. Plan:

- Nodes register and heartbeat to N mainnodes: generalize the reconnect loop in `qt_client/src/ServerNode.cpp` from one endpoint to a list.
- Catalog convergence: mainnodes gossip signed catalog records to each other. Records are already independently verifiable (`catalogSig` + `stateSig`), so convergence is last-writer-wins on the signed timestamp — mainnodes never need to trust each other.
- Repo identity across mainnodes already exists: mirror grouping keys off the repo **root commit**, which is exactly what makes cross-mainnode merging safe.
- Client and website pick any healthy mainnode (order by latency, fail over on error).

ForgeFed/ActivityPub interop (<https://forgefed.org>) is the eventual bridge to the wider federated-forge ecosystem, and rides on this — not before it.
