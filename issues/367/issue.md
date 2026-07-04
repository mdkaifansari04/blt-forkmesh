---
schema: forkmesh-issue-v1
number: 367
title: [code + live-ops] Exercise the bounty payout end-to-end on mainnet
status: closed
labels: [security, infra]
milestone: Phase 2
priority: 50
progress: 0
assignees: [Claude Code]
createdAt: 1783116818266
author: 7ZMh_2s_IOTPiKoZ6K5eDe4AclTYh_JzqJ7pK2lovT4
authorName: newnewnode
bountyUsd: 0.00
bountyAddress: 
bountyStatus: 
type: open
id: open-367
ts: 1783116818266
attachments: []
sig: dUZQAM23JHFSKie9Hby-0bUERIQ2xXhYqrPDl7_wM2Vd_EUE2e53sKDIMv0FfE1rkR3Torlz7c7Rsn85gfuwDQ
---

**Roadmap Phase 6 (money & trust).** Deposit custody runs on mainnet (per-issue Solana deposit addresses, funding confirmation, sweeps — `REPO_BOUNTY_RE`, `cloudflare_worker/src/entry.py` ~104), but the payout-on-merge split (`_record_bounty_payout`, ~1944; 90/10 contributor/project) has **never** executed against real funds.

- Code: pytest with a mocked Solana RPC covering fund → merge → split → sweep, including failure paths. Payout must be idempotent — record the transfer signature before broadcast so an RPC failure mid-payout can never double-pay on retry.
- Live-ops (not code — needs the owner, real SOL, and a production deploy): run one small (~$5) bounty through production end-to-end and keep the txids as the audit trail.
- Afterwards: write the custody design into `docs/` — that document is the prerequisite input for a third-party security audit (external/business task, tracked when we get there).

Open issue #347 (auto-SOL per merged PR) must build on this verified path, not precede it.
