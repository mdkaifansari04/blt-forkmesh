#!/usr/bin/env python3
"""Pull-inbox wire contracts for portable browser submissions."""

import ast
import asyncio
import hashlib
from pathlib import Path

from worker_test_helpers import json_from_request_double


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
EVENTS = ROOT / "src" / "events.py"
EVENTS_TEXT = EVENTS.read_text(encoding="utf-8")


def _load_pulls_handler(extra_globals):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "pulls_handler"
    ]
    assert len(selected) == 1
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    namespace.setdefault("bounded_json_request", json_from_request_double)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["pulls_handler"]


def _load_pull_verifier(extra_globals):
    tree = ast.parse(EVENTS_TEXT, filename=str(EVENTS))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "verify_pull_event"
    ]
    assert len(selected) == 1
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(EVENTS), "exec"), namespace)
    return namespace["verify_pull_event"]


class _Request:
    method = "POST"
    url = "https://forkmesh.test/api/repo/alice/project/pulls"

    def __init__(self, pull):
        self._body = {"pull": pull}

    async def json(self):
        return self._body


def _runtime(inserted, notifications, verified):
    class _Date:
        @staticmethod
        def now():
            return 1_000_000

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def noop(*_args, **_kwargs):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def d1_first(_env, sql, *_args):
        if "SELECT COUNT(*) AS c FROM pull_inbox" in sql:
            return {"c": 0}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO pull_inbox"):
            inserted.append(args)
            return None
        raise AssertionError("unexpected d1_run: " + sql)

    async def encrypt_row(_env, value):
        return value

    async def verify_pull_event(pull):
        verified.append(dict(pull))
        return True

    async def notify_mentions(
        _env, _owner, _repo, _actor, _title, body, *_args
    ):
        notifications.append(body)

    return {
        "Date": _Date,
        "base64": __import__("base64"),
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "ensure_schema": noop,
        "blind_index": blind_index,
        "verify_pull_event": verify_pull_event,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "encrypt_row": encrypt_row,
        "_inbox_author_over_quota": (
            lambda *_args, **_kwargs: _async_value(False)
        ),
        "_record_contributor": noop,
        "notify_pending_inbox": noop,
        "notify_mentions": notify_mentions,
        "notify_repo_host": noop,
        "_best_effort_inbox_side_effect": noop,
        "_ap_publish_repo_event": lambda *_args, **_kwargs: None,
        "repo_web_href": lambda owner, repo: f"/{owner}/{repo}",
        "patch_file_stats": lambda _patch: [],
        "pull_badge_png": lambda *_args, **_kwargs: b"",
        "clean_string": (
            lambda value, limit=240:
                value.strip()[:limit] if isinstance(value, str) else ""
        ),
        "MAX_ISSUE_BYTES": 64 * 1024,
        "MAX_PULL_BYTES": 100 * 1024 * 1024,
        "MAX_PENDING_PULLS": 200,
        "MAX_NODE_NAME": 63,
    }


async def _async_value(value):
    return value


def test_pull_post_preserves_portable_change_data_and_normalizes_legacy_body():
    inserted = []
    notifications = []
    verified = []
    handler = _load_pulls_handler(
        _runtime(inserted, notifications, verified))
    pull = {
        "title": "Portable browser pull",
        "body": "Description from the browser",
        "base": "main",
        "head": "feature",
        "patch": "diff --git a/a.txt b/a.txt\n+portable\n",
        "commits": "From abc Mon Sep 17 00:00:00 2001\nSubject: [PATCH] portable\n",
        "author": "web-author",
        "authorName": "alice",
        "ts": 123,
        "sig": "signed-change-set",
    }

    response = asyncio.run(
        handler(object(), _Request(pull), "alice", "project"))

    assert response == {"status": 201, "data": {"ok": True}}
    assert len(inserted) == 1
    stored = inserted[0][1]["pull"]
    assert stored["description"] == "Description from the browser"
    assert "body" not in stored
    assert stored["patch"] == pull["patch"]
    assert stored["commits"] == pull["commits"]
    assert verified == [stored]
    assert notifications == ["Description from the browser"]


def test_pull_post_rejects_an_unbounded_description_before_storage():
    inserted = []
    handler = _load_pulls_handler(_runtime(inserted, [], []))
    pull = {
        "title": "Too large",
        "description": "x" * (64 * 1024 + 1),
        "base": "main",
        "head": "feature",
        "patch": "diff",
        "commits": "",
        "author": "web-author",
        "ts": 123,
        "sig": "signature",
    }

    response = asyncio.run(
        handler(object(), _Request(pull), "alice", "project"))

    assert response == {
        "status": 413,
        "data": {"error": "description_too_large"},
    }
    assert inserted == []


def test_legacy_pull_signature_cannot_authenticate_an_appended_commit_series():
    calls = []

    async def sha256_hex(value):
        return hashlib.sha256(value.encode("utf-8")).hexdigest()

    async def accept_only_legacy(_author, _signature, canonical):
        calls.append(canonical)
        # The deliberately permissive fake makes the old four-field fallback
        # succeed. The verifier must never reach it while commits are present.
        return len(calls) == 2

    verify = _load_pull_verifier({
        "sha256_hex": sha256_hex,
        "ed25519_verify": accept_only_legacy,
    })
    pull = {
        "title": "Signed patch",
        "base": "main",
        "head": "feature",
        "patch": "signed patch bytes",
        "commits": "unsigned appended mbox",
        "author": "author",
        "sig": "signature",
        "ts": 123,
    }

    assert asyncio.run(verify(pull)) is False
    assert len(calls) == 1


def test_legacy_four_field_signature_remains_valid_without_commits():
    calls = []

    async def sha256_hex(value):
        return hashlib.sha256(value.encode("utf-8")).hexdigest()

    async def accept_legacy(_author, _signature, canonical):
        calls.append(canonical)
        return len(calls) == 2

    verify = _load_pull_verifier({
        "sha256_hex": sha256_hex,
        "ed25519_verify": accept_legacy,
    })
    pull = {
        "title": "Legacy patch",
        "base": "main",
        "head": "feature",
        "patch": "legacy signed patch",
        "commits": "",
        "author": "author",
        "sig": "signature",
        "ts": 123,
    }

    assert asyncio.run(verify(pull)) is True
    assert len(calls) == 2
