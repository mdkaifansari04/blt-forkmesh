#!/usr/bin/env python3
"""Platform-admin-only permanent World node removal."""

import ast
import asyncio
from pathlib import Path

from worker_test_helpers import json_from_request_double


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WORLD = ROOT / "public" / "world" / "world.js"


class Request:
    method = "POST"

    def __init__(self, payload):
        self._payload = payload

    async def json(self):
        return self._payload


def response(payload, status=200, **kwargs):
    return {"data": payload, "status": status, **kwargs}


def runtime(admin=True):
    node_record = {"name": "mirror6", "owner": "jett", "pubkey": "p" * 64}
    owner_record = {"name": "jett", "nodes": ["mirror6", "mirror7"]}
    writes = []
    audits = []
    saved = []
    deleted = []

    async def account_row(_env, name):
        if name == "mirror6":
            return "bi:mirror6", dict(node_record)
        if name == "jett":
            return "bi:jett", dict(owner_record)
        return "", None

    async def d1_first(_env, sql, *params):
        if "FROM nodes" in sql and params == ("bi:mirror6",):
            return {"node_bi": "bi:mirror6"}
        return None

    async def d1_run(_env, sql, *params):
        writes.append((sql, params))

    async def audit(_env, actor, action, target_type, target, outcome,
                    details=None):
        audits.append((actor, action, target_type, target, outcome, details))

    async def save(_env, key, record):
        saved.append((key, record))

    async def delete(_env, key, record):
        deleted.append((key, record))

    function = next(
        node for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "world_admin_delete_node_handler"
    )
    namespace = {
        "method_name": lambda request: request.method,
        "bounded_json_request": json_from_request_double,
        "json_response": response,
        "_authed_account_name": lambda *_args: asyncio.sleep(
            0, result="jett"),
        "_is_admin": lambda _env, name: asyncio.sleep(
            0, result=admin and name == "jett"),
        "clean_string": lambda value, maximum: str(value or "")[:maximum],
        "MAX_NODE_NAME": 64,
        "valid_node_name": lambda value: bool(value) and value.isalnum(),
        "_audit_sensitive_action": audit,
        "_account_row": account_row,
        "d1_first": d1_first,
        "_owned_nodes": lambda record: list(record.get("nodes") or []),
        "_save_account": save,
        "d1_run": d1_run,
        "_delete_account_namespace": delete,
    }
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=[function], type_ignores=[])), str(ENTRY), "exec"),
         namespace)
    return namespace["world_admin_delete_node_handler"], writes, audits, saved, deleted


def test_non_admin_cannot_delete_a_node():
    handler, writes, audits, saved, deleted = runtime(admin=False)
    result = asyncio.run(handler(None, Request({
        "nodeName": "mirror6",
        "confirmation": "DELETE mirror6",
    })))
    assert result["status"] == 403
    assert not writes and not audits and not saved and not deleted


def test_exact_confirmation_is_required_before_any_mutation():
    handler, writes, audits, saved, deleted = runtime()
    result = asyncio.run(handler(None, Request({
        "nodeName": "mirror6",
        "confirmation": "mirror6",
    })))
    assert result["status"] == 400
    assert result["data"]["requiredConfirmation"] == "DELETE mirror6"
    assert not writes and not audits and not saved and not deleted


def test_admin_delete_unlinks_owner_purges_node_state_and_audits():
    handler, writes, audits, saved, deleted = runtime()
    result = asyncio.run(handler(None, Request({
        "nodeName": "mirror6",
        "confirmation": "DELETE mirror6",
    })))
    assert result["status"] == 200
    assert result["data"]["nodeDeleted"] == "mirror6"
    assert saved == [("bi:jett", {"name": "jett", "nodes": ["mirror7"]})]
    assert any("reward_node_observations" in sql for sql, _ in writes)
    assert any("mirror_https_endpoints" in sql for sql, _ in writes)
    assert deleted == [("bi:mirror6", {
        "name": "mirror6", "owner": "jett", "pubkey": "p" * 64,
    })]
    assert audits[-1][1:5] == (
        "world.node_delete", "node", "mirror6", "success")


def test_world_ui_only_renders_delete_action_for_admins():
    world = WORLD.read_text(encoding="utf-8")
    assert "this.identity?.isAdmin === true" in world
    assert "data-world-admin-delete-node" in world
    assert "node?.machineName || node?.name" in world
    assert "DELETE ${nodeName}" in world
    assert 'this.postJSON("/api/world/admin/nodes/delete"' in world
    assert "await this.loadWorldData({ forceMirrors: true });" in world
    assert 'cache: force ? "no-store" : "default"' in world
    assert "/api/world/admin/nodes/delete" in ENTRY_TEXT
