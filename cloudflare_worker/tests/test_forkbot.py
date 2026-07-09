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
    "_forkbot_context_text",
    "_forkbot_issue_title",
    "_forkbot_fallback_issue_fields",
    "_forkbot_json_object_from_text",
    "_forkbot_clean_ai_issue_fields",
    "_forkbot_run_ai",
    "_forkbot_ai_issue_fields",
    "_forkbot_ai_interpret",
    "_forkbot_next_issue_number",
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
    "FORKBOT_CONTEXT_MAX_MESSAGES",
    "FORKBOT_CONTEXT_MAX_CHARS",
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


def _env_and_calls(ai=None, catalog_issue_max=None):
    calls = {"inserted": [], "contributors": [], "side_effects": []}

    class _Env:
        AI = ai
        FORKBOT_AI_MODEL = ""

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    # Per-repo issue-number allocator state (issue_seq table). Seeded from the
    # catalog record's issueMaxNumber the first time a repo is used.
    seq = {}

    async def d1_first(_env, sql, *args):
        if "SELECT COUNT(*) AS c FROM issue_inbox" in sql:
            return {"c": 0}
        if "WHERE repo_bi=? AND submitter_bi=?" in sql:
            return {"c": 0}
        if "FROM repositories WHERE key_bi=?" in sql:
            # A stored catalog record whose issueMaxNumber seeds the allocator.
            return {"data": {"issueMaxNumber": str(catalog_issue_max)}} \
                if catalog_issue_max is not None else None
        if "SELECT next_number FROM issue_seq" in sql:
            return {"next_number": seq.get(args[0], 1)}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO issue_inbox"):
            calls["inserted"].append(args)
            return None
        if sql.startswith("INSERT INTO issue_seq"):
            repo_bi, seed_next = args[0], args[1]
            seq[repo_bi] = max(seq.get(repo_bi, 0), seed_next)
            return None
        if sql.startswith("UPDATE issue_seq"):
            seq[args[1]] = args[0]
            return None
        raise AssertionError("unexpected d1_run: " + sql)

    async def decrypt_row(_env, data):
        return dict(data) if isinstance(data, dict) else None

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
        "decrypt_row": decrypt_row,
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


def test_forkbot_regex_recognizes_natural_phrasings():
    # The offline fast path now covers common natural wordings, not just
    # "create/open/file/make/report an issue".
    ns = _load_forkbot()
    parse = ns["_forkbot_parse_issue_command"]
    assert parse("can you log a bug about the flaky login test") == {
        "description": "the flaky login test"}
    assert parse("track a task to add dark mode") == {
        "description": "add dark mode"}
    assert parse("please raise a ticket regarding slow clones") == {
        "description": "slow clones"}
    assert parse("open a feature request for saved searches") == {
        "description": "saved searches"}
    # Still not an issue request.
    assert parse("what's the weather") is None


def test_forkbot_context_text_bounds_and_formats_conversation():
    ns = _load_forkbot()
    fmt = ns["_forkbot_context_text"]
    assert fmt(None) == ""
    assert fmt([]) == ""
    out = fmt([
        {"sender": "alice", "text": "the login page 500s on submit"},
        {"sender": "bob", "text": "yeah since the auth deploy"},
        {"not": "a dict"},
        {"sender": "carol", "text": "   "},
    ])
    assert out == (
        "alice: the login page 500s on submit\n"
        "bob: yeah since the auth deploy")
    # Never exceeds the message cap.
    many = [{"sender": "u", "text": "m%d" % i} for i in range(50)]
    assert len(fmt(many).splitlines()) == ns["FORKBOT_CONTEXT_MAX_MESSAGES"]


def test_forkbot_chat_handler_queues_default_repo_issue():
    # Catalog says the repo's highest issue number is 6, so the proposed number
    # (what the desktop's nextNumber() would assign) is 7.
    env, calls, ns = _env_and_calls(catalog_issue_max=6)

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
    assert response["data"]["issueNumber"] == 7
    assert response["data"]["issueUrl"] == "/forkmesh/forkmesh/issues"
    assert "#7" in response["data"]["botMessage"]
    assert "/forkmesh/forkmesh/issues" in response["data"]["botMessage"]

    assert len(calls["inserted"]) == 1
    repo_bi, item, submitter_bi = calls["inserted"][0]
    assert repo_bi == "bi:forkmesh/forkmesh"
    assert submitter_bi == "bi:forkbot"
    # The proposed number lands in the stored item so the desktop honors it and
    # the chat reply can echo it.
    assert item["number"] == 7
    assert item["issueNumber"] == 7
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
    # No AI configured and no regex match -> help, nothing enqueued.
    env, calls, ns = _env_and_calls()

    response = asyncio.run(ns["forkbot_chat_handler"](
        env, _Request({"message": "forkbot what can you do?"})))

    assert response["status"] == 200
    assert response["data"]["action"] == "help"
    assert "open an issue" in response["data"]["botMessage"]
    assert calls["inserted"] == []


