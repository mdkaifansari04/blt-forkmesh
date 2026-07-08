#!/usr/bin/env python3
"""ForkBot chat-to-issue contracts."""

import ast
import asyncio
import json
import re
import tomllib
from pathlib import Path
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
WRANGLER = ROOT / "wrangler.toml"
PUBLIC = ROOT / "public"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
CATALOG_TEXT = CATALOG.read_text(encoding="utf-8")
CHAT_TEXT = (PUBLIC / "chat.js").read_text(encoding="utf-8")
DASHBOARD_CHAT_TEXT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
WRANGLER_DATA = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))

FUNCS = {
    "clean_string",
    "method_name",
    "repo_web_href",
    "_forkbot_extract_mention_command",
    "_forkbot_parse_issue_command",
    "_forkbot_issue_title",
    "_forkbot_fallback_issue_fields",
    "_forkbot_json_object_from_text",
    "_forkbot_clean_ai_issue_fields",
    "_forkbot_ai_issue_fields",
    "_forkbot_enqueue_issue",
    "forkbot_chat_handler",
}

CONSTANTS = {
    "FORKBOT_NAME",
    "FORKBOT_AUTHOR",
    "FORKBOT_DEFAULT_OWNER",
    "FORKBOT_DEFAULT_REPO",
    "FORKBOT_MAX_COMMAND",
    "FORKBOT_AI_DEFAULT_MODEL",
    "MAX_ISSUE_BYTES",
    "MAX_PENDING_ISSUES",
    "MAX_PENDING_PER_AUTHOR",
    "MAX_NODE_NAME",
}


