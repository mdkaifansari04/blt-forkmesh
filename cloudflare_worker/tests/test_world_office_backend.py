#!/usr/bin/env python3
"""Authorization and runtime contracts for spatial Office meeting rooms."""

import ast
import asyncio
import base64
import hmac
import re
import tomllib
from types import SimpleNamespace
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WRANGLER = ROOT / "wrangler.toml"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _top_level_node(name):
    for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            if node.name == name:
                return node
    raise AssertionError("%s not found in entry.py" % name)


def _ticket_helpers():
    names = {
        "_require_data_secret",
        "_office_entry_ticket",
        "_office_entry_ticket_claims",
        "_office_entry_rate_step",
        "_office_meeting_ticket",
        "_office_meeting_ticket_claims",
    }
    nodes = [
        node for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in nodes} == names
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
    clock = [1_800_000_000_000]

    class _Date:
        @staticmethod
        def now():
            return clock[0]

    namespace = {
        "OFFICE_ENTRY_TICKET_TTL_MS": 5 * 60 * 1000,
        "OFFICE_ENTRY_RATE_WINDOW_MS": 60 * 1000,
        "OFFICE_ENTRY_RATE_MAX_PER_WINDOW": 8,
        "OFFICE_ENTRY_RATE_BUCKETS_MAX": 2048,
        "OFFICE_MEETING_TICKET_TTL_MS": 60 * 1000,
        "Date": _Date,
        "_INSECURE_DATA_KEYS": frozenset({"", "forkmesh-dev-data-key"}),
        "base64": base64,
        "hmac": hmac,
        "new_world_peer_id": lambda: "0123456789abcdef",
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace, clock


class _Env:
    DATA_KEY = "private-production-data-key"


def test_office_entry_ticket_is_short_lived_tamper_evident_and_code_free():
    namespace, clock = _ticket_helpers()
    token = namespace["_office_entry_ticket"](_Env())
    claims = namespace["_office_entry_ticket_claims"](_Env(), token)

    assert claims == {
        "scope": "world-general",
        "expires": clock[0] + 5 * 60 * 1000,
        "nonce": "0123456789abcdef",
    }
    assert "OFFICE_ENTRY_CODE" not in ast.unparse(
        _top_level_node("_office_entry_ticket"))
    replacement = "0" if token[-1] != "0" else "1"
    assert namespace["_office_entry_ticket_claims"](
        _Env(), token[:-1] + replacement) is None
    clock[0] += 5 * 60 * 1000
    assert namespace["_office_entry_ticket_claims"](_Env(), token) is None


def test_office_entry_rate_limit_is_windowed_and_memory_bounded():
    namespace, _clock = _ticket_helpers()
    step = namespace["_office_entry_rate_step"]
    buckets = {}
    key = "a" * 64

    for attempt in range(8):
        assert step(buckets, key, 1000 + attempt) == (True, 0)
    admitted, retry_ms = step(buckets, key, 1009)
    assert admitted is False
    assert 1000 <= retry_ms <= 60 * 1000
    assert len(buckets) == 1

    assert step(buckets, key, 61_001) == (True, 0)
    full = {
        ("%064x" % index): (61_001, 1)
        for index in range(2048)
    }
    assert step(full, "f" * 64, 61_002)[0] is False
    assert len(full) == 2048


def _entry_handler():
    node = _top_level_node("office_general_entry_handler")
    module = ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[]))
    state = {"occupied": False, "participantCount": 0}
    request_code = [""]

    class _FakeResponse(dict):
        def __init__(self, data, status):
            super().__init__(status=status, data=data)
            self.status = status

    async def blind_index(_env, _value):
        return "a" * 64

    async def room_state(
            _env, _rate_key, code_approved=None, code_digest="",
            request=None):
        assert isinstance(code_approved, bool)
        assert code_digest == (
            "digest:" + str(request_code[0])
            if request_code[0] else "")
        if state["occupied"] and not code_approved:
            return None, _FakeResponse(
                {"error": "invalid_entry_code"}, 403)
        return {
            **state,
            "codeConfigured": False,
            "canSetCode": False,
        }, None

    async def bounded_json_request(request, max_bytes):
        assert max_bytes == 128
        return request.data

    def json_response(data, status=200, **_kwargs):
        return _FakeResponse(data, status)

    namespace = {
        "OFFICE_ENTRY_REQUEST_MAX_BYTES": 128,
        "RequestBodyTooLarge": type("RequestBodyTooLarge", (BaseException,), {}),
        "_office_entry_ticket": lambda _env: "entry-proof",
        "_office_entry_ticket_claims": lambda _env, _token: {
            "expires": 1_800_000_060_000,
        },
        "_office_entry_code_digest": (
            lambda _env, code: "digest:" + str(code)),
        "_office_general_room_state": room_state,
        "_request_same_origin": lambda request: request.same_origin,
        "_transient_client_address": lambda _request: "192.0.2.1",
        "blind_index": blind_index,
        "bounded_json_request": bounded_json_request,
        "hmac": hmac,
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    original = namespace["office_general_entry_handler"]

    async def handler(env, request):
        request_code[0] = request.data.get("code", "")
        return await original(env, request)

    return handler, state


class _EntryRequest:
    method = "POST"
    same_origin = True

    def __init__(self, data):
        self.data = data


def test_office_entry_is_open_when_empty_and_four_digit_secret_when_occupied():
    handler, state = _entry_handler()

    empty = asyncio.run(handler(_Env(), _EntryRequest({})))
    assert empty["status"] == 200
    assert empty["data"]["entryTicket"] == "entry-proof"
    assert empty["data"]["occupied"] is False

    state.update({"occupied": True, "participantCount": 1})
    unavailable = asyncio.run(handler(_Env(), _EntryRequest({})))
    assert unavailable == {
        "status": 503,
        "data": {"error": "office_unavailable"},
    }

    class OccupiedEnv(_Env):
        OFFICE_ENTRY_CODE = "2468"

    wrong = asyncio.run(handler(OccupiedEnv(), _EntryRequest({"code": "0000"})))
    assert wrong == {
        "status": 403,
        "data": {"error": "invalid_entry_code"},
    }
    entered = asyncio.run(
        handler(OccupiedEnv(), _EntryRequest({"code": "2468"})))
    assert entered["status"] == 200
    assert entered["data"]["occupied"] is True
    assert "participantCount" not in entered["data"]
    assert "2468" not in repr(entered)


def test_office_entry_rejects_cross_origin_posts_before_admission():
    handler, _state = _entry_handler()
    request = _EntryRequest({})
    request.same_origin = False

    response = asyncio.run(handler(_Env(), request))

    assert response == {
        "status": 403,
        "data": {"error": "origin_not_allowed"},
    }


def _office_code_protocol():
    names = {
        "_ws_attachment",
        "_ws_attr",
        "_office_entry_code_digest",
        "_office_socket_code_state",
        "_office_entry_digest_approved",
        "_office_live_account",
        "durable_object_traffic_note",
        "durable_object_traffic_flush",
    }
    parsed = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    nodes = [
        node for node in parsed.body
        if (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and node.name in names
        )
        or (
            isinstance(node, ast.ClassDef)
            and node.name == "ForkMeshOfficeRoom"
        )
    ]
    assert {
        node.name for node in nodes
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
    } == names
    module = ast.fix_missing_locations(
        ast.Module(body=nodes, type_ignores=[]))

    class _Date:
        @staticmethod
        def now():
            return 1_800_000_000_000

    class _World:
        OFFICE_PUBLIC_FIELDS = ("id",)
        OFFICE_MAX_CONNECTIONS = 50

        @staticmethod
        def public_office_presence(state):
            return {"id": str((state or {}).get("id") or "")}

        @staticmethod
        def office_presence_is_stale(_last, _now):
            return False

        @staticmethod
        def advance_office_broadcast_window(_start, count, now):
            return True, now, count + 1

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def d1_run(*_args):
        return None

    namespace = {
        "DurableObject": object,
        "Date": _Date,
        "d1_run": d1_run,
        "DURABLE_OBJECT_BINDING_RE": re.compile(r"[A-Z][A-Z0-9_]{0,63}"),
        "DURABLE_OBJECT_TRAFFIC_FLUSH_MS": 60_000,
        "DURABLE_OBJECT_TRAFFIC_FLUSH_BYTES": 262_144,
        "DURABLE_OBJECT_TRAFFIC_MAX": 9_007_199_254_740_991,
        "OFFICE_INTERNAL_RE": re.compile(
            r"^/api/world/office/(world-general|[0-9a-f]{32})/"
            r"v([1-9][0-9]*)/(ws|revoke|status|entry|code)$"),
        "_require_data_secret": lambda env: env.DATA_KEY,
        "base64": base64,
        "hmac": hmac,
        "json": __import__("json"),
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "re": re,
        "to_js": lambda value: value,
        "urlparse": urlparse,
        "world_protocol": _World,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _OfficeSocket:
    def __init__(self, account_bi, digest="", departed=False):
        self.attachment = SimpleNamespace(
            id=account_bi[:8],
            account_bi=account_bi,
            scope="world-general",
            version=1,
            trusted_name="User",
            trusted_status="Registered",
            last=1_800_000_000_000,
            rl_start=1_800_000_000_000,
            rl_count=0,
            departed=departed,
            entry_code_digest=digest,
        )
        self.closed = False

    def deserializeAttachment(self):
        return self.attachment

    def serializeAttachment(self, value):
        self.attachment = SimpleNamespace(**dict(value))

    def close(self, _code, _reason):
        self.closed = True

    def send(self, _message):
        return None


class _OfficeContext:
    def __init__(self, sockets):
        self.sockets = sockets

    def getWebSockets(self, _tag):
        return list(self.sockets)


class _OfficeInternalRequest:
    method = "POST"

    def __init__(self, account_bi, digest):
        self.url = (
            "https://forkmesh.internal/api/world/office/"
            "world-general/v1/code"
        )
        self.headers = {
            "x-forkmesh-office-account-bi": account_bi,
            "x-forkmesh-office-code-digest": digest,
        }


def test_office_session_code_is_keyed_compared_and_cleared_when_empty():
    namespace = _office_code_protocol()
    digest = namespace["_office_entry_code_digest"](_Env(), "2468")
    wrong = namespace["_office_entry_code_digest"](_Env(), "0000")
    assert re.fullmatch(r"[0-9a-f]{64}", digest)
    assert digest != wrong
    assert "2468" not in digest
    assert namespace["_office_entry_code_digest"](_Env(), "12x4") == ""
    assert namespace["_office_entry_digest_approved"](
        True, digest, digest, False) is True
    assert namespace["_office_entry_digest_approved"](
        True, digest, wrong, True) is False
    assert namespace["_office_entry_digest_approved"](
        False, "", "", True) is True

    account = "a" * 64
    first = _OfficeSocket(account)
    second = _OfficeSocket("b" * 64)
    room = namespace["ForkMeshOfficeRoom"]()
    room.ctx = _OfficeContext([first, second])
    assert room._set_session_code_digest([first, second], digest) is True
    assert namespace["_office_socket_code_state"](
        [first, second]) == (True, digest)

    room._depart(first, 1000, "")
    assert second.attachment.entry_code_digest == digest
    room._depart(second, 1000, "")
    assert first.attachment.entry_code_digest == ""
    assert second.attachment.entry_code_digest == ""
    assert namespace["_office_socket_code_state"]([]) == (False, "")


def test_office_room_accepts_code_change_only_from_matching_live_account():
    namespace = _office_code_protocol()
    account = "a" * 64
    digest = "d" * 64
    socket = _OfficeSocket(account)
    room = namespace["ForkMeshOfficeRoom"]()
    room.ctx = _OfficeContext([socket])

    denied = asyncio.run(room.fetch(
        _OfficeInternalRequest("b" * 64, digest)))
    assert denied["status"] == 403
    assert socket.attachment.entry_code_digest == ""

    accepted = asyncio.run(room.fetch(
        _OfficeInternalRequest(account, digest)))
    assert accepted == {
        "status": 200,
        "data": {
            "ok": True,
            "occupied": True,
            "codeConfigured": True,
            "canSetCode": True,
        },
    }
    assert socket.attachment.entry_code_digest == digest

    empty = namespace["ForkMeshOfficeRoom"]()
    empty.ctx = _OfficeContext([])
    prelock = asyncio.run(empty.fetch(
        _OfficeInternalRequest(account, "e" * 64)))
    assert prelock["status"] == 403
    assert prelock["data"]["occupied"] is False


def _code_handler():
    node = _top_level_node("office_general_code_handler")
    module = ast.fix_missing_locations(ast.Module(
        body=[node], type_ignores=[]))
    state = {
        "account_bi": "a" * 64,
        "account": {"name": "alice", "status": "active", "kind": "user"},
        "live": True,
        "forwarded": [],
    }

    async def account_session(_env, _request):
        return state["account_bi"], state["account"]

    async def bounded_json(request, max_bytes):
        assert max_bytes == 128
        return request.data

    async def set_code(_env, account_bi, digest, request=None):
        state["forwarded"].append((account_bi, digest))
        if not state["live"]:
            return None, {
                "status": 403,
                "data": {"error": "office_occupant_required"},
            }
        return {
            "occupied": True,
            "codeConfigured": True,
            "canSetCode": True,
        }, None

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    namespace = {
        "OFFICE_ENTRY_REQUEST_MAX_BYTES": 128,
        "RequestBodyTooLarge": type("RequestBodyTooLarge", (BaseException,), {}),
        "_account_kind": lambda record: record.get("kind", ""),
        "_account_session_record": account_session,
        "_office_entry_code_digest": lambda _env, _code: "d" * 64,
        "_office_general_set_code": set_code,
        "_request_same_origin": lambda request: request.same_origin,
        "bounded_json_request": bounded_json,
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["office_general_code_handler"], state


class _CodeRequest:
    method = "POST"
    same_origin = True

    def __init__(self, data):
        self.data = data
        # An expired/stale entry proof is intentionally not decisive once the
        # authenticated account still has a live Office socket.
        self.headers = {"x-forkmesh-office-entry": "expired-proof"}


def test_office_code_handler_requires_same_origin_registered_live_occupant():
    handler, state = _code_handler()
    request = _CodeRequest({"code": "2468"})
    request.same_origin = False
    cross_origin = asyncio.run(handler(_Env(), request))
    assert cross_origin["status"] == 403
    assert state["forwarded"] == []

    request.same_origin = True
    state["account_bi"] = ""
    state["account"] = None
    unauthenticated = asyncio.run(handler(_Env(), request))
    assert unauthenticated["status"] == 401

    state["account_bi"] = "a" * 64
    state["account"] = {
        "name": "rootadmin",
        "status": "active",
        "kind": "user",
        "isAdmin": True,
    }
    state["live"] = False
    not_present = asyncio.run(handler(_Env(), request))
    assert not_present["status"] == 403

    state["live"] = True
    accepted = asyncio.run(handler(_Env(), request))
    assert accepted["status"] == 200
    assert accepted["data"] == {
        "ok": True,
        "occupied": True,
        "codeConfigured": True,
        "canSetCode": True,
    }
    assert state["forwarded"][-1] == ("a" * 64, "d" * 64)
    assert "2468" not in repr(state["forwarded"])


def test_office_status_exposes_boolean_state_without_secret_or_count():
    node = _top_level_node("office_general_status_handler")
    module = ast.fix_missing_locations(ast.Module(
        body=[node], type_ignores=[]))

    async def account_session(_env, _request):
        return "a" * 64, {
            "name": "alice",
            "status": "active",
            "kind": "user",
        }

    async def room_state(_env, occupant_bi="", request=None):
        assert occupant_bi == "a" * 64
        return {
            "occupied": True,
            "codeConfigured": False,
            "canSetCode": True,
        }, None

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    namespace = {
        "_account_kind": lambda record: record.get("kind", ""),
        "_account_session_record": account_session,
        "_office_general_room_state": room_state,
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)

    class _StatusRequest:
        method = "GET"

    class _FallbackEnv(_Env):
        OFFICE_ENTRY_CODE = "2468"

    response = asyncio.run(namespace["office_general_status_handler"](
        _FallbackEnv(), _StatusRequest()))
    assert response == {
        "status": 200,
        "data": {
            "ok": True,
            "occupied": True,
            "codeConfigured": True,
            "requiresCode": True,
            "canSetCode": True,
        },
    }
    assert all(isinstance(value, bool) for value in response["data"].values())
    assert "2468" not in repr(response)
    assert "participant" not in repr(response).lower()


def _access_handler():
    node = _top_level_node("office_general_access_handler")
    module = ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[]))

    class _Headers(dict):
        def get(self, name, default=None):
            return super().get(str(name).lower(), default)

    class _Request:
        method = "GET"

        def __init__(self, entry=""):
            self.headers = _Headers({
                "x-forkmesh-office-entry": entry,
            })

    async def account_session(_env, _request):
        return "", None

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    namespace = {
        "_account_session_record": account_session,
        "_office_entry_ticket_claims": (
            lambda _env, token: (
                {"scope": "world-general"} if token == "valid-entry" else None
            )
        ),
        "_office_meeting_ticket": (
            lambda _env, _scope, _version, _account, _name: "meeting-proof"
        ),
        "_office_meeting_ticket_claims": (
            lambda _env, _token: {"expires": 1_800_000_060_000}
        ),
        "json_response": json_response,
        "method_name": lambda request: request.method,
        "quote": lambda value, safe="": value,
        "world_protocol": type("_World", (), {
            "clean_display_name": staticmethod(
                lambda value, fallback: value or fallback),
        }),
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["office_general_access_handler"], _Request


def test_general_access_requires_valid_entry_header_before_meeting_ticket():
    handler, request_type = _access_handler()

    for entry in ("", "tampered"):
        denied = asyncio.run(handler(_Env(), request_type(entry)))
        assert denied == {
            "status": 401,
            "data": {"error": "office_entry_required"},
        }

    admitted = asyncio.run(handler(_Env(), request_type("valid-entry")))
    assert admitted["status"] == 200
    assert admitted["data"]["meetingWebSocketUrl"].endswith("meeting-proof")
    assert "valid-entry" not in repr(admitted)


def test_office_ticket_is_short_lived_and_tamper_evident():
    namespace, clock = _ticket_helpers()
    token = namespace["_office_meeting_ticket"](
        _Env(), "world-general", 1, "", "Guest 1234")
    claims = namespace["_office_meeting_ticket_claims"](_Env(), token)

    assert claims == {
        "scope": "world-general",
        "version": 1,
        "account_bi": "",
        "name": "Guest 1234",
        "expires": clock[0] + 60 * 1000,
        "nonce": "0123456789abcdef",
    }
    replacement = "0" if token[-1] != "0" else "1"
    assert namespace["_office_meeting_ticket_claims"](
        _Env(), token[:-1] + replacement) is None
    clock[0] += 60 * 1000
    assert namespace["_office_meeting_ticket_claims"](_Env(), token) is None


def test_office_ticket_rejects_unbounded_or_malformed_claims():
    namespace, _clock = _ticket_helpers()
    for scope, version, account_bi, name in (
        ("private-name", 1, "", "Guest"),
        ("world-general", 0, "", "Guest"),
        ("a" * 32, 1, "not-a-blind-index", "Alice"),
        ("world-general", 1, "", "x" * 80),
    ):
        try:
            namespace["_office_meeting_ticket"](
                _Env(), scope, version, account_bi, name)
        except ValueError:
            continue
        raise AssertionError("invalid Office ticket input must fail closed")


def test_office_routes_authorize_before_durable_object_lookup():
    channel_source = ast.unparse(
        _top_level_node("_office_channel_socket_handler"))
    forward_at = channel_source.index("_forward_office_socket")
    assert channel_source.index("FROM chat_channels") < forward_at
    assert channel_source.index("FROM users") < forward_at
    assert channel_source.index("chat_channel_members") < forward_at
    assert "_office_meeting_ticket_claims" in channel_source
    assert "world_websocket_origin_allowed" in channel_source
    forward_source = ast.unparse(_top_level_node("_forward_office_socket"))
    assert "FORKMESH_OFFICE_ROOM.idFromName" in forward_source


def test_office_general_access_and_socket_routes_are_registered():
    route_source = ast.unparse(_top_level_node("Default"))
    for route in (
        "/api/world/office/general/status",
        "/api/world/office/general/entry",
        "/api/world/office/general/code",
        "/api/world/office/general/access",
        "/api/world/office/general/ws",
    ):
        assert route in route_source
    assert "OFFICE_CHANNEL_WS_RE" in route_source
    assert "/api/world/office/channels/" in ENTRY_TEXT
    assert "office_general_access_handler" in route_source
    assert "office_general_code_handler" in route_source
    assert "_office_general_socket_handler" in route_source
    assert "_office_channel_socket_handler" in route_source

    access_source = ast.unparse(
        _top_level_node("office_general_access_handler"))
    assert "x-forkmesh-office-entry" in access_source
    assert access_source.index("_office_entry_ticket_claims") < (
        access_source.index("_office_meeting_ticket"))
    assert "office_entry_required" in access_source
    assert "meetingWebSocketUrl" in access_source
    assert "expiresAt" in access_source
    assert "no-store, max-age=0, must-revalidate" in access_source

    entry_source = ast.unparse(
        _top_level_node("office_general_entry_handler"))
    assert "bounded_json_request" in entry_source
    assert "OFFICE_ENTRY_CODE" in entry_source
    assert "hmac.compare_digest" in entry_source
    assert "_request_same_origin" in entry_source
    assert "_office_general_room_state" in entry_source
    assert "entryTicket" in entry_source
    assert "abuse" not in entry_source.lower()
    assert "quarantine" not in entry_source.lower()

    status_source = ast.unparse(
        _top_level_node("office_general_status_handler"))
    assert "_office_general_room_state" in status_source
    assert "participantCount" not in status_source
    assert "occupied" in status_source
    assert "participantCount" not in status_source

    code_source = ast.unparse(
        _top_level_node("office_general_code_handler"))
    assert "_request_same_origin" in code_source
    assert "_account_session_record" in code_source
    assert "_office_entry_code_digest" in code_source
    assert "_office_general_set_code" in code_source
    assert "_is_admin" not in code_source
    assert "_audit_sensitive_action" not in code_source
    assert "OFFICE_ENTRY_CODE" not in code_source


def test_office_internal_upgrade_strips_browser_credentials_and_query():
    source = ast.unparse(
        _top_level_node("office_durable_object_request")).lower()
    assert "x-forkmesh-office-claim" in source
    assert "source_url.path" in source
    assert "source_url.query" not in source
    for forbidden in (
        "authorization",
        "cookie",
        "user-agent",
        "cf-connecting-ip",
        "x-forwarded-for",
        "referer",
    ):
        assert forbidden not in source


def test_wrangler_registers_office_room_in_prod_and_dev():
    parsed = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
    prod = {
        item["name"]: item["class_name"]
        for item in parsed["durable_objects"]["bindings"]
    }
    dev = {
        item["name"]: item["class_name"]
        for item in parsed["env"]["dev"]["durable_objects"]["bindings"]
    }
    assert prod["FORKMESH_OFFICE_ROOM"] == "ForkMeshOfficeRoom"
    assert dev["FORKMESH_OFFICE_ROOM"] == "ForkMeshOfficeRoom"
    assert parsed["migrations"][-1] == {
        "tag": "v11",
        "new_sqlite_classes": ["ForkMeshOfficeRoom"],
    }


def test_office_room_uses_hibernation_attachments_without_storage():
    source = ast.unparse(_top_level_node("ForkMeshOfficeRoom"))
    assert "acceptWebSocket" in source
    assert "getWebSockets" in source
    assert "serializeAttachment" in source
    assert "OFFICE_MESSAGE_MAX_BYTES" in source
    assert "allocate_office_seat" in source
    assert "seat-denied" in source
    assert "Seat just taken." in source
    assert "webSocketClose" in source
    assert "webSocketError" in source
    assert "x-forkmesh-office-rate-key" in source
    assert "x-forkmesh-office-code-approved" in source
    assert "x-forkmesh-office-code-digest" in source
    assert "x-forkmesh-office-account-bi" in source
    assert "_office_entry_rate_step" in source
    assert "peers = self._live_sockets(cleanup=True)" in source
    assert source.index("peers = self._live_sockets(cleanup=True)") < (
        source.index("entry_code_required"))
    assert "participantCount" not in source
    for forbidden in (
        "ctx.storage.put",
        "ctx.storage.get",
        "chat_history",
        "roomKey",
        "passphrase",
        "attachment",
    ):
        assert forbidden not in source


def _room_state_harness(fetch_results, memo, now=1_800_000_000_000):
    """Load the real _office_general_room_state over a scripted Office DO.

    fetch_results: one entry per room fetch attempt — an Exception instance
    to raise, or a (status, payload) tuple to answer with.
    """
    names = {
        "_office_general_room_state",
        "_office_status_memo_fallback",
        "_office_status_memo_store",
    }
    nodes = [
        node for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in nodes} == names
    module = ast.fix_missing_locations(
        ast.Module(body=nodes, type_ignores=[]))
    attempts = []
    aborts = []

    class _Response:
        def __init__(self, status, payload):
            self.status = status
            self.payload = payload

    class _RoomObject:
        async def fetch(self, _request):
            attempts.append("fetch")
            result = fetch_results.pop(0)
            if isinstance(result, Exception):
                raise result
            return _Response(*result)

    class _RoomBinding:
        @staticmethod
        def idFromName(name):
            assert name == "office:world-general:v1"
            return "room-id"

        @staticmethod
        def get(_room_id):
            return _RoomObject()

    class _Date:
        @staticmethod
        def now():
            return now

    async def response_json(response):
        return response.payload

    async def log_abort(_env, _request, path, error):
        aborts.append((path, str(error)))

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    namespace = {
        "Date": _Date,
        "EXPECTED_DEGRADED_HEADERS": {"x-forkmesh-expected-degraded": "1"},
        "JsRequest": SimpleNamespace(new=lambda _url, _options: "request"),
        "OFFICE_STATUS_MEMO_MAX": 256,
        "OFFICE_STATUS_MEMO_STALE_MS": 5 * 60 * 1000,
        "_OFFICE_STATUS_MEMO": memo,
        "_response_json": response_json,
        "json_response": json_response,
        "log_durable_object_abort": log_abort,
        "re": re,
        "to_js": lambda value: value,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    env = SimpleNamespace(FORKMESH_OFFICE_ROOM=_RoomBinding())
    return namespace, env, attempts, aborts


def test_office_status_retries_the_aborted_room_then_serves_stale_state():
    # The platform can abort the Office DO mid-request (free-tier duration
    # cap / co-located isolate overload). A read-only status poll retries
    # once, records the real abort reason, and falls back to the last state
    # the room actually confirmed — the 2026-07-25 burst of bare
    # "response status 503" rows carried no diagnostic value at all.
    ok_payload = (200, {
        "ok": True, "occupied": True,
        "codeConfigured": True, "canSetCode": False,
    })
    memo = {}
    ns, env, attempts, aborts = _room_state_harness([ok_payload], memo)
    state, error = asyncio.run(ns["_office_general_room_state"](
        env, occupant_bi="b" * 64, request=object()))
    assert error is None
    assert state["occupied"] is True
    assert memo[("b" * 64)]["state"] == state

    # Both attempts abort: the memoized answer serves, the abort is logged
    # with its reason, and no 503 reaches the client.
    ns, env, attempts, aborts = _room_state_harness(
        [Exception("internal error; reference = x")] * 2, memo)
    state, error = asyncio.run(ns["_office_general_room_state"](
        env, occupant_bi="b" * 64, request=object()))
    assert error is None
    assert state["occupied"] is True
    assert attempts == ["fetch", "fetch"]
    assert aborts == [(
        "/api/world/office/general/status",
        "internal error; reference = x",
    )]

    # A different occupant has no memoized state: the 503 still happens but
    # carries the expected-degraded marker so it is not double-logged.
    ns, env, _attempts, aborts = _room_state_harness(
        [Exception("internal error; reference = y")] * 2, memo)
    state, error = asyncio.run(ns["_office_general_room_state"](
        env, occupant_bi="c" * 64, request=object()))
    assert state is None
    assert error == {"status": 503, "data": {"error": "office_unavailable"}}
    assert len(aborts) == 1


def test_office_entry_never_uses_the_stale_status_memo():
    memo = {"": {
        "ts": 1_800_000_000_000,
        "state": {
            "occupied": False, "codeConfigured": False, "canSetCode": False,
        },
    }}
    ns, env, attempts, aborts = _room_state_harness(
        [Exception("aborted")] * 2, memo)
    state, error = asyncio.run(ns["_office_general_room_state"](
        env, "a" * 64, code_approved=True, request=object()))
    assert state is None
    assert error == {"status": 503, "data": {"error": "office_unavailable"}}
    assert attempts == ["fetch", "fetch"]
    assert len(aborts) == 1
