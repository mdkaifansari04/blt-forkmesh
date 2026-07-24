"""Encrypted chat retention stays below D1 row and per-room byte limits."""

import ast
import asyncio
from pathlib import Path
import sqlite3


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
SOURCE = ENTRY.read_text(encoding="utf-8")


def _history_helpers(db):
    names = {
        "_chat_history_prune_bounds",
        "chat_history_store",
    }
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef) and node.name in names
    ]
    found = {node.name for node in selected}
    assert found == names, "missing: %s" % sorted(names - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))

    async def d1_run(_env, sql, *args):
        db.execute(sql, args)
        db.commit()

    namespace = {
        "CHAT_HISTORY_MAX_BYTES_PER_ROOM": 20,
        "CHAT_HISTORY_MAX_PER_ROOM": 3,
        "d1_run": d1_run,
        "ensure_schema": lambda _env: asyncio.sleep(0),
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _room_class(namespace):
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    node = next(
        item for item in tree.body
        if isinstance(item, ast.ClassDef) and item.name == "ForkMeshRoom"
    )
    module = ast.fix_missing_locations(
        ast.Module(body=[node], type_ignores=[]))
    globals_map = {"DurableObject": object, **namespace}
    exec(compile(module, str(ENTRY), "exec"), globals_map)
    return globals_map["ForkMeshRoom"]


def test_attachment_frame_ceiling_stays_below_d1_row_limit():
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    values = {
        node.targets[0].id: eval(
            compile(ast.Expression(node.value), str(ENTRY), "eval"),
            {"__builtins__": {}},
        )
        for node in tree.body
        if isinstance(node, ast.Assign)
        and len(node.targets) == 1
        and isinstance(node.targets[0], ast.Name)
        and node.targets[0].id in {
            "CHAT_HISTORY_MAX_BODY",
            "CHAT_HISTORY_MAX_BYTES_PER_ROOM",
            "CHAT_HISTORY_INGRESS_MAX_BYTES",
            "CHAT_HISTORY_INGRESS_WINDOW_MS",
        }
    }
    assert 1_850_000 <= values["CHAT_HISTORY_MAX_BODY"] <= 1_900_000
    assert values["CHAT_HISTORY_MAX_BYTES_PER_ROOM"] == 16 * 1024 * 1024
    assert values["CHAT_HISTORY_INGRESS_MAX_BYTES"] <= 8 * 1024 * 1024
    assert values["CHAT_HISTORY_INGRESS_WINDOW_MS"] >= 10 * 1000


def test_store_prunes_oldest_frames_by_count_and_total_bytes():
    db = sqlite3.connect(":memory:")
    db.execute(
        "CREATE TABLE chat_history ("
        "room_key TEXT NOT NULL,msg_id TEXT NOT NULL,ts INTEGER NOT NULL,"
        "body TEXT NOT NULL,PRIMARY KEY(room_key,msg_id))"
    )
    namespace = _history_helpers(db)
    store = namespace["chat_history_store"]

    for number in range(1, 5):
        asyncio.run(store(
            object(), "room-a", str(number), number, str(number) * 8))

    rows = db.execute(
        "SELECT msg_id,body FROM chat_history WHERE room_key='room-a' "
        "ORDER BY ts"
    ).fetchall()
    assert rows == [("3", "3" * 8), ("4", "4" * 8)]

    asyncio.run(store(object(), "room-b", "other", 1, "x" * 8))
    other = db.execute(
        "SELECT msg_id FROM chat_history WHERE room_key='room-b'"
    ).fetchall()
    assert other == [("other",)]


def test_room_retention_ingress_budget_is_byte_based_and_resets_by_window():
    clock = [1_800_000_000_000]

    class _Date:
        @staticmethod
        def now():
            return clock[0]

    room_type = _room_class({
        "CHAT_HISTORY_INGRESS_MAX_BYTES": 10,
        "CHAT_HISTORY_INGRESS_WINDOW_MS": 1_000,
        "Date": _Date,
    })

    class _Storage:
        def __init__(self):
            self.value = None

        async def get(self, key):
            assert key == "chat_history_ingress"
            return self.value

        async def put(self, key, value):
            assert key == "chat_history_ingress"
            self.value = dict(value)

    room = room_type()
    room.ctx = type("Context", (), {"storage": _Storage()})()

    assert asyncio.run(room._retention_ingress_admitted(6)) is True
    assert asyncio.run(room._retention_ingress_admitted(5)) is False
    assert asyncio.run(room._retention_ingress_admitted(4)) is True
    clock[0] += 1_000
    assert asyncio.run(room._retention_ingress_admitted(10)) is True


def test_retention_requires_boolean_true_and_budgets_before_d1_write():
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    room = next(
        item for item in tree.body
        if isinstance(item, ast.ClassDef) and item.name == "ForkMeshRoom"
    )
    method = next(
        item for item in room.body
        if isinstance(item, ast.AsyncFunctionDef)
        and item.name == "_maybe_retain"
    )
    source = ast.unparse(method)

    assert "envelope.get('persist') is True" in source
    assert source.index("_retention_ingress_admitted") < source.index(
        "chat_history_store")
