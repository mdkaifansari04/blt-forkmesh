#!/usr/bin/env python3
"""Identity key rotation (issue #368): the account rebinds to a successor key
only when the *currently bound* key signs the rotation."""

import ast
import asyncio
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"


def _load_rotate(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.AsyncFunctionDef, ast.FunctionDef))
        and node.name == "_account_rotate"
    ]
    assert selected, "missing _account_rotate"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_account_rotate"]


class _Request:
    def __init__(self, body):
        self._body = body

    async def json(self):
        return self._body


def _json_response(data, status=200, **_kwargs):
    return {"status": status, "data": data}


def _harness(rec):
    saved = []

    async def _account_row(_env, name):
        if rec is None:
            return "bi:" + name, None
        return "bi:" + name, dict(rec)

    async def _save_account(_env, name_bi, updated_rec, **_kwargs):
        saved.append((name_bi, dict(updated_rec)))

    async def ed25519_verify(pubkey, sig, _canonical):
        # Only the currently-bound old key with the sentinel signature verifies.
        return sig == "goodsig" and pubkey == "old-pubkey"

    class _Date:
        @staticmethod
        def now():
            return 1783000000000

    handler = _load_rotate(
        {
            "clean_string": lambda v, n=240: str(v or "")[:n],
            "MAX_NODE_NAME": 32,
            "valid_node_name": lambda v: bool(v),
            "valid_node_pubkey": lambda v: bool(v) and v != "invalid",
            "_ts_ok": lambda ts: ts != "stale",
            "_account_row": _account_row,
            "_save_account": _save_account,
            "ed25519_verify": ed25519_verify,
            "json_response": _json_response,
            "Date": _Date,
        }
    )
    return handler, saved


def _bound_account(**overrides):
    rec = {"name": "alice-node", "status": "active", "pubkey": "old-pubkey"}
    rec.update(overrides)
    return rec


def _run(handler, body):
    return asyncio.run(handler(object(), _Request(body)))


def test_rotate_rebinds_to_successor_when_old_key_signs():
    handler, saved = _harness(_bound_account())
    resp = _run(handler, {
        "nodeName": "alice-node",
        "oldPubkey": "old-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "1783000000000",
        "sig": "goodsig",
    })
    assert resp["status"] == 200
    assert resp["data"] == {"ok": True, "nodeName": "alice-node", "pubkey": "new-pubkey"}
    assert len(saved) == 1
    _, rec = saved[0]
    assert rec["pubkey"] == "new-pubkey"
    assert rec["prev_pubkeys"] == ["old-pubkey"]
    assert rec["rotated_at"] == 1783000000000


def test_rotate_rejects_signature_from_a_non_bound_key():
    handler, saved = _harness(_bound_account())
    resp = _run(handler, {
        "nodeName": "alice-node",
        "oldPubkey": "attacker-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "1783000000000",
        "sig": "goodsig",
    })
    assert resp["status"] == 403
    assert resp["data"]["error"] == "not_bound"
    assert saved == []


def test_rotate_rejects_bad_signature_from_the_bound_key():
    handler, saved = _harness(_bound_account())
    resp = _run(handler, {
        "nodeName": "alice-node",
        "oldPubkey": "old-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "1783000000000",
        "sig": "forged",
    })
    assert resp["status"] == 401
    assert resp["data"]["error"] == "bad_signature"
    assert saved == []


def test_rotate_is_idempotent_when_already_bound_to_successor():
    handler, saved = _harness(_bound_account(pubkey="new-pubkey"))
    resp = _run(handler, {
        "nodeName": "alice-node",
        "oldPubkey": "old-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "1783000000000",
        "sig": "forged",
    })
    assert resp["status"] == 200
    assert resp["data"] == {"ok": True, "nodeName": "alice-node", "pubkey": "new-pubkey"}
    assert saved == []


def test_rotate_rejects_stale_request():
    handler, saved = _harness(_bound_account())
    resp = _run(handler, {
        "nodeName": "alice-node",
        "oldPubkey": "old-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "stale",
        "sig": "goodsig",
    })
    assert resp["status"] == 401
    assert resp["data"]["error"] == "stale_request"
    assert saved == []


def test_rotate_requires_an_existing_account():
    handler, saved = _harness(None)
    resp = _run(handler, {
        "nodeName": "ghost-node",
        "oldPubkey": "old-pubkey",
        "newPubkey": "new-pubkey",
        "ts": "1783000000000",
        "sig": "goodsig",
    })
    assert resp["status"] == 404
    assert resp["data"]["error"] == "no_account"
    assert saved == []
