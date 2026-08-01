#!/usr/bin/env python3
"""ForkBot chat-to-issue contracts."""

import ast
import asyncio
import base64
import json
import re
import tomllib
from pathlib import Path
from urllib.parse import quote

from worker_test_helpers import json_from_request_double


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
    "_forkbot_command_hints_issue",
    "_forkbot_polite_prefix",
    "_forkbot_parse_count_command",
    "_forkbot_parse_list_command",
    "_forkbot_parse_search_command",
    "_forkbot_parse_agent_command",
    "_forkbot_parse_show_command",
    "_forkbot_parse_help_command",
    "_forkbot_help_message",
    "_forkbot_repo_gateway_json",
    "_forkbot_missing_tree",
    "_forkbot_issue_json_path",
    "_forkbot_issue_json_candidates",
    "_forkbot_blob_text",
    "_forkbot_parse_issue_record",
    "_forkbot_recent_issue_numbers",
    "_forkbot_load_issue_records",
    "_forkbot_search_issues",
    "_forkbot_issue_lines",
    "_forkbot_gateway_offline_reply",
    "_forkbot_action_count",
    "_forkbot_action_list",
    "_forkbot_action_search",
    "_forkbot_action_show",
    "_forkbot_action_start_agent",
    "_forkbot_enqueue_agent_request",
    "_forkbot_context_text",
    "js_nullish",
    "log_error",
    "_safe_error_text",
    "_forkbot_issue_title",
    "_forkbot_fallback_issue_fields",
    "_forkbot_json_object_from_text",
    "_forkbot_clean_ai_issue_fields",
    "_forkbot_run_ai",
    "_forkbot_ai_issue_fields",
    "_forkbot_ai_interpret",
    "_forkbot_next_issue_number",
    "_forkbot_attributed_body",
    "_ap_org_alias_owner",
    "_forkbot_rekey_alias_inbox",
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
    "FORKBOT_LIST_DEFAULT",
    "FORKBOT_LIST_MAX",
    "FORKBOT_SEARCH_MAX",
    "FORKBOT_GATEWAY_TIMEOUT_MS",
    "FORKBOT_ISSUE_FIELDS_SCHEMA",
    "FORKBOT_INTENT_SCHEMA",
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
    ns = {"json": json, "re": re, "quote": quote, "asyncio": asyncio,
          "base64": base64, "to_js": lambda value: value,
          "bounded_json_request": json_from_request_double}
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


class _HostResponse:
    def __init__(self, data):
        self._data = data
        self.status = 200

    async def json(self):
        return self._data


class _FakeGateway:
    """Direct-HTTPS gateway fixture for tree/blobs/search responses.
    `tree` answers every /tree read; `trees` (path -> payload) answers per
    path instead, for split open//closed/ layout fixtures."""

    def __init__(self, tree=None, blobs=None, search=None, trees=None):
        self.urls = []
        self._tree = tree
        self._trees = trees
        self._blobs = blobs
        self._search = search

    async def fetch(self, url):
        self.urls.append(url)
        if "/tree?" in url:
            if self._trees is not None:
                from urllib.parse import parse_qs, urlparse
                path = parse_qs(urlparse(url).query).get("path", [""])[0]
                data = self._trees.get(path)
            else:
                data = self._tree
        elif "/blobs?" in url:
            data = self._blobs
        elif "/search?" in url:
            data = self._search
        else:
            data = None
        if data is None:
            raise RuntimeError("host offline")
        return _HostResponse(data)


def _issue_tree(numbers):
    return {"ok": True, "entries": [
        {"type": "tree", "name": str(n)} for n in numbers]}


def _issue_blobs(records):
    blobs = {}
    for record in records:
        path = ".forkmesh/issues/%d/issue-%d.json" % (
            record["number"], record["number"])
        blobs[path] = {"ok": True, "encoding": "utf8",
                       "content": json.dumps(record)}
    return {"ok": True, "blobs": blobs}


