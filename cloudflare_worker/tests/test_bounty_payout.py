#!/usr/bin/env python3
"""Bounty payout end-to-end tests (issue #367).

Exercises the on-merge bounty split — fund → merge → split → sweep — plus every
failure path, against an in-memory mock of the Solana cluster. The overriding
property under test is IDEMPOTENCY: the payout records the transfer signature
BEFORE it broadcasts, so a worker that dies (or an RPC that fails) mid-payout can
resume without ever issuing a second on-chain transfer. No Workers runtime: the
functions are lifted straight out of src/entry.py and driven with mocks.
"""

import ast
import asyncio
import re
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

SOLANA_RE = re.compile(r"^[1-9A-HJ-NP-Za-km-z]{32,44}$")
FEE_RESERVE = 5000
TREASURY_BPS = 1000  # 10%

ESCROW = "Escrow11111111111111111111111111111111111111"
PAYEE = "Payee111111111111111111111111111111111111111"
TREASURY = "Treasury11111111111111111111111111111111111x"


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Clock:
    value = 1_700_000_000_000

    @classmethod
    def now(cls):
        return cls.value


class _Chain:
    """A stand-in Solana cluster: tracks balances, which signatures have landed,
    and lets a test script each broadcast outcome to reproduce crash/retry races.
    """

    def __init__(self, balances):
        self.balances = dict(balances)
        self.landed = set()      # signatures the cluster knows and accepted
        self.expired = set()     # tx bytes that will always be rejected
        self.txs = {}            # tx_b64 -> (sig, from_addr, transfers)
        self.sign_calls = 0
        self.broadcast_calls = 0
        # Outcome of the NEXT non-expired broadcast:
        #   "land"        -> settle funds, return the sig
        #   "land_silent" -> settle funds, but return "" (RPC response lost)
        #   "fail"        -> reject, return ""
        self.next_broadcast = "land"

    async def sign(self, _env, from_addr, _secret, transfers):
        self.sign_calls += 1
        sig = "SIG%d" % self.sign_calls
        tx_b64 = "TX%d" % self.sign_calls
        self.txs[tx_b64] = (sig, from_addr, list(transfers))
        return sig, tx_b64

    async def broadcast(self, _env, tx_b64):
        self.broadcast_calls += 1
        entry = self.txs.get(tx_b64)
        if not entry or tx_b64 in self.expired:
            return ""
        sig, from_addr, transfers = entry
        if self.next_broadcast == "fail":
            return ""
        if sig not in self.landed:  # settle exactly once (dedupe on retry)
            for to, lam in transfers:
                self.balances[from_addr] = self.balances.get(from_addr, 0) - lam
                self.balances[to] = self.balances.get(to, 0) + lam
            self.landed.add(sig)
        return "" if self.next_broadcast == "land_silent" else sig

    async def signature_landed(self, _env, sig):
        return sig in self.landed

    async def balance(self, _env, addr):
        return self.balances.get(addr, 0)


def _env_for(chain, saved, captured):
    async def _save_bounty(_env, _bi, rec):
        saved.append(dict(rec))

    async def _record_bounty_payout(_env, rec, transfers):
        captured["records"].append((dict(rec), list(transfers)))

    async def notify_bounty_event(_env, rec, kind):
        captured["notified"].append(kind)

    return {
        "Date": _Clock,
        "SOLANA_RE": SOLANA_RE,
        "SOLANA_SWEEP_FEE_RESERVE_LAMPORTS": FEE_RESERVE,
        "BOUNTY_TREASURY_BPS": TREASURY_BPS,
        "_treasury_address": lambda _e: TREASURY,
        "_solana_sign_transfers": chain.sign,
        "_solana_broadcast_raw": chain.broadcast,
        "_solana_signature_landed": chain.signature_landed,
        "_solana_balance_lamports": chain.balance,
        "_save_bounty": _save_bounty,
        "_record_bounty_payout": _record_bounty_payout,
        "notify_bounty_event": notify_bounty_event,
    }


def _run_payout(rec, chain):
    saved = []
    captured = {"records": [], "notified": []}
    ns = _load("_bounty_auto_payout", "_bounty_mark_paid",
               extra_globals=_env_for(chain, saved, captured))
    result = asyncio.run(ns["_bounty_auto_payout"](object(), "bi", rec))
    return result, saved, captured


def _funded_rec():
    return {
        "status": "funded", "payee": PAYEE, "payee_authorized": True,
        "address": ESCROW, "secret": "seed", "owner": "acme", "repo": "widget",
    }


# --- Happy path: fund -> merge -> split -> sweep ----------------------------

