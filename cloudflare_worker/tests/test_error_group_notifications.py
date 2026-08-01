#!/usr/bin/env python3
"""The first error of a group pings the platform administrators (adhoc #77).

The admin error view groups by (status, method, path, message). The row that
opens a group is the interesting event — "something new is broken" — while the
repeats behind it are noise, so only the first one raises a notification. This
rides the error path, so it must also stay bounded and never raise.

AST-extraction harness in the style of test_account_alert_signed_inbox.py.
"""

import ast
import asyncio
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

FUNCS = {
    "_write_error_log",
    "_notify_new_error_group",
    "_admin_error_source",
}


def _load(extra):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCS
    ]
    found = {node.name for node in selected}
    assert found == FUNCS, "missing: %s" % sorted(FUNCS - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Date:
    @staticmethod
    def now():
        return 1700000000000


def _harness(admins=("root",)):
    rows = []
    sent = []

    async def ensure_schema(_env):
        return None

    async def d1_run(_env, sql, *params):
        if sql.strip().startswith("INSERT INTO error_log"):
            rows.append(params)
        return None

    async def d1_first(_env, sql, *params):
        if "FROM error_log" not in sql:
            return None
        status, method, path, message = params
        for row in rows:
            if (row[1], row[2], row[3], row[4]) == (
                    status, method, path, message):
                return {"hit": 1}
        return None

    async def d1_all(_env, sql, *_params):
        if "is_admin=1" not in sql:
            return []
        return [{"data": name} for name in admins]

    async def decrypt_row(_env, data, key=None):
        return {"name": data}

    async def enqueue_notification(_env, recipient, kind, title, **kwargs):
        sent.append({"recipient": recipient, "kind": kind, "title": title,
                     **kwargs})
        return True

    namespace = _load({
        "ensure_schema": ensure_schema,
        "d1_run": d1_run,
        "d1_first": d1_first,
        "d1_all": d1_all,
        "decrypt_row": decrypt_row,
        "enqueue_notification": enqueue_notification,
        "valid_node_name": lambda name: bool(name) and name.isalnum(),
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "_privacy_safe_error_text": lambda _path, message: message,
        "Date": _Date,
        "MAX_ERROR_LOG": 5000,
        "MAX_NODE_NAME": 63,
    })
    return namespace, object(), rows, sent


def test_the_first_error_of_a_group_notifies_every_administrator():
    namespace, env, rows, sent = _harness(admins=("root", "alice"))
    write = namespace["_write_error_log"]

    asyncio.run(write(env, 500, "GET", "/api/sync", "boom"))
    assert len(rows) == 1
    assert [item["recipient"] for item in sent] == ["root", "alice"]
    assert {item["kind"] for item in sent} == {"error_group"}
    assert "500" in sent[0]["title"]
    assert "/api/sync" in sent[0]["body"]
    assert sent[0]["meta"]["path"] == "/api/sync"


def test_repeats_of_a_known_group_are_silent():
    namespace, env, rows, sent = _harness()
    write = namespace["_write_error_log"]

    for _ in range(4):
        asyncio.run(write(env, 500, "GET", "/api/sync", "boom"))
    assert len(rows) == 4
    assert len(sent) == 1


    asyncio.run(write(env, 500, "GET", "/api/sync", "different boom"))
    assert len(sent) == 2


def test_browser_and_worker_errors_are_labelled_apart():
    namespace, env, _rows, sent = _harness()
    write = namespace["_write_error_log"]

    asyncio.run(write(env, 500, "JS", "/client-error/world", "boom"))
    assert sent[0]["meta"]["errorSource"] == "JavaScript"
    asyncio.run(write(env, 500, "GET", "/api/sync", "boom"))
    assert sent[1]["meta"]["errorSource"] == "Worker"


def test_a_failing_notification_never_breaks_error_logging():
    namespace, env, rows, _sent = _harness()

    async def explode(*_args, **_kwargs):
        raise RuntimeError("relay is down")

    namespace["enqueue_notification"] = explode
    asyncio.run(namespace["_write_error_log"](
        env, 500, "GET", "/api/sync", "boom"))
    assert len(rows) == 1


def test_the_group_is_read_before_the_row_that_would_answer_it():
    body = ENTRY_TEXT[ENTRY_TEXT.index("async def _write_error_log("):]
    body = body[:body.index("def _sanitize_client_error_text")]
    assert body.index("SELECT 1 AS hit FROM error_log") < body.index(
        "INSERT INTO error_log")
    assert "_notify_new_error_group(" in body


def test_error_group_is_a_recognized_notification_kind():
    kinds = ENTRY_TEXT[ENTRY_TEXT.index("NOTIFICATION_KINDS = frozenset({"):]
    kinds = kinds[:kinds.index("})")]
    assert '"error_group"' in kinds