def _env_and_calls(ai=None, catalog_issue_max=None, host=None, admins=(),
                   org_node=""):


    calls = {"inserted": [], "contributors": [], "side_effects": [],
             "hostNotifies": [], "rekeyed": []}

    class _Env:
        AI = ai
        FORKBOT_AI_MODEL = ""

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)



    seq = {}

    async def d1_first(_env, sql, *args):
        if "SELECT COUNT(*) AS c FROM issue_inbox" in sql:
            return {"c": 0}
        if "WHERE repo_bi=? AND submitter_bi=?" in sql:
            return {"c": 0}
        if "FROM repositories WHERE key_bi=?" in sql:

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
        if sql.startswith("UPDATE issue_inbox SET repo_bi"):
            calls["rekeyed"].append(args)
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

    def notify_mentions(*args, **kwargs):
        return ("mentions", args, kwargs)

    async def is_admin(_env, name):
        return str(name or "").lower() in {str(a).lower() for a in admins}

    async def org_repo_node(_env, _org, _repo):
        return org_node

    async def notify_repo_host(_env, owner, repo, topic):
        calls["hostNotifies"].append((owner, repo, topic))




    async def capture_sentry_error(*_args, **_kwargs):
        return False

    async def write_error_log(*_args, **_kwargs):
        return None

    runtime = {
        "_is_admin": is_admin,
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
        "_org_repo_node": org_repo_node,
        "notify_repo_host": notify_repo_host,
        "capture_sentry_error": capture_sentry_error,
        "_write_error_log": write_error_log,
        "_public_base_url": lambda _env: "https://forkmesh.test",
        "safe_segment": lambda value: str(value or ""),
    }
    if host is not None:
        runtime["js_fetch"] = host.fetch
    else:
        async def offline_fetch(_url):
            raise RuntimeError("gateway offline")
        runtime["js_fetch"] = offline_fetch
    ns = _load_forkbot(runtime)
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

    many = [{"sender": "u", "text": "m%d" % i} for i in range(50)]
    assert len(fmt(many).splitlines()) == ns["FORKBOT_CONTEXT_MAX_MESSAGES"]


def test_forkbot_chat_handler_queues_default_repo_issue():


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


    assert item["event"]["body"] == (
        "make crash logs searchable\n\n"
        "---\n_Filed by ForkBot at @alice's request via chat._")



    assert calls["hostNotifies"] == [("forkmesh", "forkmesh", "issues")]


def test_forkbot_queues_issues_under_the_org_alias_backing_node():





    env, calls, ns = _env_and_calls(catalog_issue_max=6, org_node="jett")

    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot create an issue to make crash logs searchable",
        "sender": "alice",
    })))

    assert response["status"] == 201
    repo_bi, item, _submitter_bi = calls["inserted"][0]
    assert repo_bi == "bi:jett/forkmesh"

    assert response["data"]["owner"] == "forkmesh"
    assert response["data"]["issueUrl"] == "/forkmesh/forkmesh/issues"
    assert item["titleIfNew"] == "make crash logs searchable"


    assert calls["hostNotifies"] == [("jett", "forkmesh", "issues")]
    pending = [effect for effect in calls["side_effects"]
               if effect[0] == "pending"]
    assert pending and pending[0][1][1] == "jett"


    assert calls["rekeyed"] == [("bi:jett/forkmesh", "bi:forkmesh/forkmesh")]


def test_forkbot_does_not_rekey_when_the_owner_is_a_plain_node():
    env, calls, ns = _env_and_calls()
    asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot open an issue about flaky login tests",
        "sender": "alice",
    })))
    assert calls["inserted"][0][0] == "bi:forkmesh/forkmesh"
    assert calls["rekeyed"] == []


