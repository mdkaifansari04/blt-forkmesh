#!/usr/bin/env python3
"""Contracts for the shared, administrator-locked Town Square object layout.

Anyone may read the placement document (it is served from the edge cache so
every world load stays one tiny request), but only a platform administrator
can move an object and lock it into a new place for every visitor.
"""

import ast
import asyncio
import importlib.util
import re
import sqlite3
from pathlib import Path

from worker_test_helpers import json_from_request_double


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
SCHEMA = ROOT / "src" / "schema.py"
MIGRATION = ROOT / "migrations" / "0080_world_object_layout.sql"
WORLD_JS = ROOT / "public" / "world" / "world.js"
WORLD_SCENE_JS = ROOT / "public" / "world" / "world-scene.js"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
        and node.name in names
    ]
    assert {node.name for node in selected} == set(names)
    namespace = dict(extra_globals or {})
    namespace.setdefault("bounded_json_request", json_from_request_double)
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[])), str(ENTRY), "exec"),
         namespace)
    return namespace


def _run(value):
    return asyncio.run(value)


class _Date:
    value = 1_700_000_000_000

    @classmethod
    def now(cls):
        return cls.value


class _Request:
    def __init__(self, data=None, method="POST"):
        self._data = data if data is not None else {}
        self.method = method
        self.url = "https://forkmesh.test/api/world/layout"
        self.headers = type(
            "H", (), {"get": staticmethod(lambda *_a, **_k: None)})()

    async def json(self):
        return self._data


def _response(payload, status=200, **kwargs):
    return {"payload": payload, "status": status, **kwargs}


async def _noop(*_args, **_kwargs):
    return None


def _layout_constants():
    return {
        "WORLD_LAYOUT_CACHE_KEY": "https://forkmesh.internal/api/world/layout",
        "WORLD_LAYOUT_TTL": 60,
        "WORLD_LAYOUT_MAX_OBJECTS": 200,
        "WORLD_LAYOUT_MAX_COORDINATE": 500,
        "WORLD_LAYOUT_ID_RE": re.compile(r"[a-z0-9][a-z0-9-]{0,79}"),
    }


def test_schema_and_migration_store_only_object_ids_and_coordinates():
    schema = SCHEMA.read_text(encoding="utf-8")
    migration = MIGRATION.read_text(encoding="utf-8")
    for source in (schema, migration):
        assert "CREATE TABLE IF NOT EXISTS world_object_layout" in source

    db = sqlite3.connect(":memory:")
    db.executescript(migration)
    db.executescript(migration)
    columns = {
        row[1] for row in db.execute("PRAGMA table_info(world_object_layout)")
    }
    assert columns == {"object_id", "x", "z", "updated_by_bi", "updated_at"}


def _handler_runtime(*, admin=True, existing_rows=None, row_count=0):
    writes = []
    audits = []
    cache = {"match": None, "put": [], "deleted": []}
    rows = existing_rows if existing_rows is not None else []

    async def account_session(_env, _request, _data):
        return "bi:admin", {"name": "jett"}

    async def has_role(_env, actor, role):
        assert actor == "jett"
        assert role == "platform_administrator"
        return admin

    async def d1_all(_env, sql, *params):
        assert "world_object_layout" in sql
        return rows

    async def d1_first(_env, sql, *params):
        if "COUNT(*)" in sql:
            return {"n": row_count}
        return None

    async def d1_run(_env, sql, *params):
        writes.append((sql, params))

    async def audit(_env, actor, action, target_type="", target="",
                    outcome="success", details=None):
        audits.append({
            "actor": actor,
            "action": action,
            "target": target,
            "outcome": outcome,
            "details": details or {},
        })

    async def edge_cache_match(_key):
        return cache["match"]

    async def edge_cache_put(key, response):
        cache["put"].append((key, response))

    async def edge_cache_delete(key):
        cache["deleted"].append(key)

    runtime = _load(
        "_world_layout_objects",
        "_world_layout_coordinate",
        "world_layout_handler",
        extra_globals={
            **_layout_constants(),
            "method_name": lambda request: request.method,
            "json_response": _response,
            "_account_session_record": account_session,
            "clean_string": lambda value, limit: str(value or "")[:limit],
            "MAX_NODE_NAME": 64,
            "_has_role": has_role,
            "_audit_sensitive_action": audit,
            "Date": _Date,
            "ensure_schema": _noop,
            "d1_all": d1_all,
            "d1_first": d1_first,
            "d1_run": d1_run,
            "edge_cache_match": edge_cache_match,
            "edge_cache_put": edge_cache_put,
            "edge_cache_delete": edge_cache_delete,
        },
    )
    return runtime["world_layout_handler"], writes, audits, cache