def test_forkbot_ai_intent_creates_issue_from_natural_request():
    # The wording matches no command regex; the AI classifier recognizes the
    # intent from meaning and drafts the issue.
    class _AI:
        async def run(self, model, payload):
            self.payload = payload
            return {"response": json.dumps({
                "intent": "create_issue",
                "title": "Flaky login test",
                "body": "The login integration test fails intermittently.",
            })}

    env, calls, ns = _env_and_calls(ai=_AI(), catalog_issue_max=3)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot the login test keeps flaking, can you sort that out?",
        "sender": "alice",
    })))

    assert response["status"] == 201
    assert response["data"]["issueNumber"] == 4
    assert calls["inserted"][0][1]["titleIfNew"] == "Flaky login test"
    assert calls["inserted"][0][1]["event"]["body"] == (
        "The login integration test fails intermittently.")


def test_forkbot_ai_intent_none_returns_help_without_enqueueing():
    class _AI:
        async def run(self, model, payload):
            return {"response": json.dumps({"intent": "none"})}

    env, calls, ns = _env_and_calls(ai=_AI())
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot good morning, how are you?",
    })))

    assert response["status"] == 200
    assert response["data"]["action"] == "help"
    assert calls["inserted"] == []


def test_forkbot_forwards_conversation_context_to_the_ai():
    # "log that" only makes sense with the prior conversation; the recent
    # messages must reach the AI prompt.
    class _AI:
        async def run(self, model, payload):
            self.payload = payload
            return {"response": json.dumps({
                "intent": "create_issue",
                "title": "Clone hangs on large repos",
                "body": "Cloning a large repo stalls at 90%.",
            })}

    ai = _AI()
    env, calls, ns = _env_and_calls(ai=ai, catalog_issue_max=0)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot log that as an issue",
        "sender": "alice",
        "context": [
            {"sender": "bob", "text": "cloning the monorepo hangs at 90%"},
            {"sender": "alice", "text": "same here, never finishes"},
        ],
    })))

    assert response["status"] == 201
    assert response["data"]["issueNumber"] == 1
    user_prompt = ai.payload["messages"][1]["content"]
    assert "cloning the monorepo hangs at 90%" in user_prompt
    assert "log that as an issue" in user_prompt


def test_forkbot_issue_numbers_increment_and_reanchor_to_catalog():
    seq = {}

    async def d1_first(_env, sql, *args):
        if "FROM repositories WHERE key_bi=?" in sql:
            return {"data": {"issueMaxNumber": str(_env.catalog_max)}}
        if "SELECT next_number FROM issue_seq" in sql:
            return {"next_number": seq.get(args[0], 1)}
        raise AssertionError(sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO issue_seq"):
            seq[args[0]] = max(seq.get(args[0], 0), args[1])
        elif sql.startswith("UPDATE issue_seq"):
            seq[args[1]] = args[0]
        else:
            raise AssertionError(sql)

    async def decrypt_row(_env, data):
        return dict(data)

    g = _load_forkbot({
        "d1_first": d1_first, "d1_run": d1_run, "decrypt_row": decrypt_row,
    })
    alloc = g["_forkbot_next_issue_number"]

    class _Env:
        catalog_max = 4

    env = _Env()
    # Seeds from catalog max (4) -> proposes 5, then 6 monotonically.
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 5
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 6
    # The desktop merged up to 9 and republished; the allocator re-anchors up.
    env.catalog_max = 9
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 10
    # A stale/lower catalog max never drags the counter backward.
    env.catalog_max = 2
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 11


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
        # Recent conversation is buffered and forwarded so ForkBot has context.
        assert "rememberContext(" in script
        assert "context" in script
