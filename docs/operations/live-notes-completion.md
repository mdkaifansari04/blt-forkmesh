# ForkMesh plan and live-notes completion record

Completed locally on 2026-07-23 America/New_York. This record is intentionally
outside the checklist files that were scheduled for deletion.

## Outcome

The approved `plan.md` and `live-notes.md` work is implemented and validated,
with one explicit exception: at the owner's direction, the final qualitative
`plan.md` line was skipped instead of being claimed as objectively complete.
The implementation includes the multiplayer Three.js World, privacy-safe
identity badges, repository and organization spaces, mirror routing and
control-node support, security and abuse controls, repository analysis,
Fediverse/community surfaces, non-custodial rewards, and the documented
desktop/Web/Flutter capability boundaries.

The capability inventory and intentional surface exceptions are recorded in
`docs/capability-matrix.md` and `docs/capability-matrix.json`.

## Reward-program follow-up

- `TREASURY_SOLANA_ADDRESS` remains a supported public community-pool address.
  A non-empty `COMMUNITY_REWARD_POOL_ADDRESS` is an explicit override; otherwise
  the Worker uses `TREASURY_SOLANA_ADDRESS`.
- Users can voluntarily join or fund the program with a direct, user-signed
  wallet transfer. ForkMesh does not take custody, create a user wallet, promise
  returns, sell ownership, or grant governance/reward entitlement for a
  contribution.
- The public address, user-owned funds, pending allocations, completed
  transfers, and visual-only fountain effects are distinguished in the UI.
- Pool signing remains on the first instance owner's local Qt control node.
  Private signer material is not accepted by the Worker.
- Production validation fails closed on invalid cluster, RPC, or pool-address
  combinations. Devnet remains an explicit development/test mode.

Operational details are in `community-reward-pool.md` and
`community-reward-pool-signer.md` in this directory.

## SSH Git follow-up

- Authenticated users can register, list, and revoke validated OpenSSH public
  keys. Normalized key material is encrypted at rest; private keys are never
  uploaded.
- The Cloudflare Worker is the HTTPS authorization plane, not an SSH server.
  A separately operated OpenSSH gateway uses a restrictive
  `AuthorizedKeysCommand` and forced Git commands.
- Only `git-upload-pack` and `git-receive-pack` are accepted. Repository
  allowlists, repository-root containment, local read-only enforcement, current
  account identity, and Worker owner/share/organization permissions are all
  checked.
- SSH URLs remain hidden unless the gateway host, ASCII base64url bearer token,
  and explicit per-repository allowlist are all valid. Setting the allowlist to
  `disabled` fails closed.
- Pasting a public key does not itself prove private-key possession. OpenSSH
  proves possession when the connection is made; an account-specific fresh key
  is recommended to avoid conventional public-key registration squatting.
- Raw SSH requires a real TCP endpoint such as direct DNS plus firewalling,
  Cloudflare Spectrum, or an appropriate Tunnel configuration. An HTTP Worker
  cannot terminate raw SSH.

Deployment and hardening details are in `git-ssh-gateway.md` in this directory.

## Commit-history rewrite

The local rewrite completed successfully for 8,703 commits across 18 local
branches and 2 tags. It did not include remote-tracking, pull, stash, backup, or
other operational refs, and it did not push.

Canonical private recovery directory:

```text
/home/f/projects/forkmesh/.git/forkmesh-history-rewrite/20260724T004019Z
```

The directory is mode `0700`; its bundle, original-message export, mapping, and
manifests are mode `0600`. `pre-rewrite.bundle` verifies as a complete bundle.
The 20 original scoped tips also remain under:

```text
refs/forkmesh-history-backup/20260724T004019Z/
```

`original-commit-messages.jsonl` contains every original ID, full title,
description, exact message bytes, and identity headers. It must remain private
and must not be committed or published. `old-to-new.json` contains the complete
8,703-entry mapping. All rewritten commits were independently checked for
unchanged tree, mapped parent order/topology, exact author/committer headers and
timestamps, stripped invalid signatures, and the one-line name-redaction
policy. The main tree remained
`a807d623a608d45e805e48ba2b9dc89b736dcfb5`, and the worktree-status fingerprint
remained `adc3c1ba7e22be67cc89200d197da433e6cfce656178a4bceafb56dea5c8e085`.

A guarded preflight at `20260724T003653Z` found a redaction-placeholder edge
case and stopped before changing refs. Its private bundle/backup refs remain as
additional recovery material, but `20260724T004019Z` is the completed canonical
rewrite.

To restore the 20 scoped refs locally:

```bash
python3 tools/rewrite_commit_messages.py restore \
  --artifact-dir /home/f/projects/forkmesh/.git/forkmesh-history-rewrite/20260724T004019Z \
  --confirm RESTORE-COMMIT-HISTORY
```

The rewrite invalidates signed commit objects, signed annotated tags, old
commit URLs/external references, and scan records pinned to old IDs. Publishing
the rewritten history requires a separately authorized, coordinated
force-update. Mirrors and existing clones must preserve a backup and re-clone
or reset explicitly to the exact new tips; ordinary merges must not reconnect
the old and new graphs. Never use a blind `push --mirror`.

## Final validation evidence

- `python3 -m pytest -q cloudflare_worker/tests`: 1,717 passed, 1 skipped,
  0 failed in 138.64 seconds. The skip is the optional `jsonschema` package
  contract check; the same file otherwise passed 16 tests, and runtime schema
  validation remains covered.
- `python3 tools/quality_gate.py run --profile release`: passed Python syntax,
  35 security/privacy contracts, 1 performance-budget test, 4 quality-gate
  contracts, and 19/19 Playwright browser tests.
- `cmake --build qt_client/build -j2 --target check`: 11/11 passed.
- `/home/f/.forkmesh/toolchain/flutter/bin/flutter analyze`: no issues.
- `/home/f/.forkmesh/toolchain/flutter/bin/flutter test`: 126 passed.
- Production JavaScript syntax, Python compile-all, `git diff --check`, all 7
  branch/PR guard checks, and the complete MCP end-to-end script passed.
- `git fsck --full --no-dangling`, bundle verification, exact ref/backup
  comparison, tag peeling, mapping cardinality, and all-commit rewrite
  invariants passed.
- The full Worker suite covers the migrations, repository privacy, mirror
  routing, reward, SSH, security, Fediverse, World, and automation contracts.

## Intentionally unperformed external operations

- No Cloudflare production deployment, DNS mutation, Worker secret mutation,
  or real mirror/SSH gateway provisioning was performed.
- `COMMUNITY_REWARD_POOL_ADDRESS`, `TREASURY_SOLANA_ADDRESS`, and
  `COMMUNITY_REWARD_RPC_URL` were unset locally. Consequently no live Solana
  mainnet RPC/account preflight or transfer was performed, and locally signed
  distributions were not enabled. Fail-closed dry-run/mock validation passed.
- `SSH_GATEWAY_HOST`, `SSH_GATEWAY_TOKEN`, and
  `SSH_GATEWAY_REPOSITORIES` were unset locally. Consequently no live OpenSSH
  handshake or push was attempted; SSH URLs remain correctly suppressed.
- No Git history was pushed. No GitHub issue migration, Fediverse post, email,
  wallet transfer, or other third-party write was sent without configured
  credentials and explicit deployment authority.
