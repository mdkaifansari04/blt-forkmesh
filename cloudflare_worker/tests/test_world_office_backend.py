#!/usr/bin/env python3
"""Authorization and runtime contracts for spatial Office meeting rooms."""

import ast
import asyncio
import base64
import hmac
import re
import tomllib
from pathlib import Path


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

    class _FakeResponse(dict):
        def __init__(self, data, status):
            super().__init__(status=status, data=data)
            self.status = status

    async def blind_index(_env, _value):
        return "a" * 64

    async def room_state(_env, _rate_key, code_approved=None):
        assert isinstance(code_approved, bool)
        if state["occupied"] and not code_approved:
            return None, _FakeResponse(
                {"error": "invalid_entry_code"}, 403)
        return dict(state), None

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
    return namespace["office_general_entry_handler"], state


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
        "/api/world/office/general/access",
        "/api/world/office/general/ws",
    ):
        assert route in route_source
    assert "OFFICE_CHANNEL_WS_RE" in route_source
    assert "/api/world/office/channels/" in ENTRY_TEXT
    assert "office_general_access_handler" in route_source
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
    assert "_office_entry_rate_step" in source
    assert "len(self._live_sockets(cleanup=True))" in source
    assert source.index("len(self._live_sockets(cleanup=True))") < (
        source.index("entry_code_required"))
    for forbidden in (
        "ctx.storage.put",
        "ctx.storage.get",
        "chat_history",
        "roomKey",
        "passphrase",
        "attachment",
    ):
        assert forbidden not in source