def test_forkbot_agent_requests_reach_the_org_alias_backing_node_owner():
    host = _FakeGateway(
        tree=_issue_tree([4, 5, 6]),
        blobs=_issue_blobs([{"number": 6, "title": "Fix relay retries",
                             "status": "open"}]),
    )
    env, calls, ns = _env_and_calls(host=host, org_node="jett")
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on the most recent issue",
        "sender": "jett",
    })))

    assert response["status"] == 201
    assert response["data"]["action"] == "agent_requested"
    repo_bi, item, _submitter_bi = calls["inserted"][0]
    assert repo_bi == "bi:jett/forkmesh"
    assert item["meta"]["wantsAgent"] is True
    assert calls["hostNotifies"] == [("jett", "forkmesh", "issues")]


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
        "Index the main log so crash reports can be found.\n\n"
        "---\n_Filed by ForkBot via chat._")


def test_forkbot_unknown_command_returns_help_without_enqueueing():

    env, calls, ns = _env_and_calls()

    response = asyncio.run(ns["forkbot_chat_handler"](
        env, _Request({"message": "forkbot what can you do?"})))

    assert response["status"] == 200
    assert response["data"]["action"] == "help"
    assert "open an issue" in response["data"]["botMessage"]
    assert calls["inserted"] == []


def test_forkbot_ai_intent_creates_issue_from_natural_request():


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
        "The login integration test fails intermittently.\n\n"
        "---\n_Filed by ForkBot at @alice's request via chat._")


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

    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 5
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 6

    env.catalog_max = 9
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 10

    env.catalog_max = 2
    assert asyncio.run(alloc(env, "bi:r", "o", "r")) == 11


def test_forkbot_attributed_body_credits_source_and_requester():
    ns = _load_forkbot()
    attribute = ns["_forkbot_attributed_body"]

    assert attribute("fix the thing", "forkbot", "alice") == (
        "fix the thing\n\n---\n_Filed by ForkBot at @alice's request via chat._")

    assert attribute("fix the thing", "forkbot", "forkbot") == (
        "fix the thing\n\n---\n_Filed by ForkBot via chat._")

    assert attribute("", "forkbot", "alice") == (
        "_Filed by ForkBot at @alice's request via chat._")

    assert attribute("mention body", "fediverse", "@bob@example.social") == (
        "mention body")


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

        assert "rememberContext(" in script
        assert "context" in script


def test_forkbot_regex_catches_noun_first_issue_commands():


    ns = _load_forkbot()
    parse = ns["_forkbot_parse_issue_command"]
    assert parse("issue to add more features to FOrkbot") == {
        "description": "add more features to FOrkbot"}
    assert parse("bug: the mirror panel flickers") == {
        "description": "the mirror panel flickers"}
    assert parse("ticket about slow release downloads") == {
        "description": "slow release downloads"}
    assert parse("how do issues work?") is None


def test_forkbot_files_the_raw_request_when_ai_is_unreachable():



    env, calls, ns = _env_and_calls(ai=None)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": ("forkbot you should be able to get issue context and "
                    "create issues - create one for that"),
        "sender": "jett",
    })))

    assert response["status"] == 201
    assert calls["inserted"]
    body = calls["inserted"][0][1]["event"]["body"]
    assert "create issues" in body


def test_forkbot_ai_confident_none_still_returns_help():
    class _AI:
        async def run(self, model, payload):
            return {"response": json.dumps({"intent": "none"})}

    env, calls, ns = _env_and_calls(ai=_AI())
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot create some good vibes in here",
    })))
    assert response["status"] == 200
    assert response["data"]["action"] == "help"
    assert calls["inserted"] == []


