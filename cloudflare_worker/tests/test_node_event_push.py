#!/usr/bin/env python3
"""ForkMeshNodes — the per-owner node event push channel.

notify_repo_host fans a payload-free {"type":"event","topic"} frame out to the
owner's connected desktop/headless nodes the moment a web submission lands;
each node answers with its one signed GET /api/sync. The channel is advisory
only: it must never carry repository bytes, inbox payloads, or control
commands, and a notify failure must never fail the write that triggered it.
"""

import ast
import asyncio
import json
import re
import tomllib
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import parse_qs, quote, unquote, urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
CATALOG = ROOT / "src" / "catalog.py"
WRANGLER = ROOT / "wrangler.toml"
ENTRY_TEXT = (ENTRY.read_text(encoding="utf-8") + "\n"
              + CATALOG.read_text(encoding="utf-8"))

NAMES = {
    "notify_repo_host",
    "node_events_handler",
    "_node_events_do_name",
    "ForkMeshNodes",
    "safe_segment",
    "clean_string",
    "_ws_attachment",
    "_ws_attr",
}
CONSTANTS = {
    "NODE_EVENT_MAX_SOCKETS",
    "NODE_SOCKET_STALE_MS",
    "NODE_EVENT_MSG_WINDOW_MS",
    "NODE_EVENT_MSG_MAX_PER_WINDOW",
    "NODE_EVENT_MAX_FRAME_BYTES",
    "HOST_COUNT_TIMEOUT_MS",
    "EXPECTED_DEGRADED_HEADERS",
    "EXPECTED_DEGRADED_HEADER",
    # safe_segment's dependencies from catalog.py
    "MAX_REPO_SEGMENT",
    "ROOM_NAME_RE",
}

NOW = 1_000_000_000


def _load(extra_globals):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = []
    found = set()
    for node in tree.body:
        if (isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                              ast.ClassDef))
                and node.name in NAMES):
            selected.append(node)
            found.add(node.name)
        elif isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id in CONSTANTS
                for target in node.targets):
            selected.append(node)
    assert found == NAMES, "missing: %s" % sorted(NAMES - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _source_of(name):
    for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body:
        if (isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                              ast.ClassDef)) and node.name == name):
            return ast.unparse(node)
    raise AssertionError("%s not found in entry.py" % name)


class _Headers:
    def __init__(self, values=None):
        self.values = {str(k).lower(): v for k, v in (values or {}).items()}

    def get(self, name):
        return self.values.get(str(name).lower())


class _Request:
    def __init__(self, url, headers=None):
        self.url = url
        self.headers = _Headers(headers)


class _Sock:
    def __init__(self, attachment=None, fail_send=False):
        self.attachment = attachment
        self.fail_send = fail_send
        self.sent = []
        self.closed = None

    def serializeAttachment(self, value):
        self.attachment = value

    def deserializeAttachment(self):
        return self.attachment

    def send(self, frame):
        if self.fail_send:
            raise RuntimeError("socket gone")
        self.sent.append(frame)

    def close(self, code=1000, reason=""):
        self.closed = (code, reason)


class _Ctx:
    def __init__(self):
        self.tagged = []  # (tags, socket)

    def acceptWebSocket(self, server, tags):
        self.tagged.append((list(tags), server))

    def getWebSockets(self, tag=None):
        return [sock for tags, sock in self.tagged
                if tag is None or tag in tags]


def _to_js(value, **_kwargs):
    if isinstance(value, dict):
        return SimpleNamespace(**value)
    return value


class _JsResponse:
    @staticmethod
    def new(_body, opts):
        return {"status": getattr(opts, "status", None),
                "webSocket": getattr(opts, "webSocket", None)}


class _WebSocketPair:
    @staticmethod
    def new():
        pair = SimpleNamespace(client=_Sock(), server=_Sock())

        class _Values:
            def object_values(self):
                return pair.client, pair.server

        return _Values()


class DurableObject:
    def __init__(self, ctx=None, env=None):
        self.ctx = ctx
        self.env = env


def _json_response(data, status=200, cache_seconds=None, cache_control=None,
                   extra_headers=None):
    return {"status": status, "data": data,
            "headers": dict(extra_headers or {})}


def _base_globals():
    socket_ids = iter(range(1, 100))
    return {
        "asyncio": asyncio,
        "json": json,
        "re": re,
        "parse_qs": parse_qs,
        "quote": quote,
        "unquote": unquote,
        "urlparse": urlparse,
        "Date": SimpleNamespace(now=staticmethod(lambda: NOW)),
        "to_js": _to_js,
        "json_response": _json_response,
        "new_socket_id": lambda: "sock-%d" % next(socket_ids),
        "JsResponse": _JsResponse,
        "WebSocketPair": _WebSocketPair,
        "DurableObject": DurableObject,
    }


