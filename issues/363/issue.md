---
schema: forkmesh-issue-v1
number: 363
title: [docs + code] Mainnode protocol spec + fully configurable mainnode URL
status: closed
labels: [infra]
milestone: v1
priority: 30
progress: 0
assignees: [Claude Code]
createdAt: 1783116818262
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-363
ts: 1783116818262
attachments: []
sig: XHPi70C02clHf4-aoYi5BTFl14gDA_nTRwcoIugzcusHQeKTp-Fqoy5EUbgwQOluPiUf6-Th1RiUI_rhEW_yDg
---

**Roadmap Phase 4 (a real network).** Federation starts by making "mainnode" a specification, not this one deployment.

- Docs (writing, not code — hence the title tag): `docs/protocol.md` covering the identity and signature schemes (`issues/README.md` already documents the issue canonical string; catalog records, state attestation, presence, and the clone tunnel need the same treatment), every `/api` route, and the two-request clone flow with sticky pinning.
- Code: the mainnode endpoint is hardcoded (`wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws`, `qt_client/src/MainWindowInternal.h` ~2013; first-run host screen ~5559). Thread a configurable mainnode base host through client and website so a self-hosted mainnode is a first-class target. `cloudflare_worker/deploy.sh` already self-verifies deploys via `/api/version` — document that path as the self-hosting guide.

Acceptance: a second party stands up a working mainnode from the spec alone and points a stock desktop client at it.