def test_forkbot_requests_json_mode_then_falls_back_without_it():


    class _AI:
        def __init__(self):
            self.payloads = []

        async def run(self, model, payload):
            self.payloads.append(payload)
            if "response_format" in payload:
                raise RuntimeError("response_format not supported")
            return {"response": json.dumps({
                "intent": "create_issue",
                "title": "Login timeouts",
                "body": "The login page keeps timing out.",
            })}

    ai = _AI()
    env, calls, ns = _env_and_calls(ai=ai, catalog_issue_max=0)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot the login page keeps timing out, sort it out",
    })))

    assert response["status"] == 201
    assert len(ai.payloads) == 2
    assert "response_format" in ai.payloads[0]
    assert ai.payloads[0]["response_format"]["type"] == "json_schema"
    assert "response_format" not in ai.payloads[1]
    assert calls["inserted"][0][1]["titleIfNew"] == "Login timeouts"


def test_forkbot_handles_json_mode_object_responses():


    class _AI:
        async def run(self, model, payload):
            return {"response": {
                "intent": "create_issue",
                "title": "Add dark mode",
                "body": "Users asked for a dark theme.",
            }}

    env, calls, ns = _env_and_calls(ai=_AI(), catalog_issue_max=0)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot folks keep asking for a dark theme, get it tracked",
    })))
    assert response["status"] == 201
    assert calls["inserted"][0][1]["titleIfNew"] == "Add dark mode"


def test_forkbot_command_hint_matches_work_recording_language():
    ns = _load_forkbot()
    hints = ns["_forkbot_command_hints_issue"]
    assert hints("create one for that")
    assert hints("you should track this bug")
    assert hints("issue to do the thing")
    assert not hints("good morning everyone")
    assert not hints("what's the weather like")


def test_forkbot_parses_list_commands():
    ns = _load_forkbot()
    parse = ns["_forkbot_parse_list_command"]
    assert parse("list issues") == {"count": 5}
    assert parse("list the last few issues") == {"count": 5}
    assert parse("show me the latest 10 issues") == {"count": 10}
    assert parse("please list the last 3 issues") == {"count": 3}
    assert parse("what are the recent issues") == {"count": 5}

    assert parse("list the last 500 issues") == {"count": ns["FORKBOT_LIST_MAX"]}

    assert parse("create an issue to fix retries") is None
    assert parse("issues are piling up") is None


def test_forkbot_parses_count_commands():
    ns = _load_forkbot()
    parse = ns["_forkbot_parse_count_command"]
    assert parse("how many issues are there?") == {}
    assert parse("how many issues are there") == {}
    assert parse("how many issues exist") == {}
    assert parse("how many issues do we have") == {}
    assert parse("how many issues") == {}
    assert parse("what is the number of issues") == {}
    assert parse("issue count?") == {}
    assert parse("please how many issues are there") == {}

    assert parse("how many issues are there about relay retries") is None
    assert parse("list the last 5 issues") is None
    assert parse("create an issue to fix retries") is None


def test_forkbot_counts_issues_from_live_mirror():
    host = _FakeGateway(tree=_issue_tree([1, 2, 3, 4, 5, 6, 7, 8]))
    env, calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot how many issues are there?", "sender": "jett",
    })))
    assert response["status"] == 200
    assert response["data"]["action"] == "issues_counted"
    assert response["data"]["count"] == 8
    assert response["data"]["botMessage"] == (
        "There are 8 issues in forkmesh/forkmesh.")
    assert calls["inserted"] == []

    assert any("/tree?" in url for url in host.urls)
    assert not any("/blobs?" in url for url in host.urls)


def test_forkbot_count_reports_zero_issues_and_offline_host():
    host = _FakeGateway(tree=_issue_tree([]))
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot how many issues are there?",
    })))
    assert response["data"]["action"] == "issues_counted"
    assert response["data"]["count"] == 0
    assert response["data"]["botMessage"] == (
        "No issues have been filed in forkmesh/forkmesh yet.")

    env, _calls, ns = _env_and_calls()
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot how many issues are there?",
    })))
    assert response["data"]["action"] == "issues_unavailable"
    assert "live host" in response["data"]["botMessage"]


