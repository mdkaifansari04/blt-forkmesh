#!/usr/bin/env python3
"""Central donation fund tests (issue #308).

The central fund is one worker-custodied Solana wallet anyone can donate to; a
cron sweeps its whole balance out to the currently-online nodes once an hour.
These tests load the relevant functions straight out of src/entry.py (no Workers
runtime) and drive them against in-memory mocks so the interval gate, even
split, and load-or-create custody are exercised end to end.
"""

import ast
import asyncio
import re
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# SCHEMA_STATEMENTS (the CREATE TABLE/INDEX DDL) was split out of entry.py
# into schema.py; concatenate it so the schema-text assertions below resolve.
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8")
    + "\n" + (ENTRY.parent / "schema.py").read_text(encoding="utf-8"))

SOLANA_RE = re.compile(r"^[1-9A-HJ-NP-Za-km-z]{32,44}$")
HOUR_MS = 60 * 60 * 1000
FUND_ADDR = "Fund1111111111111111111111111111111111111111"
_W = {
    "alice": "A1ice1111111111111111111111111111111111111x",
    "bob": "Bob2222222222222222222222222222222222222222x",
}


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
    return [namespace[name] for name in names]


class _Clock:
    value = 2_000_000_000_000

    @classmethod
    def now(cls):
        return cls.value


def _distribute_env(fund_rec, balance, payees, send_sig="SIG123",
                    is_main=True, now=_Clock.value):
    """Injected globals + capture dict for _distribute_central_fund."""
    _Clock.value = now
    captured = {"transfers": None, "saved": None, "credited": []}

    async def _central_fund_record(_env):
        return fund_rec

    async def _solana_balance_lamports(_env, _addr):
        return balance

    async def _online_payout_addresses(_env):
        return list(payees)

    async def _solana_send_transfers(_env, _from, _secret, transfers):
        captured["transfers"] = list(transfers)
        return send_sig

    async def _save_central_fund(_env, rec):
        captured["saved"] = dict(rec)

    async def _record_funds_received(_env, scope, key, _name, lamports):
        captured["credited"].append((scope, key, int(lamports)))

    extra = {
        "Date": _Clock,
        "SOLANA_RE": SOLANA_RE,
        "SOLANA_SWEEP_FEE_RESERVE_LAMPORTS": 5000,
        "CENTRAL_FUND_DISTRIBUTION_INTERVAL_MS": HOUR_MS,
        "CENTRAL_FUND_MIN_DISTRIBUTION_LAMPORTS": 100_000,
        "_is_main_relay": lambda _e: is_main,
        "_central_fund_record": _central_fund_record,
        "_solana_balance_lamports": _solana_balance_lamports,
        "_online_payout_addresses": _online_payout_addresses,
        "_solana_send_transfers": _solana_send_transfers,
        "_save_central_fund": _save_central_fund,
        "_record_funds_received": _record_funds_received,
    }
    return extra, captured


def _run_distribute(**kw):
    extra, captured = _distribute_env(**kw)
    (fn,) = _load("_distribute_central_fund", extra_globals=extra)
    result = asyncio.run(fn(object()))
    return result, captured


# --- Hourly distribution ----------------------------------------------------

def test_distributes_evenly_across_online_nodes():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    # 1_005_000 - 5000 fee reserve = 1_000_000 transferable, split over 2 nodes.
    result, cap = _run_distribute(
        fund_rec=fund, balance=1_005_000, payees=[_W["alice"], _W["bob"]])
    assert result is True
    assert cap["transfers"] == [(_W["alice"], 500_000), (_W["bob"], 500_000)]
    assert cap["saved"]["last_distribution_sig"] == "SIG123"
    assert cap["saved"]["last_distribution_at"] == _Clock.value
    assert cap["saved"]["last_distribution_lamports"] == 1_000_000
    assert cap["saved"]["last_distribution_payees"] == 2
    # Each node's share is credited to the mainnode board.
    assert cap["credited"] == [
        ("mainnode", _W["alice"], 500_000),
        ("mainnode", _W["bob"], 500_000),
    ]


def test_interval_gate_blocks_within_the_hour():
    now = 5_000_000_000_000
    fund = {"address": FUND_ADDR, "secret": "seed",
            "last_distribution_at": now - (HOUR_MS - 1)}
    result, cap = _run_distribute(
        fund_rec=fund, balance=10_000_000, payees=[_W["alice"]], now=now)
    assert result is False
    assert cap["transfers"] is None  # no transfer attempted


