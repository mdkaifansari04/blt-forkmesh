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
        }
    }
    assert 1_850_000 <= values["CHAT_HISTORY_MAX_BODY"] <= 1_900_000
    assert values["CHAT_HISTORY_MAX_BYTES_PER_ROOM"] == 16 * 1024 * 1024


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
