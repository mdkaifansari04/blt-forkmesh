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
ITEM_MIGRATION = ROOT / "migrations" / "0099_world_qa_items.sql"


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

    async def org_row(_env, _org):
        return "org:forkmesh", {"name": "forkmesh"}

    async def org_role(_env, _org_bi, _actor):
        return "owner"

    async def org_permission(_env, _org_bi, _actor):
        return "admin"

    async def d1_run(_env, _sql, account_bi, key, verdict, reviewed_at):
        rows[(account_bi, key)] = {
            "item_key": key,
            "verdict": verdict,
            "reviewed_at": reviewed_at,
        }

    async def d1_all(_env, sql, *params):
        if "FROM world_qa_items" in sql:
            return []
        if "GROUP BY item_key,verdict" in sql:
            totals = {}
            for value in rows.values():
                pair = (value["item_key"], value["verdict"])
                totals[pair] = totals.get(pair, 0) + 1
            return [
                {"item_key": key, "verdict": verdict, "count": count}
                for (key, verdict), count in totals.items()
            ]
        if "COUNT(DISTINCT account_bi)" in sql:
            return [{"testers": len({owner for owner, _key in rows})}]
        account_bi = params[0]
        return [
            dict(value)
            for (owner, _key), value in rows.items()
            if owner == account_bi
        ]

    async def d1_first(_env, _sql, *_params):
        return {"priority": 0}

    def response(payload, status=200, **_kwargs):
        return {"payload": payload, "status": status}

    class RequestBodyTooLarge(BaseException):
        pass

    namespace = {
        "Date": _Date,
        "MAX_NODE_NAME": 80,
        "RequestBodyTooLarge": RequestBodyTooLarge,
        "ensure_schema": ensure_schema,
        "method_name": lambda request: request.method,
        "_request_same_origin": lambda request: request.same_origin,
        "bounded_json_request": bounded_json_request,
        "_account_session_record": account_session,
        "_org_row": org_row,
        "_org_role": org_role,
        "_org_permission": org_permission,
        "d1_run": d1_run,
        "d1_all": d1_all,
        "d1_first": d1_first,
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
    item_sql = ITEM_MIGRATION.read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS world_qa_items" in item_sql
    assert "how_to_test TEXT NOT NULL" in item_sql
    assert "world_qa_items" in SCHEMA


def test_qa_votes_are_per_account_with_global_aggregate_totals():
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
    assert saved["payload"]["globalStats"]["pass"] == 1
    assert saved["payload"]["globalStats"]["testers"] == 1
    assert saved["payload"]["globalReviews"]["office-doorway"] == {
        "pass": 1, "fail": 0, "unsure": 0, "total": 1,
    }
    assert ("bi:alice", "office-doorway") in rows

    bob = _run(handler(None, _Request(account="bi:bob")))
    assert bob["payload"]["reviews"] == {}
    assert bob["payload"]["stats"]["reviewed"] == 0
    assert bob["payload"]["globalStats"]["pass"] == 1


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
            "globalStats: this.qaDeck.globalStats",
            "handleQaAction(detail = {})",
            "routeQaCard(target)",
            'action: target === "todo" ? "route_todo" : "route_issue"',
    ):
        assert contract in WORLD
    for contract in (
        "function worldQaCardTexture(",
        'worldQaBoard.name = "forkmesh-world-qa-board"',
        'qaBoardFace.userData.interactive = "world-qa-board"',
        'arrow: "←", label: "FAIL"',
        'arrow: "→", label: "PASS"',
        'arrow: "↓", label: "UNSURE"',
        'color: "#ff3f46"',
        'color: "#27df78"',
        'color: "#aeb7b2"',
        "qaSwipeCues.visible = true",
        'verdict = deltaX > 0 ? "pass" : "fail"',
        'verdict = "unsure"',
        "onQaVerdict({ verdict })",
        "updateQaBoard,",
        "HOW TO TEST",
        "function qaBoardHitAction(",
        'const views = ["cards", "pass", "fail", "unsure"]',
        'return { action: "route", target: "todo" }',
        'return { action: "route", target: "issues" }',
        "SEND TO TODO",
        "SEND TO ISSUES",
        "onQaAction(qaAction)",
    ):
        assert contract in SCENE
    assert "renderQaBoardPanel" not in WORLD
    assert "data-world-qa-verdict" not in WORLD
    assert ".world-qa-playing-card" not in CSS


def test_qa_routing_is_privileged_audited_and_targets_real_work_queues():
    handler = ENTRY[
        ENTRY.index("async def world_qa_handler"):
        ENTRY.index("\n\nWORLD_PREFERENCES_MAX_BYTES")
    ]
    for contract in (
        'action in ("route_todo", "route_issue")',
        "if not can_route:",
        "INSERT INTO world_build_board_items",
        "completed_at=0",
        "_forkbot_enqueue_issue(",
        '"forkmesh",',
        '"QA follow-up: " + title',
        '"world.qa_" + action',
    ):
        assert contract in handler
    board_api = (ROOT / "src" / "world_build_board.py").read_text(
        encoding="utf-8")
    assert '"customTasks": [' in board_api
    assert "payload?.customTasks" in SCENE


def test_completed_build_tasks_can_be_sent_into_the_shared_qa_deck():
    for contract in (
        'action == "send_qa"',
        "INSERT INTO world_qa_items(",
        "how_to_test=excluded.how_to_test",
    ):
        assert contract in (
            ROOT / "src" / "world_build_board.py"
        ).read_text(encoding="utf-8")
    for contract in (
        'context.fillText("DONE → QA"',
        "function buildBoardSendQaHit",
        "onBuildSendQa({",
        "sendBuildTaskToQa(key, title)",
        "action: \"send_qa\"",
    ):
        assert contract in SCENE + WORLD


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