def _nodes_binding(idFromName_calls, fetch_calls, fetch_error=None,
                   fetch_result=None):
    class _Stub:
        async def fetch(self, url):
            fetch_calls.append(url)
            if fetch_error is not None:
                raise fetch_error
            return fetch_result if fetch_result is not None else {"ok": True}

    binding = SimpleNamespace()
    binding.idFromName = lambda name: (idFromName_calls.append(name)
                                       or "id:" + name)
    binding.get = lambda _id: _Stub()
    return binding


# --- notify_repo_host --------------------------------------------------------

def test_notify_pushes_one_payload_free_frame_to_the_owner_channel():
    ids, fetches = [], []
    env = SimpleNamespace(FORKMESH_NODES=_nodes_binding(ids, fetches))
    ns = _load(_base_globals())
    asyncio.run(ns["notify_repo_host"](env, "Alice", "widgets", "issues"))
    assert ids == ["nodes:alice"]
    assert len(fetches) == 1
    url = urlparse(fetches[0])
    assert url.path == "/api/nodes/notify"
    query = parse_qs(url.query)
    assert query["topic"] == ["issues"]
    assert query["repo"] == ["Alice/widgets"]


def test_notify_failure_never_escapes_and_bad_segments_are_dropped():
    ids, fetches = [], []
    env = SimpleNamespace(FORKMESH_NODES=_nodes_binding(
        ids, fetches, fetch_error=RuntimeError("DO unavailable")))
    ns = _load(_base_globals())
    # A dead Durable Object must not fail the issue/PR write that notified.
    asyncio.run(ns["notify_repo_host"](env, "alice", "widgets", "issues"))
    assert ids == ["nodes:alice"]
    # Invalid segments never even select a Durable Object.
    ids.clear()
    asyncio.run(ns["notify_repo_host"](env, "", "widgets", "issues"))
    asyncio.run(ns["notify_repo_host"](env, "alice", "../etc", "issues"))
    assert ids == []


def test_write_paths_stay_wired_to_the_event_push():
    # The push is only instant if the submission handlers keep calling it.
    entry_only = ENTRY.read_text(encoding="utf-8")
    calls = entry_only.count("notify_repo_host(env, owner, repo,")
    assert calls >= 6, "web submission paths no longer notify the owner node"
    for topic in ("issues", "pulls", "commits", "discussions", "agents",
                  "about"):
        assert 'notify_repo_host(env, owner, repo, "%s")' % topic in entry_only


def test_channel_is_advisory_only():
    # The DO and its notifier must never touch D1, decrypt anything, or move
    # request bodies: a compromised relay can at most trigger a signed sync.
    for name in ("ForkMeshNodes", "notify_repo_host", "node_events_handler"):
        source = _source_of(name)
        assert "d1_" not in source
        assert "decrypt" not in source
        assert "request.bytes" not in source
        assert "request.text" not in source
        assert "request.json" not in source
    assert "'type': 'event'" in _source_of("ForkMeshNodes")


# --- /api/nodes/events routing ----------------------------------------------

def _events_request(upgrade="websocket"):
    headers = {"upgrade": upgrade} if upgrade else {}
    return _Request(
        "https://forkmesh.com/api/nodes/events?owner=Alice&ts=1&sig=s",
        headers)


def test_events_route_requires_websocket_upgrade():
    ns = _load({**_base_globals(),
                "_authorize_owner": None})  # must not be reached
    result = asyncio.run(ns["node_events_handler"](
        SimpleNamespace(), _events_request(upgrade=None)))
    assert result["status"] == 426


def test_events_route_rejects_unsigned_upgrades_before_do_selection():
    ids, fetches = [], []
    env = SimpleNamespace(FORKMESH_NODES=_nodes_binding(ids, fetches))

    async def deny(_env, _request, _owner):
        return False

    ns = _load({**_base_globals(), "_authorize_owner": deny})
    result = asyncio.run(ns["node_events_handler"](env, _events_request()))
    assert result["status"] == 401
    assert ids == [] and fetches == []


def test_events_route_forwards_authorized_upgrades_to_the_owner_do():
    ids, fetches = [], []
    env = SimpleNamespace(FORKMESH_NODES=_nodes_binding(
        ids, fetches, fetch_result={"status": 101}))
    seen = []

    async def allow(_env, _request, owner):
        seen.append(owner)
        return True

    async def durable_object_request(request):
        return request

    ns = _load({**_base_globals(),
                "_authorize_owner": allow,
                "durable_object_request": durable_object_request})
    result = asyncio.run(ns["node_events_handler"](env, _events_request()))
    assert result == {"status": 101}
    assert seen == ["Alice"]
    assert ids == ["nodes:alice"]
    assert len(fetches) == 1


