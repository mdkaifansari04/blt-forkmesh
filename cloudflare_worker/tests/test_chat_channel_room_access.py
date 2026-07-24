#!/usr/bin/env python3
"""Private chat channel key, ticket, and socket-admission contracts."""

import ast
import asyncio
import hashlib
import hmac
import re
from pathlib import Path
from urllib.parse import parse_qs, quote, urlparse


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
FUNCTIONS = {
    "_chat_channel_passphrase",
    "_chat_channel_ticket",
    "_chat_channel_ticket_claims",
    "_require_data_secret",
}


class _FakeSubtle:
    async def digest(self, _algorithm, data):
        return hashlib.sha256(bytes(data)).digest()


class _FakeCrypto:
    subtle = _FakeSubtle()


class _FakeU8:
    @staticmethod
    def new(data):
        class _Array:
            def __init__(self, value):
                self.value = bytes(value)

            def to_py(self):
                return self.value

        return _Array(data)


def _helpers():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCTIONS
    ]
    found = {node.name for node in selected}
    assert found == FUNCTIONS, "missing: %s" % sorted(FUNCTIONS - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    clock = [1_800_000_000_000]

    class _Date:
        @staticmethod
        def now():
            return clock[0]

    namespace = {
        "CHAT_CHANNEL_TICKET_TTL_MS": 60 * 1000,
        "Date": _Date,
        "Uint8Array": _FakeU8,
        "_INSECURE_DATA_KEYS": frozenset({"", "forkmesh-dev-data-key"}),
        "_to_js": lambda value: value,
        "hmac": hmac,
        "js_crypto": _FakeCrypto(),
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace, clock


def _socket_harness(channel_version=1, member=True, admin=False):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    names = FUNCTIONS | {"_chat_channel_socket_handler"}
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    calls = []
    clock = [1_800_000_000_000]

    class _Date:
        @staticmethod
        def now():
            return clock[0]

    async def d1_first(_env, sql, *args):
        if "FROM chat_channels" in sql:
            return {"key_version": channel_version}
        if "FROM users" in sql:
            return {"data": "sealed-user", "is_admin": 1 if admin else 0}
        if "FROM chat_channel_members" in sql:
            return {"allowed": 1} if member else None
        raise AssertionError(sql)

    namespace = {
        "CHAT_CHANNEL_TICKET_TTL_MS": 60 * 1000,
        "Date": _Date,
        "Uint8Array": _FakeU8,
        "_INSECURE_DATA_KEYS": frozenset({"", "forkmesh-dev-data-key"}),
        "_account_kind": lambda record: record.get("kind", "user"),
        "_private_replica_not_found": lambda: {
            "status": 404, "data": {"error": "not_found"}},
        "_to_js": lambda value: value,
        "d1_first": d1_first,
        "decrypt_row": lambda _env, _data: asyncio.sleep(0, result={
            "status": "active", "kind": "user"}),
        "durable_object_request": lambda request, target_url=None: asyncio.sleep(
            0, result={"request": request, "target": target_url}),
        "ensure_schema": lambda _env: asyncio.sleep(0),
        "hmac": hmac,
        "js_crypto": _FakeCrypto(),
        "json_response": lambda data, status=200: {
            "status": status, "data": data},
        "log_durable_object_abort": lambda *_args: asyncio.sleep(0),
        "method_name": lambda request: request.method,
        "parse_qs": parse_qs,
        "re": re,
        "urlparse": urlparse,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)

    class _Room:
        def idFromName(self, name):
            calls.append(("id", name))
            return "room-id"

        def get(self, room_id):
            calls.append(("get", room_id))

            class _Stub:
                async def fetch(self, request):
                    calls.append(("fetch", request))
                    return {"status": 101}

            return _Stub()

    class _SocketEnv(_Env):
        FORKMESH_MAINNODE_ROOM = _Room()

    class _Headers:
        def get(self, name, default=None):
            return {"upgrade": "websocket"}.get(str(name).lower(), default)

    class _Request:
        method = "GET"
        headers = _Headers()

        def __init__(self, token):
            self.url = (
                "https://forkmesh.test/api/chat/channels/" + "a" * 32
                + "/ws?ticket=" + quote(token, safe="")
            )

    return namespace, _SocketEnv(), _Request, calls


class _Env:
    DATA_KEY = "private-production-data-key"


def _function_source(name):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    node = next(
        item for item in ast.walk(tree)
        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
        and item.name == name
    )
    return ast.unparse(node)


def test_channel_passphrases_are_scoped_by_channel_and_version():
    namespace, _clock = _helpers()
    env = _Env()
    first = asyncio.run(
        namespace["_chat_channel_passphrase"](env, "a" * 32, 1))
    other_channel = asyncio.run(
        namespace["_chat_channel_passphrase"](env, "b" * 32, 1))
    rotated = asyncio.run(
        namespace["_chat_channel_passphrase"](env, "a" * 32, 2))
    assert len(first) == 64
    assert first != other_channel
    assert first != rotated
    assert env.DATA_KEY not in first


def test_ticket_is_channel_version_account_and_expiry_bound():
    namespace, clock = _helpers()
    env = _Env()
    account_bi = "b" * 64
    token = namespace["_chat_channel_ticket"](
        env, "a" * 32, 3, account_bi)
    assert namespace["_chat_channel_ticket_claims"](env, token) == {
        "channel_id": "a" * 32,
        "key_version": 3,
        "account_bi": account_bi,
    }
    assert namespace["_chat_channel_ticket_claims"](env, token + "x") is None
    assert namespace["_chat_channel_ticket_claims"](
        env, token.replace("." + account_bi + ".", "." + "c" * 64 + ".")
    ) is None
    clock[0] += 60_001
    assert namespace["_chat_channel_ticket_claims"](env, token) is None


def test_ticket_rejects_malformed_and_insecure_secret_inputs():
    namespace, _clock = _helpers()
    assert namespace["_chat_channel_ticket_claims"](_Env(), "") is None
    assert namespace["_chat_channel_ticket_claims"](
        _Env(), "v1.release-team.1.user.999.signature") is None

    class _BadEnv:
        DATA_KEY = "forkmesh-dev-data-key"

    try:
        namespace["_chat_channel_ticket"](
            _BadEnv(), "a" * 32, 1, "b" * 64)
    except RuntimeError as error:
        assert "DATA_KEY" in str(error)
    else:
        raise AssertionError("placeholder DATA_KEY must fail closed")


def test_private_socket_authorization_precedes_durable_object_lookup():
    source = _function_source("_chat_channel_socket_handler")
    assert source.index("_chat_channel_ticket_claims") < source.index(
        "idFromName")
    assert source.index("chat_channel_members") < source.index("idFromName")
    assert source.index("users") < source.index("idFromName")
    assert "chat-channel:" in source
    assert ":v" in source
    assert "_private_replica_not_found" in source


def test_private_socket_routes_only_current_authorized_members():
    namespace, env, request_type, calls = _socket_harness()
    token = namespace["_chat_channel_ticket"](
        env, "a" * 32, 1, "b" * 64)
    response = asyncio.run(namespace["_chat_channel_socket_handler"](
        env, request_type(token), "a" * 32))
    assert response["status"] == 101
    assert calls[0] == ("id", "chat-channel:" + "a" * 32 + ":v1")
    assert calls[-1][0] == "fetch"

    for kwargs in (
        {"member": False},
        {"channel_version": 2},
    ):
        denied_ns, denied_env, denied_request, denied_calls = _socket_harness(
            **kwargs)
        denied_token = denied_ns["_chat_channel_ticket"](
            denied_env, "a" * 32, 1, "b" * 64)
        denied = asyncio.run(denied_ns["_chat_channel_socket_handler"](
            denied_env, denied_request(denied_token), "a" * 32))
        assert denied == {"status": 404, "data": {"error": "not_found"}}
        assert denied_calls == []


def test_private_socket_allows_current_admin_without_membership_row():
    namespace, env, request_type, calls = _socket_harness(
        member=False, admin=True)
    token = namespace["_chat_channel_ticket"](
        env, "a" * 32, 1, "b" * 64)
    response = asyncio.run(namespace["_chat_channel_socket_handler"](
        env, request_type(token), "a" * 32))
    assert response["status"] == 101
    assert calls[0][0] == "id"


def test_runtime_adapter_and_routes_use_private_channel_gates():
    assert "import chat_channels_api" in ENTRY_TEXT
    for route_name in (
        "CHAT_CHANNELS_RE",
        "CHAT_CHANNEL_MEMBERS_RE",
        "CHAT_CHANNEL_ROOM_ACCESS_RE",
        "CHAT_CHANNEL_WS_RE",
    ):
        assert route_name in ENTRY_TEXT
    assert "class _ChatChannelsRuntime(_WorldCommunityRuntime)" in ENTRY_TEXT
    runtime_source = ENTRY_TEXT[
        ENTRY_TEXT.index("class _ChatChannelsRuntime"):
        ENTRY_TEXT.index("class _ChatChannelsRuntime") + 1800
    ]
    assert "_chat_channel_passphrase" in runtime_source
    assert "_chat_channel_ticket" in runtime_source
    assert "quote(" in runtime_source

    route = _function_source("_route")
    api_at = route.index("chat_channels_api.handle")
    socket_at = route.index("_chat_channel_socket_handler")
    generic_room_at = route.index("room_key_from_path")
    assert api_at < generic_room_at
    assert socket_at < generic_room_at
