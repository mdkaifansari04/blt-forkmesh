#!/usr/bin/env python3
"""Authorization and runtime contracts for spatial Office meeting rooms."""

import ast
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
    assert "meetingWebSocketUrl" in access_source
    assert "expiresAt" in access_source
    assert "no-store, max-age=0, must-revalidate" in access_source


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
    for forbidden in (
        "ctx.storage.put",
        "ctx.storage.get",
        "chat_history",
        "roomKey",
        "passphrase",
        "attachment",
    ):
        assert forbidden not in source