def test_split_pays_payee_and_treasury_90_10():
    chain = _Chain({ESCROW: 1_005_000})  # -5000 fee = 1_000_000 transferable
    rec, saved, cap = _run_payout(_funded_rec(), chain)

    assert rec["status"] == "paid"
    assert rec["payout_sig"] == "SIG1"
    sig, _from, transfers = chain.txs["TX1"]
    assert transfers == [(PAYEE, 900_000), (TREASURY, 100_000)]
    # Funds actually moved on-chain, exactly once.
    assert chain.balances[PAYEE] == 900_000
    assert chain.balances[TREASURY] == 100_000
    assert chain.broadcast_calls == 1 and chain.sign_calls == 1
    assert cap["notified"] == ["bounty_paid"]
    # payout_transfers is persisted; payout_tx is dropped once paid.
    assert rec["payout_transfers"] == [
        {"address": PAYEE, "lamports": 900_000},
        {"address": TREASURY, "lamports": 100_000}]
    assert "payout_tx" not in rec


def test_signature_recorded_before_broadcast():
    # The FIRST save must persist status "paying" + the signature, and it must
    # happen before the escrow is ever debited (record-before-broadcast).
    chain = _Chain({ESCROW: 1_005_000})
    _, saved, _ = _run_payout(_funded_rec(), chain)
    first = saved[0]
    assert first["status"] == "paying"
    assert first["payout_sig"] == "SIG1"
    assert first["payout_tx"] == "TX1"
    # Final save flips to paid.
    assert saved[-1]["status"] == "paid"


# --- Idempotency: repeated calls never double-pay ---------------------------

def test_second_call_is_a_noop():
    chain = _Chain({ESCROW: 1_005_000})
    rec, _, _ = _run_payout(_funded_rec(), chain)
    # Re-run auto-payout against the now-paid record (e.g. cron sweep + a poll).
    rec2, saved2, cap2 = _run_payout(rec, chain)
    assert rec2["status"] == "paid"
    assert chain.broadcast_calls == 1  # unchanged: no second broadcast
    assert chain.balances[PAYEE] == 900_000  # not double-paid
    assert cap2["notified"] == []


def test_resume_after_broadcast_landed_but_response_lost():
    # Worst case: the transfer settled on-chain, but the RPC response was lost so
    # the worker never marked it paid. On retry we must detect the landed
    # signature and finalize WITHOUT rebroadcasting.
    chain = _Chain({ESCROW: 1_005_000})
    chain.next_broadcast = "land_silent"
    rec, _, cap1 = _run_payout(_funded_rec(), chain)
    assert rec["status"] == "paying"       # stuck: send returned ""
    assert chain.balances[PAYEE] == 900_000  # ...but funds already moved
    assert cap1["notified"] == []          # not finalized yet

    chain.next_broadcast = "land"
    rec2, _, cap2 = _run_payout(rec, chain)
    assert rec2["status"] == "paid"
    assert rec2["payout_sig"] == "SIG1"     # same signature, not a new one
    assert chain.sign_calls == 1            # never re-signed
    assert chain.broadcast_calls == 1       # never re-broadcast
    assert chain.balances[PAYEE] == 900_000  # still paid exactly once
    assert cap2["notified"] == ["bounty_paid"]


def test_resume_rebroadcasts_same_bytes_when_not_yet_landed():
    # Broadcast failed outright (nothing landed). Retry must rebroadcast the SAME
    # signed bytes (same signature) rather than sign a fresh transfer.
    chain = _Chain({ESCROW: 1_005_000})
    chain.next_broadcast = "fail"
    rec, _, _ = _run_payout(_funded_rec(), chain)
    assert rec["status"] == "paying"
    assert chain.balances.get(PAYEE, 0) == 0  # nothing moved

    chain.next_broadcast = "land"
    rec2, _, _ = _run_payout(rec, chain)
    assert rec2["status"] == "paid"
    assert rec2["payout_sig"] == "SIG1"  # reused the recorded signature
    assert chain.sign_calls == 1
    assert chain.balances[PAYEE] == 900_000  # paid once


def test_resume_signs_fresh_tx_when_recorded_one_expired():
    # The recorded blockhash expired and the tx never landed: only then may a
    # fresh transaction be signed. Still exactly one on-chain transfer.
    chain = _Chain({ESCROW: 1_005_000})
    chain.next_broadcast = "fail"
    rec, _, _ = _run_payout(_funded_rec(), chain)
    assert rec["payout_sig"] == "SIG1"

    chain.expired.add("TX1")     # the recorded tx can never land now
    chain.next_broadcast = "land"
    rec2, _, _ = _run_payout(rec, chain)
    assert rec2["status"] == "paid"
    assert rec2["payout_sig"] == "SIG2"  # a fresh, different signature
    assert chain.sign_calls == 2
    assert chain.balances[PAYEE] == 900_000  # paid once, not twice


