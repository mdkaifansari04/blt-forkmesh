#!/usr/bin/env python3
"""ForkMeshNodes — the per-owner node event push channel.

notify_repo_host fans a payload-free {"type":"event","topic"} frame out to the
owner's connected desktop/headless nodes the moment a web submission lands;
each node answers with its one signed GET /api/sync. Relay -> node stays
payload free: it must never carry repository bytes or inbox payloads, and a
notify failure must never fail the write that triggered it.

Node -> relay carries keepalives plus the key-signed writes named in
NODE_FRAME_ACCEPTS — today the batched agent-status report a desktop would
otherwise POST while already holding this socket (adhoc #1618). Those are
transport only: the frame carries the same proof the HTTPS route demands and
goes to the same handler, so holding the socket grants nothing.
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
    "notify_repo_mirrors",
    "node_events_handler",
    "_node_events_do_name",
    "ForkMeshNodes",
    "safe_segment",
    "clean_string",
    "_ws_attachment",
    "_ws_attr",
    "durable_object_traffic_note",
    "durable_object_traffic_flush",
}
CONSTANTS = {
    "DURABLE_OBJECT_BINDING_RE",
    "DURABLE_OBJECT_TRAFFIC_FLUSH_MS",
    "DURABLE_OBJECT_TRAFFIC_FLUSH_BYTES",
    "DURABLE_OBJECT_TRAFFIC_MAX",
    "NODE_EVENT_MAX_SOCKETS",
    "NODE_SOCKET_STALE_MS",
    "NODE_EVENT_MSG_WINDOW_MS",
    "NODE_EVENT_MSG_MAX_PER_WINDOW",
    "NODE_EVENT_MAX_FRAME_BYTES",
    "NODE_FRAME_ACCEPTS",
    "MAX_NODE_NAME",
    # The gate the DO's synthetic request has to satisfy for its proof to be
    # checked at all; pulled from entry.py so the two cannot drift apart.
    "ORG_TASK_AGENT_STATUS_BATCH_RE",
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


def _base_globals(traffic_writes=None):
    socket_ids = iter(range(1, 100))

    async def d1_run(_env, sql, *args):
        if traffic_writes is not None:
            traffic_writes.append((sql, args))
        return None

    return {
        "d1_run": d1_run,
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


def test_mirror_notifications_reach_machine_and_linked_owner_channels():
    notified = []

    async def context(_env, owner, repo):
        assert (owner, repo) == ("source", "widgets")
        return {"nodes": {"mirror2", "mirror3"}}

    async def d1_all(_env, sql, *args):
        assert "LEFT JOIN users" in sql
        assert set(args) == {"mirror2", "mirror3"}
        return [
            {"node_name": "mirror2", "owner": "jett"},
            {"node_name": "mirror3", "owner": ""},
        ]

    async def notify(_env, owner, repo, topic):
        notified.append((owner, repo, topic))

    ns = _load({
        **_base_globals(),
        "_https_mirror_public_context": context,
        "d1_all": d1_all,
        "notify_repo_host": notify,
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z][a-z0-9-]{0,62}", str(value or ""))),
    })
    # Replace the loaded notifier so notify_repo_mirrors observes the spy.
    ns["notify_repo_mirrors"].__globals__["notify_repo_host"] = notify
    asyncio.run(ns["notify_repo_mirrors"](
        object(), "source", "widgets", "issues"))
    assert notified == [
        ("jett", "widgets", "issues"),
        ("mirror2", "widgets", "issues"),
        ("mirror3", "widgets", "issues"),
    ]


def test_write_paths_stay_wired_to_the_event_push():
    # The push is only instant if the submission handlers keep calling it.
    entry_only = ENTRY.read_text(encoding="utf-8")
    calls = entry_only.count("notify_repo_host(env, owner, repo,")
    assert calls >= 6, "web submission paths no longer notify the owner node"
    for topic in ("issues", "pulls", "discussions", "agents", "about"):
        assert 'notify_repo_host(env, owner, repo, "%s")' % topic in entry_only
    # Freshly pushed source commits have no per-owner submission handler; they
    # reach the fleet through the mirror fan-out on the catalog publish path
    # (a public record whose advertised head moved), which wakes every
    # integrity-approved mirror's sync.
    assert 'env, owner, record["name"], "commits")' in entry_only


def test_channel_carries_no_data_and_grants_no_authority():
    # Nothing on this channel may read D1, decrypt a row, or move an inbound
    # request body of its own. What the relay pushes down is payload free, and
    # the one write a node may push up is authorized by the proof inside the
    # frame — never by the fact that it arrived on a connected socket.
    for name in ("ForkMeshNodes", "notify_repo_host", "node_events_handler"):
        source = _source_of(name)
        assert "d1_" not in source
        assert "decrypt" not in source
        assert "request.bytes" not in source
        assert "request.text" not in source
        assert "request.json" not in source
    nodes = _source_of("ForkMeshNodes")
    assert "'type': 'event'" in nodes
    # The write path re-signs nothing and invents no identity: it forwards the
    # node's own node/ts/sig triple to the shared task handler, which runs the
    # batch proof check and the per-task ownership gates exactly as on HTTPS.
    for token in ("frame.get('node')", "frame.get('ts')", "frame.get('sig')"):
        assert token in nodes
    assert "organization_tasks_handler" in nodes
    assert "'/api/tasks/agent-status'" in nodes


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


# --- the batched agent-status write carried on the socket --------------------

def _tasks_globals(calls, status=200, payload=None, raises=None):
    """Capture what the DO hands the shared task handler."""

    class _Response:
        def __init__(self):
            self.status = status

        async def json(self):
            return payload if payload is not None else {"ok": True}

    async def organization_tasks_handler(_env, request, path):
        calls.append((request, path))
        if raises is not None:
            raise raises
        return _Response()

    class _JsRequest:
        @staticmethod
        def new(url, init):
            return SimpleNamespace(
                url=url,
                method=getattr(init, "method", None),
                body=getattr(init, "body", None),
                headers=getattr(init, "headers", None),
            )

    return {
        **_base_globals(),
        "organization_tasks_handler": organization_tasks_handler,
        "JsRequest": _JsRequest,
    }


def _status_frame(**overrides):
    frame = {
        "type": "agent-status",
        "node": "Alice",
        "ts": "1786072265687",
        "sig": "j4wIWDJLjNhC",
        "statuses": [
            {"task": "a" * 32, "status": "running"},
            {"task": "b" * 32, "status": "success"},
        ],
    }
    frame.update(overrides)
    return json.dumps(frame)


def test_signed_status_frame_goes_to_the_same_handler_as_the_https_write():
    # The whole point of moving this write onto the socket: no request is
    # made, and no authority is invented. The node's own proof rides through
    # to the shared task handler, which re-checks it exactly as on HTTPS.
    calls = []
    ns = _load(_tasks_globals(calls, payload={
        "ok": True,
        "results": [{"task": "a" * 32, "ok": True, "error": ""},
                    {"task": "b" * 32, "ok": False, "error": "task_not_found"}],
    }))
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW))
    asyncio.run(do.webSocketMessage(ws, _status_frame()))

    (request, path), = calls
    assert path == "/api/tasks/agent-status"
    assert request.method == "POST"
    url = urlparse(request.url)
    # The batch-proof gate in _org_task_signed_session matches on the path, so
    # a synthetic request the real regex misses would fall through to "no
    # session" and silently write nothing.
    assert ns["ORG_TASK_AGENT_STATUS_BATCH_RE"].match(url.path)
    # Lower-cased like the HTTPS gate does before verifying the signature.
    assert parse_qs(url.query) == {
        "node": ["alice"], "ts": ["1786072265687"], "sig": ["j4wIWDJLjNhC"]}
    assert json.loads(request.body) == {"statuses": [
        {"task": "a" * 32, "status": "running"},
        {"task": "b" * 32, "status": "success"},
    ]}
    # Per-entry outcomes go back so one deleted row cannot strand the rest.
    assert json.loads(ws.sent[0]) == {
        "type": "agent-status-result",
        "ok": True,
        "results": [{"task": "a" * 32, "ok": True, "error": ""},
                    {"task": "b" * 32, "ok": False, "error": "task_not_found"}],
    }


def test_status_frame_without_a_complete_proof_is_dropped():
    # Being connected is not a credential: a frame missing any part of the
    # triple never reaches the task handler at all.
    for missing in ("node", "ts", "sig"):
        calls = []
        ns = _load(_tasks_globals(calls))
        do = _make_do(ns)
        ws = _Sock(SimpleNamespace(id="a", last=NOW))
        asyncio.run(do.webSocketMessage(ws, _status_frame(**{missing: ""})))
        assert calls == []
        assert ws.sent == []
    # So is a frame whose statuses are not a list.
    calls = []
    ns = _load(_tasks_globals(calls))
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW))
    asyncio.run(do.webSocketMessage(ws, _status_frame(statuses="all")))
    assert calls == []
    assert ws.sent == []


def test_refused_status_write_still_answers_so_the_node_can_retry():
    # The node marks these states published optimistically. An unanswered
    # frame would leave runs showing a stale state on the board until
    # something else changed them, so a refusal must come back as one.
    for globals_ in (
        _tasks_globals([], status=401, payload={"error": "invalid_session"}),
        _tasks_globals([], raises=RuntimeError("handler exploded")),
    ):
        ns = _load(globals_)
        do = _make_do(ns)
        ws = _Sock(SimpleNamespace(id="a", last=NOW))
        asyncio.run(do.webSocketMessage(ws, _status_frame()))
        assert json.loads(ws.sent[0]) == {
            "type": "agent-status-result", "ok": False, "results": []}


def test_rate_limited_status_frames_do_no_work_but_are_still_refused():
    # A node that floods writes gets the same budget as one that floods pings.
    # Over budget the write never reaches the task handler — but it is still
    # answered, because silence would leave the node believing it landed.
    calls = []
    budget = _load(_base_globals())["NODE_EVENT_MSG_MAX_PER_WINDOW"]
    ns = _load(_tasks_globals(calls))
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW))
    for _ in range(budget + 5):
        asyncio.run(do.webSocketMessage(ws, _status_frame()))
    assert len(calls) == budget
    assert len(ws.sent) == budget + 5
    assert json.loads(ws.sent[-1]) == {
        "type": "agent-status-result", "ok": False, "results": []}
    assert ws.closed is None


def test_rate_limited_keepalives_are_still_just_dropped():
    # The refusal is for writes only: a node must never see a verdict for a
    # frame it did not send, or it would retire a batch that is still pending.
    ns = _load(_base_globals())
    do = _make_do(ns)
    ws = _Sock(SimpleNamespace(id="a", last=NOW))
    for _ in range(ns["NODE_EVENT_MSG_MAX_PER_WINDOW"] + 5):
        asyncio.run(do.webSocketMessage(ws, json.dumps({"type": "ping"})))
    assert {json.loads(frame)["type"] for frame in ws.sent} == {"pong"}


def test_frame_ceiling_fits_a_full_fleet_report():
    # A desktop publishes one entry per live session, up to the task API's
    # MAX_AGENT_STATUS_BATCH of 200, and each entry carries the run's
    # provenance as well as its state. The socket's frame cap has to hold that
    # much or a fleet-sized node would silently fall back to HTTPS for the one
    # burst this whole change exists to keep off HTTPS.
    ns = _load(_base_globals())
    module = (ROOT / "src" / "world_office_tasks.py").read_text(
        encoding="utf-8")
    assert "MAX_AGENT_STATUS_BATCH = 200" in module
    full = _status_frame(statuses=[
        {
            "task": "%032x" % index,
            "status": "running",
            "agent": {
                "provider": "claude-code", "startedBy": "forkbot-longish",
                "model": "claude-opus-5-20260101", "mode": "agent",
                "strength": "high", "sessionId": "%d" % index,
            },
        }
        for index in range(200)
    ])
    assert len(full.encode("utf-8")) < ns["NODE_EVENT_MAX_FRAME_BYTES"]
    # ...and the cap stays inside the two-byte extended frame length the Qt
    # client writes (NodeEventSocket::sendTextFrame).
    assert ns["NODE_EVENT_MAX_FRAME_BYTES"] < 65_536
    assert "constexpr int kMaxOutboundPayload = 60 * 1024;" in (
        ROOT.parent / "desktop" / "src" / "NodeEventSocket.cpp"
    ).read_text(encoding="utf-8")


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