def test_forkbot_parses_search_commands():
    ns = _load_forkbot()
    parse = ns["_forkbot_parse_search_command"]
    assert parse("search issues for relay retries") == {
        "query": "relay retries"}
    assert parse("find issues about dark mode") == {"query": "dark mode"}
    assert parse("look for issues mentioning flaky login") == {
        "query": "flaky login"}
    assert parse("search the issues: crash logs") == {"query": "crash logs"}

    assert parse("search for happiness") is None
    assert parse("find the bug in the relay") is None


def test_forkbot_parses_start_agent_commands():
    ns = _load_forkbot()
    parse = ns["_forkbot_parse_agent_command"]
    assert parse("start an agent on the most recent issue") == {
        "issueNumber": 0}
    assert parse("start a coding agent on the latest issue") == {
        "issueNumber": 0}
    assert parse("please run an agent on issue #12") == {"issueNumber": 12}
    assert parse("assign an agent to issue 7") == {"issueNumber": 7}
    assert parse("kick off an agent on the newest issue") == {
        "issueNumber": 0}
    assert parse("start the meeting") is None
    assert parse("the agent crashed") is None


def test_forkbot_parses_show_and_help_commands():
    ns = _load_forkbot()
    show = ns["_forkbot_parse_show_command"]
    assert show("show issue #12") == {"issueNumber": 12}
    assert show("show me the latest issue") == {"issueNumber": 0}
    assert show("what's the status of issue 4") == {"issueNumber": 4}

    assert show("open issue 4") is None
    assert show("show me the latest issues") is None
    helping = ns["_forkbot_parse_help_command"]
    assert helping("help")
    assert helping("what can you do?")
    assert not helping("help me fix the relay")
    assert not helping("create an issue to add help docs")


def test_forkbot_lists_recent_issues_from_live_mirror():
    host = _FakeGateway(
        tree=_issue_tree([1, 2, 3, 4, 5, 6, 7, 8]),
        blobs=_issue_blobs([
            {"number": n, "title": "Issue %d" % n, "status": "open",
             "authorName": "jett"} for n in (8, 7, 6, 5, 4)]),
    )
    env, calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot list the last few issues", "sender": "alice",
    })))

    assert response["status"] == 200
    assert response["data"]["action"] == "issues_listed"
    assert [i["number"] for i in response["data"]["issues"]] == [8, 7, 6, 5, 4]
    message = response["data"]["botMessage"]
    assert "#8 Issue 8 (open, by jett)" in message
    assert "#3" not in message

    assert "list the last N issues" in message
    assert "/forkmesh/forkmesh/issues" in message
    assert calls["inserted"] == []

    assert any("/tree?" in url for url in host.urls)
    assert any("/blobs?" in url for url in host.urls)


def test_forkbot_lists_issues_from_split_open_closed_folders():




    def _blob(path, record):
        return (path, {"ok": True, "encoding": "utf8",
                       "content": json.dumps(record)})

    host = _FakeGateway(
        trees={
            ".forkmesh/issues": {"ok": True, "entries": [
                {"type": "tree", "name": "open"},
                {"type": "tree", "name": "closed"},
                {"type": "tree", "name": "2"},
            ]},
            ".forkmesh/issues/open": _issue_tree([3]),
            ".forkmesh/issues/closed": _issue_tree([1]),
        },
        blobs={"ok": True, "blobs": dict([
            _blob(".forkmesh/issues/open/3/issue-3.json",
                  {"number": 3, "title": "Split open", "status": "open",
                   "authorName": "jett"}),
            _blob(".forkmesh/issues/closed/1/issue-1.json",
                  {"number": 1, "title": "Split closed", "status": "closed",
                   "authorName": "jett"}),
            _blob(".forkmesh/issues/2/issue-2.json",
                  {"number": 2, "title": "Legacy spot", "status": "open",
                   "authorName": "jett"}),
        ])},
    )
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot list the last 3 issues",
    })))
    assert response["data"]["action"] == "issues_listed"
    assert [i["number"] for i in response["data"]["issues"]] == [3, 2, 1]
    message = response["data"]["botMessage"]
    assert "#3 Split open (open, by jett)" in message
    assert "#2 Legacy spot (open, by jett)" in message
    assert "#1 Split closed (closed, by jett)" in message


