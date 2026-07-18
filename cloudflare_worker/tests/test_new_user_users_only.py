#!/usr/bin/env python3
"""New users live only in the users table, never legacy accounts (issue #172).

_save_account is the single choke point every user write flows through. For a
user-kind record with no pre-existing accounts row, it must skip the accounts
INSERT entirely and persist only via the users/nodes mirror. Legacy accounts
rows that still exist must keep being updated in place (reads prefer accounts,
so leaving one stale would shadow the fresh users record). Node records are
unaffected.
"""

import ast
import asyncio
from pathlib import Path

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"

FUNCS = {"_save_account", "_account_kind"}


def _load(extra):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [n for n in tree.body
                if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                and n.name in FUNCS]
    assert {n.name for n in selected} == FUNCS, "missing functions"
    mod = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = dict(extra)
    exec(compile(mod, str(ENTRY), "exec"), ns)
    return ns


def _harness(accounts=None):
    accounts = set(accounts or [])   # name_bi values with an existing accounts row
    calls = {"account_inserts": [], "mirrors": []}

    def clean_string(value, _n, *_a, **_k):
        return str(value or "")

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def d1_first(_env, sql, *args):
        if "FROM accounts WHERE name_bi" in sql:
            key = args[0]
            return {"name_bi": key} if key in accounts else None
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO accounts"):
            calls["account_inserts"].append(args[0])

    async def _mirror_account_identity_tables(_env, name_bi, rec, **_k):
        calls["mirrors"].append((name_bi, rec.get("name")))

    ns = _load({
        "clean_string": clean_string,
        "encrypt_row": encrypt_row,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "_mirror_account_identity_tables": _mirror_account_identity_tables,
        "MAX_NODE_NAME": 64,
    })
    return ns, calls


def test_new_user_skips_accounts_insert_but_mirrors_to_users():
    ns, calls = _harness(accounts=[])
    rec = {"name": "alice", "kind": "user", "pass_hash": "h"}
    asyncio.run(ns["_save_account"](object(), "bi:alice", rec, email_bi="e"))
    assert calls["account_inserts"] == []          # never touched accounts
    assert calls["mirrors"] == [("bi:alice", "alice")]  # went into users


def test_existing_accounts_row_is_still_updated():
    ns, calls = _harness(accounts=["bi:legacy"])
    rec = {"name": "legacy", "kind": "user", "pass_hash": "h"}
    asyncio.run(ns["_save_account"](object(), "bi:legacy", rec))
    assert calls["account_inserts"] == ["bi:legacy"]    # legacy row updated in place
    assert calls["mirrors"] == [("bi:legacy", "legacy")]


def test_node_records_still_write_to_accounts():
    ns, calls = _harness(accounts=[])
    rec = {"name": "mirror1", "kind": "node", "pubkey": "pk"}
    asyncio.run(ns["_save_account"](object(), "bi:mirror1", rec))
    assert calls["account_inserts"] == ["bi:mirror1"]   # nodes unchanged
    assert calls["mirrors"] == [("bi:mirror1", "mirror1")]


if __name__ == "__main__":
    test_new_user_skips_accounts_insert_but_mirrors_to_users()
    test_existing_accounts_row_is_still_updated()
    test_node_records_still_write_to_accounts()
    print("ok")