def test_interval_gate_allows_after_the_hour():
    now = 5_000_000_000_000
    fund = {"address": FUND_ADDR, "secret": "seed",
            "last_distribution_at": now - HOUR_MS}
    result, cap = _run_distribute(
        fund_rec=fund, balance=205_000, payees=[_W["alice"]], now=now)
    assert result is True
    assert cap["transfers"] == [(_W["alice"], 200_000)]


def test_first_ever_distribution_needs_no_prior_timestamp():
    fund = {"address": FUND_ADDR, "secret": "seed"}  # last_distribution_at absent
    result, _ = _run_distribute(
        fund_rec=fund, balance=205_000, payees=[_W["alice"]])
    assert result is True


def test_skips_dust_below_minimum():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    # 104_999 - 5000 = 99_999 transferable, below the 100_000 minimum.
    result, cap = _run_distribute(
        fund_rec=fund, balance=104_999, payees=[_W["alice"]])
    assert result is False
    assert cap["transfers"] is None
    assert cap["saved"] is None  # clock not advanced, so a bigger donation still goes out


def test_no_online_nodes_holds_the_funds():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    result, cap = _run_distribute(
        fund_rec=fund, balance=10_000_000, payees=[])
    assert result is False
    assert cap["transfers"] is None
    assert cap["saved"] is None


def test_federated_relay_does_not_distribute():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    result, cap = _run_distribute(
        fund_rec=fund, balance=10_000_000, payees=[_W["alice"]], is_main=False)
    assert result is False
    assert cap["transfers"] is None


def test_send_failure_does_not_advance_the_clock():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    result, cap = _run_distribute(
        fund_rec=fund, balance=10_000_000, payees=[_W["alice"]], send_sig="")
    assert result is False
    assert cap["saved"] is None  # nothing persisted, so it retries next tick


def test_fund_wallet_excluded_from_its_own_payee_split():
    fund = {"address": FUND_ADDR, "secret": "seed"}
    result, cap = _run_distribute(
        fund_rec=fund, balance=205_000, payees=[FUND_ADDR, _W["alice"]])
    assert result is True
    # The fund's own address is filtered out; only alice receives.
    assert cap["transfers"] == [(_W["alice"], 200_000)]


# --- Load-or-create custody -------------------------------------------------

def _fund_record_env(existing_row):
    state = {"row": existing_row, "saved": None}

    async def d1_first(_env, _sql, *_args):
        return state["row"]

    async def d1_run(_env, _sql, blob):
        state["saved"] = blob

    async def decrypt_row(_env, data):
        return data

    async def encrypt_row(_env, rec):
        return {"enc": rec}

    async def _new_solana_keypair():
        return FUND_ADDR, "freshseed"

    extra = {
        "d1_first": d1_first,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "encrypt_row": encrypt_row,
        "_new_solana_keypair": _new_solana_keypair,
    }
    return extra, state


def test_fund_record_reuses_existing_wallet():
    existing = {"data": {"address": FUND_ADDR, "secret": "kept"}}
    extra, state = _fund_record_env(existing)
    fns = _load("_central_fund_record", "_save_central_fund", extra_globals=extra)
    rec = asyncio.run(fns[0](object()))
    assert rec == {"address": FUND_ADDR, "secret": "kept"}
    assert state["saved"] is None  # no new keypair minted


def test_fund_record_mints_and_persists_when_absent():
    extra, state = _fund_record_env(None)
    fns = _load("_central_fund_record", "_save_central_fund", extra_globals=extra)
    rec = asyncio.run(fns[0](object()))
    assert rec == {"address": FUND_ADDR, "secret": "freshseed"}
    # Persisted (encrypted) so the same wallet is reused on the next call.
    assert state["saved"] == {"enc": {"address": FUND_ADDR, "secret": "freshseed"}}


# --- Source contract --------------------------------------------------------

def test_cron_invokes_the_distribution():
    assert "await _distribute_central_fund(self.env)" in ENTRY_TEXT


def test_endpoint_is_wired():
    assert '"/api/accounts/central-fund"' in ENTRY_TEXT
    assert '"/api/federation/central-fund"' in ENTRY_TEXT


def test_schema_creates_the_table():
    assert "CREATE TABLE IF NOT EXISTS central_fund" in ENTRY_TEXT