def _load_forkbot(extra_globals=None):
    tree = ast.parse(CATALOG_TEXT + "\n" + ENTRY_TEXT, filename=str(ENTRY))
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if targets & CONSTANTS:
                selected.append(node)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in FUNCS:
            selected.append(node)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = {"json": json, "re": re, "quote": quote, "to_js": lambda value: value}
    if extra_globals:
        ns.update(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


class _Date:
    @staticmethod
    def now():
        return 1_783_463_371_000


class _Request:
    method = "POST"
    url = "https://forkmesh.test/api/forkbot/chat"

    def __init__(self, body):
        self._body = body

    async def json(self):
        return self._body


def _json_response(data, status=200, **_kwargs):
    return {"status": status, "data": data}


def _env_and_calls(ai=None):
    calls = {"inserted": [], "contributors": [], "side_effects": []}

    class _Env:
        AI = ai
        FORKBOT_AI_MODEL = ""

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def d1_first(_env, sql, *_args):
        if "SELECT COUNT(*) AS c FROM issue_inbox" in sql:
            return {"c": 0}
        if "WHERE repo_bi=? AND submitter_bi=?" in sql:
            return {"c": 0}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO issue_inbox"):
            calls["inserted"].append(args)
            return None
        raise AssertionError("unexpected d1_run: " + sql)

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def record_contributor(_env, author, kind):
        calls["contributors"].append((author, kind))

    async def side_effect(awaitable):
        calls["side_effects"].append(awaitable)

    async def inbox_author_over_quota(*_args):
        return False

    def notify_pending_inbox(*args):
        return ("pending", args)

    def notify_mentions(*args):
        return ("mentions", args)

    ns = _load_forkbot({
        "Date": _Date,
        "json_response": _json_response,
        "ensure_schema": ensure_schema,
        "blind_index": blind_index,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "encrypt_row": encrypt_row,
        "_record_contributor": record_contributor,
        "_best_effort_inbox_side_effect": side_effect,
        "_inbox_author_over_quota": inbox_author_over_quota,
        "notify_pending_inbox": notify_pending_inbox,
        "notify_mentions": notify_mentions,
    })
    return _Env(), calls, ns


def test_forkbot_parses_explicit_issue_mentions_only():
    ns = _load_forkbot()
    extract = ns["_forkbot_extract_mention_command"]
    parse = ns["_forkbot_parse_issue_command"]

    assert extract("forkbot create an issue to fix relay retries") == (
        "create an issue to fix relay retries")
    assert extract("@forkbot: file issue about failed tests") == (
        "file issue about failed tests")
    assert extract("notforkbot create an issue to nope") == ""
    assert parse("create an issue to fix relay retries") == {
        "description": "fix relay retries"
    }
    assert parse("please open issue: worker logs dropped") == {
        "description": "worker logs dropped"
    }
    assert parse("tell me a joke") is None


def test_forkbot_chat_handler_queues_default_repo_issue():
    env, calls, ns = _env_and_calls()

    response = asyncio.run(ns["forkbot_chat_handler"](
        env,
        _Request({
            "message": "forkbot create an issue to make crash logs searchable",
            "sender": "alice",
        }),
    ))

    assert response["status"] == 201
    assert response["data"]["owner"] == "forkmesh"
    assert response["data"]["repo"] == "forkmesh"
    assert response["data"]["title"] == "make crash logs searchable"
    assert "owner node syncs the issue inbox" in response["data"]["botMessage"]

    assert len(calls["inserted"]) == 1
    repo_bi, item, submitter_bi = calls["inserted"][0]
    assert repo_bi == "bi:forkmesh/forkmesh"
    assert submitter_bi == "bi:forkbot"
    assert item["number"] == 0
    assert item["titleIfNew"] == "make crash logs searchable"
    assert item["submitter"] == "forkbot"
    assert item["requestedBy"] == "alice"
    assert item["meta"]["labels"] == ["forkbot"]
    assert item["event"]["type"] == "open"
    assert item["event"]["author"] == "forkbot"
    assert item["event"]["authorName"] == "forkbot"
    assert item["event"]["sig"] == ""
    assert item["event"]["body"] == "make crash logs searchable"


def test_forkbot_uses_workers_ai_for_issue_title_and_body_when_available():
    class _AI:
        async def run(self, model, payload):
            self.model = model
            self.payload = payload
            return {
                "response": json.dumps({
                    "title": "Make logs searchable",
                    "body": "Index the main log so crash reports can be found.",
                })
            }

    ai = _AI()
    env, calls, ns = _env_and_calls(ai=ai)

    response = asyncio.run(ns["forkbot_chat_handler"](
        env,
        _Request({"message": "forkbot create an issue to logs need search"}),
    ))

    assert response["status"] == 201
    assert ai.model == "@cf/meta/llama-3.1-8b-instruct"
    assert ai.payload["messages"][0]["role"] == "system"
    assert calls["inserted"][0][1]["titleIfNew"] == "Make logs searchable"
    assert calls["inserted"][0][1]["event"]["body"] == (
        "Index the main log so crash reports can be found.")


def test_forkbot_unknown_command_returns_help_without_enqueueing():
    env, calls, ns = _env_and_calls()

    response = asyncio.run(ns["forkbot_chat_handler"](
        env, _Request({"message": "forkbot what can you do?"})))

    assert response["status"] == 200
    assert response["data"]["action"] == "help"
    assert "forkbot create an issue" in response["data"]["botMessage"]
    assert calls["inserted"] == []


def test_forkbot_route_and_workers_ai_binding_are_configured():
    assert '"/api/forkbot/chat"' in ENTRY_TEXT
    assert "return await forkbot_chat_handler(self.env, request)" in ENTRY_TEXT
    assert WRANGLER_DATA["ai"]["binding"] == "AI"
    assert WRANGLER_DATA["vars"]["FORKBOT_AI_MODEL"].startswith("@cf/")


def test_web_chats_forward_mentions_and_broadcast_bot_replies():
    for script in (CHAT_TEXT, DASHBOARD_CHAT_TEXT):
        assert 'const FORKBOT_ENDPOINT = "/api/forkbot/chat";' in script
        assert "FORKBOT_MENTION_RE" in script
        assert "async function maybeAskForkbot(text)" in script
        assert "fetch(FORKBOT_ENDPOINT" in script
        assert 'sender: "forkbot"' in script
        assert 'senderId: FORKBOT_SENDER_ID' in script
        assert "broadcastForkbotMessage(data.botMessage)" in script
        assert "maybeAskForkbot(clipped)" in script
