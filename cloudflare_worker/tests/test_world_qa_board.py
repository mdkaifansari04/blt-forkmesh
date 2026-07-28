#!/usr/bin/env python3
"""Account-private one-card World QA bulletin contracts."""

import ast
import asyncio
import json
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY_PATH = ROOT / "src" / "entry.py"
ENTRY = ENTRY_PATH.read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")
MIGRATION = ROOT / "migrations" / "0098_world_qa_reviews.sql"


def _run(awaitable):
    return asyncio.run(awaitable)


class _Date:
    @staticmethod
    def now():
        return 1_700_000_000_000


class _Request:
    def __init__(self, method="GET", data=None, account="bi:alice",
                 same_origin=True):
        self.method = method
        self.data = data or {}
        self.account = account
        self.same_origin = same_origin


def _handler_runtime():
    tree = ast.parse(ENTRY, filename=str(ENTRY_PATH))
    wanted_assignments = {
        "WORLD_QA_DECK_REVISION", "WORLD_QA_CARDS", "WORLD_QA_CARD_KEYS",
    }
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            names = {
                target.id for target in node.targets
                if isinstance(target, ast.Name)
            }
            if names & wanted_assignments:
                selected.append(node)
        elif isinstance(node, ast.AsyncFunctionDef) \
                and node.name == "world_qa_handler":
            selected.append(node)
    assert any(
        isinstance(node, ast.AsyncFunctionDef) for node in selected)
    rows = {}

    async def ensure_schema(_env):
        return None

    async def bounded_json_request(request, _limit):
        return request.data

    async def account_session(_env, request, _data):
        if not request.account:
            return None, None
        return request.account, {"name": request.account.split(":")[-1]}

    async def d1_run(_env, _sql, account_bi, key, verdict, reviewed_at):
        rows[(account_bi, key)] = {
            "item_key": key,
            "verdict": verdict,
            "reviewed_at": reviewed_at,
        }

    async def d1_all(_env, _sql, account_bi):
        return [
            dict(value)
            for (owner, _key), value in rows.items()
            if owner == account_bi
        ]

    def response(payload, status=200, **_kwargs):
        return {"payload": payload, "status": status}

    class RequestBodyTooLarge(BaseException):
        pass

    namespace = {
        "Date": _Date,
        "RequestBodyTooLarge": RequestBodyTooLarge,
        "ensure_schema": ensure_schema,
        "method_name": lambda request: request.method,
        "_request_same_origin": lambda request: request.same_origin,
        "bounded_json_request": bounded_json_request,
        "_account_session_record": account_session,
        "d1_run": d1_run,
        "d1_all": d1_all,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "json_response": response,
    }
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[])),
        str(ENTRY_PATH),
        "exec",
    ), namespace)
    return namespace["world_qa_handler"], rows


def test_qa_schema_is_account_scoped_and_bounded():
    db = sqlite3.connect(":memory:")
    migration = MIGRATION.read_text(encoding="utf-8")
    db.executescript(migration)
    db.executescript(migration)
    columns = {
        row[1] for row in db.execute("PRAGMA table_info(world_qa_reviews)")
    }
    assert columns == {"account_bi", "item_key", "verdict", "reviewed_at"}
    assert "PRIMARY KEY (account_bi, item_key)" in migration
    assert "username" not in migration.lower()
    assert "ip" not in migration.lower().split("create table", 1)[1]
    assert "world_qa_reviews" in SCHEMA


def test_qa_results_are_private_per_account_and_update_totals():
    handler, rows = _handler_runtime()
    anonymous = _run(handler(None, _Request(account="")))
    assert anonymous["status"] == 200
    assert anonymous["payload"]["authenticated"] is False
    assert anonymous["payload"]["reviews"] == {}
    assert len(anonymous["payload"]["cards"]) >= 20

    saved = _run(handler(None, _Request(
        method="POST",
        account="bi:alice",
        data={"key": "office-doorway", "verdict": "pass"},
    )))
    assert saved["status"] == 200
    assert saved["payload"]["authenticated"] is True
    assert saved["payload"]["reviews"]["office-doorway"]["verdict"] == "pass"
    assert saved["payload"]["stats"]["pass"] == 1
    assert saved["payload"]["stats"]["reviewed"] == 1
    assert ("bi:alice", "office-doorway") in rows

    bob = _run(handler(None, _Request(account="bi:bob")))
    assert bob["payload"]["reviews"] == {}
    assert bob["payload"]["stats"]["reviewed"] == 0


def test_qa_write_rejects_cross_origin_unknown_items_and_bad_verdicts():
    handler, rows = _handler_runtime()
    cross_origin = _run(handler(None, _Request(
        method="POST",
        same_origin=False,
        data={"key": "office-doorway", "verdict": "pass"},
    )))
    assert cross_origin["status"] == 403
    for data in (
        {"key": "invented-task", "verdict": "pass"},
        {"key": "office-doorway", "verdict": "perfect"},
    ):
        response = _run(handler(None, _Request(method="POST", data=data)))
        assert response["status"] == 400
    assert rows == {}


def test_world_has_one_direct_physical_card_with_swipes_and_stats():
    for contract in (
        'const WORLD_QA_ENDPOINT = "/api/world/qa"',
        "async refreshQaDeck(",
        "async recordQaVerdict(verdict)",
        "onQaVerdict: ({ verdict }) => void this.recordQaVerdict(verdict)",
        "currentIndex: this.qaCardIndex",
    ):
        assert contract in WORLD
    for contract in (
        "function worldQaCardTexture(",
        'worldQaBoard.name = "forkmesh-world-qa-board"',
        'qaBoardFace.userData.interactive = "world-qa-board"',
        'arrow: "←", label: "FAIL"',
        'arrow: "→", label: "PASS"',
        'arrow: "↓", label: "UNSURE"',
        'verdict = deltaX > 0 ? "pass" : "fail"',
        'verdict = "unsure"',
        "onQaVerdict({ verdict })",
        "updateQaBoard,",
        "HOW TO TEST",
        "PASS ${",
    ):
        assert contract in SCENE
    assert "renderQaBoardPanel" not in WORLD
    assert "data-world-qa-verdict" not in WORLD
    assert ".world-qa-playing-card" not in CSS


def test_exact_view_and_saved_views_live_in_collapsed_right_rail():
    assert "data-world-share-menu" not in WORLD
    assert "data-world-share-current" in WORLD
    assert "shareCurrentWorldView()" in WORLD
    assert "data-world-saved-views-toggle" in WORLD
    assert 'data-expanded="false"' in WORLD
    assert "⌖＋" in WORLD
    assert "setSavedViewsExpanded(expanded)" in WORLD
    assert '<div class="world-saved-view-list" data-world-saved-view-list hidden>' in WORLD
    assert 'addEventListener("contextmenu"' not in SCENE
    assert "handleContextMenu" not in SCENE
    assert ".world-share-view-button" in CSS