def test_drained_escrow_with_recorded_sig_finalizes():
    # Escrow already emptied by an earlier (recorded) transfer, but never marked
    # paid. Don't loop forever: finalize rather than re-poll a zero balance.
    chain = _Chain({ESCROW: FEE_RESERVE})  # nothing transferable left
    chain.expired.add("TX1")
    rec = _funded_rec()
    rec["status"] = "paying"
    rec["payout_sig"] = "SIG1"
    rec["payout_tx"] = "TX1"
    chain.txs["TX1"] = ("SIG1", ESCROW, [(PAYEE, 900_000), (TREASURY, 100_000)])
    rec["payout_transfers"] = [
        {"address": PAYEE, "lamports": 900_000},
        {"address": TREASURY, "lamports": 100_000}]
    result, _, cap = _run_payout(rec, chain)
    assert result["status"] == "paid"
    assert chain.sign_calls == 0  # never signs a new transfer
    assert cap["notified"] == ["bounty_paid"]


# --- Failure / guard paths --------------------------------------------------

def test_unauthorized_payee_never_pays():
    chain = _Chain({ESCROW: 1_005_000})
    rec = _funded_rec()
    rec["payee_authorized"] = False
    result, saved, _ = _run_payout(rec, chain)
    assert result["status"] == "funded"
    assert chain.sign_calls == 0 and chain.broadcast_calls == 0
    assert saved == []


def test_missing_payee_never_pays():
    chain = _Chain({ESCROW: 1_005_000})
    rec = _funded_rec()
    rec["payee"] = ""
    result, _, _ = _run_payout(rec, chain)
    assert result["status"] == "funded"
    assert chain.sign_calls == 0


def test_already_paid_short_circuits():
    chain = _Chain({ESCROW: 1_005_000})
    rec = _funded_rec()
    rec["status"] = "paid"
    result, saved, _ = _run_payout(rec, chain)
    assert result["status"] == "paid"
    assert chain.sign_calls == 0 and saved == []


def test_unfunded_escrow_holds():
    chain = _Chain({ESCROW: FEE_RESERVE - 1})  # below the fee reserve
    result, saved, _ = _run_payout(_funded_rec(), chain)
    assert result["status"] == "funded"      # nothing to split yet
    assert chain.sign_calls == 0
    assert saved == []


def test_sign_failure_leaves_row_unpaid_and_unsaved():
    chain = _Chain({ESCROW: 1_005_000})

    async def _sign_fail(_env, _from, _secret, _transfers):
        chain.sign_calls += 1
        return "", ""

    chain.sign = _sign_fail
    result, saved, _ = _run_payout(_funded_rec(), chain)
    assert result["status"] == "funded"  # never advanced to "paying"
    assert saved == []                   # nothing persisted
    assert chain.broadcast_calls == 0


def test_broadcast_failure_leaves_paying_for_retry():
    chain = _Chain({ESCROW: 1_005_000})
    chain.next_broadcast = "fail"
    result, saved, cap = _run_payout(_funded_rec(), chain)
    assert result["status"] == "paying"     # persisted for the next tick
    assert saved[-1]["status"] == "paying"
    assert saved[-1]["payout_sig"] == "SIG1"
    assert cap["notified"] == []            # not announced until it lands
    assert chain.balances.get(PAYEE, 0) == 0


# --- Accounting: _record_bounty_payout credits contributor + project --------

def test_record_bounty_payout_credits_payee_and_project_not_treasury():
    credited = []

    async def _record_funds_received(_env, scope, key, name, lamports):
        credited.append((scope, key, name, lamports))

    ns = _load(
        "_record_bounty_payout",
        extra_globals={
            "_treasury_address": lambda _e: TREASURY,
            "clean_string": lambda v, n=240: (v or "")[:n],
            "MAX_NODE_NAME": 63,
            "_record_funds_received": _record_funds_received,
        },
    )
    rec = {"payee": PAYEE, "owner": "acme", "repo": "widget"}
    transfers = [(PAYEE, 900_000), (TREASURY, 100_000)]
    asyncio.run(ns["_record_bounty_payout"](object(), rec, transfers))

    # The treasury cut is NOT recorded; the payee is credited as contributor and
    # the owner/repo as project — each for the payee's 900_000 share.
    assert credited == [
        ("contributor", PAYEE, "", 900_000),
        ("project", "acme/widget", "acme/widget", 900_000),
    ]