def test_forkbot_list_honors_requested_count():
    host = _FakeGateway(
        tree=_issue_tree([10, 11, 12]),
        blobs=_issue_blobs([
            {"number": n, "title": "T%d" % n, "status": "open"}
            for n in (12, 11)]),
    )
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot show me the latest 2 issues",
    })))
    assert response["data"]["action"] == "issues_listed"
    assert [i["number"] for i in response["data"]["issues"]] == [12, 11]
    blob_urls = [url for url in host.urls if "/blobs?" in url]
    assert len(blob_urls) == 1 and "issue-10.json" not in blob_urls[0]


def test_forkbot_list_reports_offline_host_instead_of_empty_repo():
    env, calls, ns = _env_and_calls()
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot list the last 5 issues",
    })))
    assert response["data"]["action"] == "issues_unavailable"
    assert "live host" in response["data"]["botMessage"]
    assert calls["inserted"] == []


def test_forkbot_searches_issues_via_host_grep():
    host = _FakeGateway(search={"ok": True, "issues": [
        {"number": 42, "title": "Relay retries drop frames",
         "snippet": "the relay retries forever"},
        {"number": 7, "title": "Retry backoff", "snippet": ""},
    ], "pulls": [], "code": []})
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot search issues for relay retries",
    })))

    assert response["data"]["action"] == "issues_searched"
    assert response["data"]["query"] == "relay retries"
    message = response["data"]["botMessage"]
    assert "#42 Relay retries drop frames" in message
    assert "the relay retries forever" in message
    assert "#7 Retry backoff" in message
    assert any("/search?q=relay%20retries" in url for url in host.urls)


def test_forkbot_search_reports_no_matches():
    host = _FakeGateway(search={"ok": True, "issues": [], "pulls": [],
                             "code": []})
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot find issues about jetpacks",
    })))
    assert response["data"]["action"] == "issues_searched"
    assert response["data"]["issues"] == []
    assert "No issues matching" in response["data"]["botMessage"]


def test_forkbot_shows_one_issue():
    host = _FakeGateway(
        tree=_issue_tree([3, 9]),
        blobs=_issue_blobs([{
            "number": 9, "title": "Clone hangs", "status": "open",
            "authorName": "bob",
            "events": [{"type": "open", "authorName": "bob",
                        "body": "Cloning stalls at 90%."}],
        }]),
    )
    env, _calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot show me the latest issue",
    })))
    assert response["data"]["action"] == "issue_shown"
    assert response["data"]["issue"]["number"] == 9
    message = response["data"]["botMessage"]
    assert "#9 Clone hangs (open, opened by bob)" in message
    assert "Cloning stalls at 90%." in message


def test_forkbot_starts_agent_on_most_recent_issue_for_owner():
    host = _FakeGateway(
        tree=_issue_tree([4, 5, 6]),
        blobs=_issue_blobs([{"number": 6, "title": "Fix relay retries",
                             "status": "open"}]),
    )
    env, calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on the most recent issue",
        "sender": "forkmesh",
    })))

    assert response["status"] == 201
    assert response["data"]["action"] == "agent_requested"
    assert response["data"]["issueNumber"] == 6
    assert "Fix relay retries" in response["data"]["botMessage"]
    assert len(calls["inserted"]) == 1
    _repo_bi, item, submitter_bi = calls["inserted"][0]
    assert submitter_bi == "bi:forkbot"



    assert item["number"] == 6
    assert item["event"]["type"] == "comment"
    assert item["meta"]["wantsAgent"] is True
    assert item["requestedBy"] == "forkmesh"
    assert "forkmesh" in item["event"]["body"]