def test_public_get_is_edge_cached_and_returns_bounded_projection():
    handler, _writes, _audits, cache = _handler_runtime(existing_rows=[
        {"object_id": "campfire", "x": 12.5, "z": -3, "updated_at": 4},
    ])
    response = _run(handler(None, _Request(method="GET")))
    assert response["status"] == 200
    assert response["cache_seconds"] == 60
    assert response["payload"]["objects"] == [
        {"id": "campfire", "x": 12.5, "z": -3.0, "updatedAt": 4},
    ]
    assert len(cache["put"]) == 1

    cache["match"] = {"cached": True}
    assert _run(handler(None, _Request(method="GET"))) == {"cached": True}


def test_admin_move_is_validated_persisted_audited_and_purges_cache():
    handler, writes, audits, cache = _handler_runtime()
    response = _run(handler(None, _Request({
        "id": "landmark-fountain", "x": 10.339, "z": -20,
    })))
    assert response["status"] == 200
    assert response["payload"]["ok"] is True
    insert = next(
        row for row in writes if "INSERT INTO world_object_layout" in row[0])
    assert "ON CONFLICT(object_id) DO UPDATE" in insert[0]
    assert insert[1] == (
        "landmark-fountain", 10.34, -20.0, "bi:admin", _Date.value)
    assert cache["deleted"] == ["https://forkmesh.internal/api/world/layout"]
    assert audits[-1]["action"] == "world.layout.move"
    assert audits[-1]["outcome"] == "success"
    assert audits[-1]["target"] == "landmark-fountain"


def test_non_admin_and_invalid_input_cannot_move_objects():
    handler, writes, audits, cache = _handler_runtime(admin=False)
    response = _run(handler(None, _Request({
        "id": "campfire", "x": 1, "z": 2,
    })))
    assert response["status"] == 403
    assert writes == []
    assert cache["deleted"] == []
    assert audits[-1]["outcome"] == "denied"

    handler, writes, _audits, _cache = _handler_runtime()
    for body in (
        {"id": "Bad Id!", "x": 1, "z": 2},
        {"id": "campfire", "x": "10", "z": 2},
        {"id": "campfire", "x": True, "z": 2},
        {"id": "campfire", "x": float("nan"), "z": 2},
        {"id": "campfire", "x": 501, "z": 2},
        {"id": "campfire", "x": 1, "z": float("inf")},
    ):
        response = _run(handler(None, _Request(body)))
        assert response["status"] == 400
    assert writes == []

    handler, writes, _audits, _cache = _handler_runtime(row_count=200)
    response = _run(handler(None, _Request({
        "id": "brand-new-object", "x": 1, "z": 2,
    })))
    assert response["status"] == 409
    assert response["payload"]["error"] == "layout_limit"
    assert writes == []


def test_route_is_registered_for_the_layout_endpoint():
    source = ast.unparse(next(
        node for node in ast.parse(ENTRY_TEXT).body
        if isinstance(node, ast.ClassDef) and node.name == "Default"
    ))
    assert "world_layout_handler" in source


def test_world_frontend_loads_applies_and_admin_locks_the_layout():
    world_js = WORLD_JS.read_text(encoding="utf-8")
    assert "/api/world/layout" in world_js
    assert "applyWorldLayout" in world_js
    assert "lockWorldObjectPlacement" in world_js
    assert "setLayoutEditor?.(this.identity.isAdmin === true)" in world_js

    scene_js = WORLD_SCENE_JS.read_text(encoding="utf-8")
    assert "registerMovableObject" in scene_js
    assert "layoutHandle" in scene_js
    assert "OctahedronGeometry(0.09" in scene_js
    for object_id in (
        "world-bulletin", "arrival-box", "mastodon-kiosk", "campfire",
        "registered-user-lounge", "active-leaderboard-sign",
        "system-capacity-platform",
    ):
        assert '"%s"' % object_id in scene_js
