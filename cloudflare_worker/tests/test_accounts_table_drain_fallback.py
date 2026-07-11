#!/usr/bin/env python3
"""Legacy accounts table is drained into users/nodes (adhoc #20).

Making a user/node and the verified-email migration delete a record's accounts
row after mirroring it into the authoritative users/nodes tables. These tests
pin the read-side safety net: name and email lookups must fall back to
users/nodes so a drained account still resolves (login, profile, admin status).
"""

import ast
import asyncio
from pathlib import Path

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"

FUNCS = {"_account_identity_rec_by_bi", "_account_row", "_is_admin"}


def _load(extra):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [n for n in tree.body
                if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                and n.name in FUNCS]
    assert {n.name for n in selected} == FUNCS, "missing functions"
    mod = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = dict(extra)
    # Per-isolate memo the hot-path helpers reference (module-level in
    # entry.py; a fresh one per load keeps tests isolated).
    ns.setdefault("_MIRRORED_ACCOUNT_BIS", set())
    exec(compile(mod, str(ENTRY), "exec"), ns)
    return ns


def _harness(accounts=None, users=None, nodes=None):
    accounts = accounts or {}   # name_bi -> {data, email_bi, ip_bi, is_admin}
    users = users or {}         # user_bi -> {data, email_bi, is_admin}
    nodes = nodes or {}         # node_bi -> {data, name}

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def decrypt_row(_env, stored, key=None):
        return dict(stored) if isinstance(stored, dict) else stored

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def _mirror_account_identity_tables(*_a, **_k):
        return None

    async def d1_first(_env, sql, *args):
        key = args[0] if args else None
        if "FROM accounts WHERE name_bi" in sql:
            return accounts.get(key)
        if "FROM users WHERE user_bi" in sql:
            return users.get(key)
        if "FROM nodes WHERE node_bi" in sql:
            return nodes.get(key)
        raise AssertionError("unexpected d1_first: " + sql)

    return _load({
        "blind_index": blind_index,
        "decrypt_row": decrypt_row,
        "encrypt_row": encrypt_row,
        "d1_first": d1_first,
        "_mirror_account_identity_tables": _mirror_account_identity_tables,
    })


def test_account_row_falls_back_to_users_when_accounts_row_is_gone():
    ns = _harness(users={"bi:alice": {"data": {"name": "alice", "kind": "user"}}})
    name_bi, rec = asyncio.run(ns["_account_row"](object(), "alice"))
    assert name_bi == "bi:alice"
    assert rec == {"name": "alice", "kind": "user"}


def test_account_row_falls_back_to_nodes_when_accounts_row_is_gone():
    ns = _harness(nodes={"bi:mirror1": {"data": {"name": "mirror1", "kind": "node"}}})
    _, rec = asyncio.run(ns["_account_row"](object(), "mirror1"))
    assert rec == {"name": "mirror1", "kind": "node"}


def test_account_row_prefers_accounts_when_present():
    ns = _harness(
        accounts={"bi:alice": {"data": {"name": "alice", "src": "accounts"}}},
        users={"bi:alice": {"data": {"name": "alice", "src": "users"}}},
    )
    _, rec = asyncio.run(ns["_account_row"](object(), "alice"))
    assert rec["src"] == "accounts"


def test_account_row_returns_none_when_nowhere():
    ns = _harness()
    name_bi, rec = asyncio.run(ns["_account_row"](object(), "ghost"))
    assert name_bi == "bi:ghost" and rec is None


def test_is_admin_falls_back_to_users_table():
    ns = _harness(users={"bi:alice": {"is_admin": 1}})
    assert asyncio.run(ns["_is_admin"](object(), "alice")) is True
    assert asyncio.run(ns["_is_admin"](object(), "nobody")) is False
