# Bounty Custody & Payout Design

Status: implemented (mainnet). Audience: maintainers and a future third-party
security auditor. This document describes how ForkMesh custodies issue-bounty
funds and how it splits them to a contributor when a pull request merges.

All code references are to `cloudflare_worker/src/entry.py` unless noted;
low-level Solana plumbing lives in `cloudflare_worker/src/solana.py`.

## Summary

A bounty is escrowed in a **per-issue, worker-custodied Solana account**. When
the issue's PR merges, the escrow is split **90% to the contributor / 10% to the
treasury** and swept out. Everything runs on **mainnet**. The same custody model
backs the signup-donation funnel and the central fund; bounties reuse it.

The single most important safety property of the payout path is **idempotency**:
the transfer signature is recorded *before* the transaction is broadcast, so a
worker that dies — or an RPC that fails — mid-payout can resume without ever
issuing a second on-chain transfer.

## Custody

- **One keypair per bounty.** On create, `_new_solana_keypair()` generates a
  fresh Ed25519 keypair with the runtime's WebCrypto. The public key *is* the
  Solana deposit address; funders send SOL there.
- **Keys at rest.** The 32-byte seed (base64url of the JWK `d` field) is stored
  **encrypted** (`encrypt_row` / `decrypt_row`) in the D1 table `issue_bounty`,
  one row per `(owner, repo, number)`. The row is keyed by a blind index
  `bounty_bi = blind_index("<owner>/<repo>#<number>")` so the plaintext
  owner/repo/number never appears in the primary key. The seed and the encrypted
  blob are never returned to any client — `_bounty_public()` exposes only
  payout-safe fields (address, amounts, status, payee, payout signature).
- **This is a custodial model.** The worker holds the escrow key. The trust
  boundary is the worker + its D1 encryption key. There is no on-chain
  multisig/escrow program; safety comes from the authorization gate below plus
  keeping the seed encrypted at rest.

## Amounts & funding

- USD amounts are converted to lamports via `_usd_to_lamports()` using the
  SOL/USD price (`_sol_usd_price`: Pyth on-chain oracle first, HTTP fallback).
  Bounties are bounded to `[BOUNTY_MIN_USD=$1, BOUNTY_MAX_USD=$100000]`.
- Funding is confirmed by polling `getBalance` (`_solana_balance_lamports`). The
  escrow becomes `funded` once the balance reaches `required_lamports`.
- A fixed `SOLANA_SWEEP_FEE_RESERVE_LAMPORTS = 5000` is held back from every
  split to cover the network fee, so transfers never overdraw the account.

## Authorization gate (why a funded escrow is un-stealable)

Auto-payout only ever pays a payee the **repo owner explicitly authorized**.

- Choosing the payee requires an **owner-signed** request. The `create` (and the
  manual `payout`) action verifies an Ed25519 signature by the owner account key
  over a canonical string — e.g.
  `forkmesh-bounty-create-v1\n<owner>\n<repo>\n<number>\n<payee>\n<ts>` — with a
  freshness check (`_ts_ok`). Only a valid owner signature sets `payee` and
  `payee_authorized = True`.
- `_bounty_auto_payout` refuses to move funds unless `payee_authorized` is set
  and the payee is a well-formed Solana address. Without this gate an
  unauthenticated caller could repoint an unpaid bounty at their own wallet and
  drain it; the gate is what makes the escrow safe to auto-release.

## Payout split

When an escrow is both `funded` and has an owner-authorized payee, the split runs
with no second manual step:

1. `transferable = balance - fee_reserve`.
2. `treasury_lamports = transferable * BOUNTY_TREASURY_BPS / 10000` (10%).
3. `payee_lamports = transferable - treasury_lamports` (~90%).
4. Both transfers go out in a single System-Program transaction.

`_record_bounty_payout` then credits the funds-received boards: the payee's share
is booked to the **contributor** (by wallet) and to the **project** (owner/repo).
The treasury cut is intentionally not recorded.

Payout is triggered from three places, all funneling through the same idempotent
core (`_bounty_auto_payout`):

- **Client status poll** — the `status` action pays out as soon as it observes a
  funded, authorized escrow.
- **Cron backstop** — `sweep_funded_bounties` pays any funded+authorized escrow so
  the split happens even if no client ever polls.
- **Owner manual payout** — the owner-signed `payout` action authorizes the payee
  and then delegates to `_bounty_auto_payout` (one payout implementation, not two).

## Idempotency: record-before-broadcast

The invariant: **a mid-payout failure on retry can never double-pay.** A Solana
transaction's signature is fully determined by `(from, transfers, blockhash)` and
is computed locally at signing time, so it can be persisted before the network
ever sees it. `_bounty_auto_payout` uses this as follows:

1. **Sign, don't send.** `_solana_sign_transfers` builds and signs the transfer
   transaction and returns `(signature, raw_tx_base64)` — no broadcast.
2. **Record first.** The row is saved with `status = "paying"`, `payout_sig`, and
   the raw `payout_tx`, *before* the broadcast. If the process dies here, the row
   is a durable record of exactly which transaction was authorized.
3. **Broadcast.** `_solana_broadcast_raw` sends the signed bytes. On a confirmed
   send the row transitions to `paid` (`_bounty_mark_paid`, which runs accounting
   and notification exactly once and drops the retained raw tx).
4. **Resume.** If a later tick finds a `paying` row, it does **not** sign a new
   transfer. It asks the cluster about the recorded signature
   (`_solana_signature_landed` → `getSignatureStatuses`):
   - **Landed** → finalize as `paid` without re-broadcasting.
   - **Not landed** → re-broadcast the *same* bytes (identical signature, which
     the cluster dedupes if it actually did land in a race).
   - **Recorded tx expired and never landed** → only then sign a fresh
     transaction. If the escrow is already drained but a signature was recorded,
     the row is finalized rather than looped forever.

A `paying` row is also treated as locked by the `create` action, so a re-create
can't repoint or re-amount an escrow while a transfer is in flight.

Test coverage: `cloudflare_worker/tests/test_bounty_payout.py` exercises the
happy split, the 90/10 math, double-call no-ops, and every resume race
(landed-but-response-lost, rebroadcast-same-bytes, expired→fresh-sign, drained
escrow) plus the sign/broadcast/guard failure paths, against a mocked cluster.

## Live-ops (out of scope for code)

Proving the path against real funds is a separate, owner-run task: fund one small
(~$5) bounty in production, let it merge and split, and keep the transaction
signatures as the audit trail. That exercise is the prerequisite for issue #347
(auto-SOL per merged PR), which must build on this verified path.

## Trust boundary & residual risks (for the auditor)

- **Custodial key exposure.** The escrow seed is only as safe as the D1
  encryption key and the worker runtime. Compromise of either exposes escrowed
  funds. There is no on-chain escrow program limiting the worker's authority.
- **Price oracle.** USD↔SOL conversion depends on Pyth (with an HTTP fallback);
  a manipulated or stale price affects the funding target, not the split ratio.
- **Payee authorization** rests entirely on the owner account's Ed25519 key.
  Owner key compromise lets an attacker redirect that owner's bounties.
- **Finality.** Payout finalizes on a confirmed/known signature; the design
  favors never double-paying over instant finality, so a genuinely dropped
  transaction is retried on the next tick rather than lost.
