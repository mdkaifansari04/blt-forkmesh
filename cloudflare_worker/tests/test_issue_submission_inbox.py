#!/usr/bin/env python3
"""Website issue-submission inbox contracts."""

import ast
import asyncio
import base64
import re
from pathlib import Path
from urllib.parse import quote

from worker_test_helpers import json_from_request_double


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8") + "\n" + CATALOG.read_text(encoding="utf-8")

FUNCS = {
    "issues_handler",
    "_inbox_author_over_quota",
    "_best_effort_inbox_side_effect",
    "_clean_issue_attachment_data",
    "method_name",
    "clean_string",
    "repo_web_href",
}


def _load_functions(extra_globals):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCS
    ]
    found = {node.name for node in selected}
    assert found == FUNCS, "missing functions: %s" % sorted(FUNCS - found)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    namespace.setdefault("bounded_json_request", json_from_request_double)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Request:
    method = "POST"
    url = "https://forkmesh.test/api/repo/source/forkmesh/issues"

    def __init__(self, body):
        self._body = body

    async def json(self):
        return self._body


def test_issue_post_succeeds_when_notification_fanout_fails():
    inserted = []

    class _Date:
        @staticmethod
        def now():
            return 1_000_000

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def verify_issue_event(_number, _event):
        return True

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def d1_first(_env, sql, *_args):
        if "SELECT COUNT(*) AS c FROM issue_inbox" in sql:
            return {"c": 0}
        if "WHERE repo_bi=? AND submitter_bi=?" in sql:
            return {"c": 0}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO issue_inbox"):
            inserted.append(args)
            return None
        raise AssertionError("unexpected d1_run: " + sql)

    async def record_contributor(_env, _author, _kind):
        return None

    async def failing_side_effect(*_args, **_kwargs):
        raise RuntimeError("notification backend unavailable")

    async def noop_notify_repo_host(_env, _owner, _repo, _topic):
        return None

    ns = _load_functions({
        "Date": _Date,
        "json_response": json_response,
        "ensure_schema": ensure_schema,
        "blind_index": blind_index,
        "verify_issue_event": verify_issue_event,
        "encrypt_row": encrypt_row,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "_record_contributor": record_contributor,
        "notify_pending_inbox": failing_side_effect,
        "notify_mentions": failing_side_effect,
        "notify_issue_assignees": failing_side_effect,
        "notify_subscribers": failing_side_effect,
        "subscribe_thread": failing_side_effect,
        "notify_repo_host": noop_notify_repo_host,
        # Fediverse publish is best-effort exactly like the notification
        # fan-out: a dead ActivityPub path must never 500 an accepted issue.
        "_ap_publish_repo_event": failing_side_effect,
        "quote": quote,
        "base64": base64,
        "MAX_ISSUE_BYTES": 64 * 1024,
        "MAX_PENDING_ISSUES": 500,
        "MAX_PENDING_PER_AUTHOR": 50,
        "MAX_NODE_NAME": 63,
        "MAX_ISSUE_ATTACHMENTS": 6,
        "MAX_ISSUE_ATTACHMENT_BYTES": 2 * 1024 * 1024,
        "ISSUE_ATTACHMENT_NAME_RE": re.compile(
            r"^[0-9a-f]{8}\.(png|jpg|jpeg|gif|webp|bmp|svg|bin)$"),
    })

    body = {
        "number": 0,
        "titleIfNew": "Mirrors should accept reports",
        "event": {
            "type": "open",
            "author": "web-author",
            "authorName": "mirror-node",
            "title": "Mirrors should accept reports",
            "body": "source is offline, mirror is online",
        },
        "meta": {"assignees": ["source"]},
    }
    response = asyncio.run(
        ns["issues_handler"](object(), _Request(body), "source", "forkmesh"))

    assert response == {"status": 201, "data": {"ok": True}}
    assert len(inserted) == 1
    assert inserted[0][0] == "bi:source/forkmesh"