def test_forkbot_starts_agent_on_named_issue_for_admin():
    host = _FakeGateway(
        tree=_issue_tree([4, 5, 6]),
        blobs=_issue_blobs([{"number": 4, "title": "Old bug",
                             "status": "open"}]),
    )
    env, calls, ns = _env_and_calls(host=host, admins=("jett",))
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on issue #4",
        "sender": "jett",
    })))
    assert response["status"] == 201
    assert response["data"]["issueNumber"] == 4
    assert calls["inserted"][0][1]["number"] == 4


def test_forkbot_denies_agent_start_for_non_owner():
    host = _FakeGateway(tree=_issue_tree([1, 2]))
    env, calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on the latest issue",
        "sender": "mallory",
    })))
    assert response["status"] == 200
    assert response["data"]["action"] == "agent_denied"
    assert calls["inserted"] == []

    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on the latest issue",
    })))
    assert response["data"]["action"] == "agent_denied"
    assert calls["inserted"] == []


def test_forkbot_rejects_agent_start_on_unknown_issue():
    host = _FakeGateway(tree=_issue_tree([1, 2, 3]))
    env, calls, ns = _env_and_calls(host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot start an agent on issue #99",
        "sender": "forkmesh",
    })))
    assert response["data"]["action"] == "agent_denied"
    assert "#99" in response["data"]["botMessage"]
    assert calls["inserted"] == []


def test_forkbot_ai_intent_routes_list_search_and_agent():


    class _AI:
        def __init__(self, intent):
            self._intent = intent

        async def run(self, model, payload):
            return {"response": json.dumps(self._intent)}

    host = _FakeGateway(
        tree=_issue_tree([1, 2, 3]),
        blobs=_issue_blobs([
            {"number": n, "title": "T%d" % n, "status": "open"}
            for n in (3, 2)]),
        search={"ok": True, "issues": [
            {"number": 2, "title": "T2", "snippet": "relay"}]},
    )
    env, _calls, ns = _env_and_calls(
        ai=_AI({"intent": "list_issues", "count": 2}), host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot what came in recently?",
    })))
    assert response["data"]["action"] == "issues_listed"
    assert [i["number"] for i in response["data"]["issues"]] == [3, 2]

    env, _calls, ns = _env_and_calls(
        ai=_AI({"intent": "count_issues"}), host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot so how many total issues do we have logged?",
    })))
    assert response["data"]["action"] == "issues_counted"
    assert response["data"]["count"] == 3

    env, _calls, ns = _env_and_calls(
        ai=_AI({"intent": "search_issues", "query": "relay"}), host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot anything on file about the relay?",
    })))
    assert response["data"]["action"] == "issues_searched"
    assert response["data"]["issues"][0]["number"] == 2

    env, calls, ns = _env_and_calls(
        ai=_AI({"intent": "start_agent", "issueNumber": 0}), host=host)
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot get someone working on that new one",
        "sender": "forkmesh",
    })))
    assert response["data"]["action"] == "agent_requested"
    assert response["data"]["issueNumber"] == 3


def test_forkbot_help_lists_every_capability():
    env, _calls, ns = _env_and_calls()
    response = asyncio.run(ns["forkbot_chat_handler"](env, _Request({
        "message": "forkbot help",
    })))
    assert response["data"]["action"] == "help"
    message = response["data"]["botMessage"]
    for capability in ("open an issue", "list recent issues",
                       "how many issues there are", "search issues",
                       "start a coding agent"):
        assert capability in message


def test_forkbot_desktop_honors_agent_request_comments():




    issues_cpp = (ROOT.parent / "qt_client" / "src" /
                  "MainWindowIssues.cpp").read_text(encoding="utf-8")
    assert "CommentAgentRequest" in issues_cpp
    assert re.search(r"meta\.wantsAgent && number > 0", issues_cpp)
    assert "issueHasAgent" in issues_cpp