def test_events_route_retries_then_answers_a_retryable_503():
    ids, fetches, logged = [], [], []
    env = SimpleNamespace(FORKMESH_NODES=_nodes_binding(
        ids, fetches, fetch_error=RuntimeError("aborted")))

    async def allow(_env, _request, _owner):
        return True

    async def durable_object_request(request):
        return request

    async def log_durable_object_abort(_env, _request, path, error):
        logged.append((path, str(error)))

    ns = _load({**_base_globals(),
                "_authorize_owner": allow,
                "durable_object_request": durable_object_request,
                "log_durable_object_abort": log_durable_object_abort})
    result = asyncio.run(ns["node_events_handler"](env, _events_request()))
    assert result["status"] == 503
    assert len(fetches) == 2  # one retry against a fresh stub
    assert logged == [("/api/nodes/events", "aborted")]
    assert result["headers"] == ns["EXPECTED_DEGRADED_HEADERS"]


# --- ForkMeshNodes Durable Object -------------------------------------------

def _make_do(ns):
    do = ns["ForkMeshNodes"](_Ctx(), SimpleNamespace())
    return do


def test_do_notify_fans_out_to_live_sockets_and_reaps_stale_ones():
    ns = _load(_base_globals())
    do = _make_do(ns)
    live = _Sock(SimpleNamespace(id="a", last=NOW))
    dead_send = _Sock(SimpleNamespace(id="b", last=NOW), fail_send=True)
    stale = _Sock(SimpleNamespace(
        id="c", last=NOW - ns["NODE_SOCKET_STALE_MS"] - 1))
    for sock in (live, dead_send, stale):
        do.ctx.tagged.append((["node"], sock))
    result = asyncio.run(do.fetch(_Request(
        "https://forkmesh.internal/api/nodes/notify"
        "?topic=issues&repo=alice/widgets")))
    assert result["data"] == {"ok": True, "delivered": 1}
    frame = json.loads(live.sent[0])
    assert frame == {"type": "event", "topic": "issues",
                     "repo": "alice/widgets"}
    assert stale.closed == (1001, "stale")
    assert stale.sent == []


def test_do_accepts_node_sockets_and_enforces_the_cap():
    ns = _load(_base_globals())
    do = _make_do(ns)
    result = asyncio.run(do.fetch(_Request(
        "https://forkmesh.com/api/nodes/events?owner=alice&ts=1&sig=s",
        {"upgrade": "websocket"})))
    assert result["status"] == 101
    assert result["webSocket"] is not None
    (tags, accepted), = do.ctx.tagged
    assert tags == ["node"]
    assert accepted.attachment.id and accepted.attachment.last == NOW
    for _ in range(ns["NODE_EVENT_MAX_SOCKETS"]):
        do.ctx.tagged.append(
            (["node"], _Sock(SimpleNamespace(id="x", last=NOW))))
    full = asyncio.run(do.fetch(_Request(
        "https://forkmesh.com/api/nodes/events?owner=alice&ts=1&sig=s",
        {"upgrade": "websocket"})))
    assert full["status"] == 429


def test_do_ping_refreshes_staleness_clock_and_answers_pong():
    ns = _load(_base_globals())
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW - 60_000))
    asyncio.run(do.webSocketMessage(ws, json.dumps({"type": "ping"})))
    assert json.loads(ws.sent[0]) == {"type": "pong"}
    assert ws.attachment.last == NOW
    assert ws.closed is None
    # Non-text frames and oversized frames close the socket.
    binary = _Sock(SimpleNamespace(id="b", last=NOW))
    asyncio.run(do.webSocketMessage(binary, b"\x00"))
    assert binary.closed == (1003, "text frames only")
    huge = _Sock(SimpleNamespace(id="c", last=NOW))
    asyncio.run(do.webSocketMessage(
        huge, "x" * (ns["NODE_EVENT_MAX_FRAME_BYTES"] + 1)))
    assert huge.closed == (1009, "message too large")


def test_do_rate_limits_chatty_sockets_without_closing_them():
    ns = _load(_base_globals())
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW))
    for _ in range(ns["NODE_EVENT_MSG_MAX_PER_WINDOW"] + 5):
        asyncio.run(do.webSocketMessage(ws, json.dumps({"type": "ping"})))
    assert len(ws.sent) == ns["NODE_EVENT_MSG_MAX_PER_WINDOW"]
    assert ws.closed is None


# --- Deployment contract -----------------------------------------------------

def test_wrangler_registers_the_nodes_do_in_both_environments():
    config = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
    prod = {binding["name"]: binding["class_name"]
            for binding in config["durable_objects"]["bindings"]}
    dev = {binding["name"]: binding["class_name"]
           for binding in config["env"]["dev"]["durable_objects"]["bindings"]}
    assert prod["FORKMESH_NODES"] == "ForkMeshNodes"
    assert dev["FORKMESH_NODES"] == "ForkMeshNodes"
    migrated = [migration for migration in config["migrations"]
                if "ForkMeshNodes" in migration.get("new_sqlite_classes", [])]
    assert len(migrated) == 1
    # ForkMeshHost (the retired byte tunnel) must stay deleted; the event
    # channel is a different class and never re-registers it.
    assert not any("ForkMeshHost" in migration.get("new_sqlite_classes", [])
                   for migration in config["migrations"][9:])
