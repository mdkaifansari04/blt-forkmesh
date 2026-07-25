#!/usr/bin/env python3
"""Direct-message key, ticket, socket, and live-admission contracts."""

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
    "_chat_direct_passphrase",
    "_chat_direct_ticket",
    "_chat_direct_ticket_claims",
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


class _Env:
    DATA_KEY = "private-production-data-key"


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
        "CHAT_DIRECT_TICKET_TTL_MS": 60 * 1000,
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


def _socket_harness(participant=True, version=1, account_status="active"):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    names = FUNCTIONS | {"_chat_direct_socket_handler"}
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    assert found == names, "missing: %s" % sorted(names - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    calls = []
    clock = [1_800_000_000_000]

    class _Date:
        @staticmethod
        def now():
            return clock[0]

    async def d1_first(_env, sql, *args):
        if "FROM chat_direct_conversations" in sql:
            return {"data": "sealed-direct", "key_version": version}
        if "FROM users" in sql:
            return {"data": "sealed-user", "is_admin": 1}
        if "FROM chat_direct_participants" in sql:
            return {"allowed": 1} if participant else None
        raise AssertionError(sql)

    async def decrypt_row(_env, data):
        if data == "sealed-direct":
            return {"participants": ["alice", "bob"]}
        return {"status": account_status, "kind": "user"}

    namespace = {
        "CHAT_DIRECT_TICKET_TTL_MS": 60 * 1000,
        "Date": _Date,
        "Uint8Array": _FakeU8,
        "_INSECURE_DATA_KEYS": frozenset({"", "forkmesh-dev-data-key"}),
        "_account_kind": lambda record: record.get("kind", "user"),
        "_private_replica_not_found": lambda: {
            "status": 404, "data": {"error": "not_found"}},
        "_to_js": lambda value: value,
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
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
        "quote": quote,
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
                "https://forkmesh.test/api/chat/direct-messages/"
                + "a" * 32
                + "/ws?ticket="
                + quote(token, safe="")
            )

    return namespace, _SocketEnv(), _Request, calls


def _room_key_from_path(path, account_bi):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    node = next(
        item for item in tree.body
        if isinstance(item, ast.FunctionDef)
        and item.name == "room_key_from_path"
    )
    namespace = {
        "MAX_ROOM_NAME": 80,
        "REPO_ROOM_RE": re.compile(
            r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$"
        ),
        "ROOM_RE": re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$"),
        "CHAT_CHANNEL_DO_RE": re.compile(
            r"^/api/chat/channels/([0-9a-f]{32})/v([1-9][0-9]*)/"
            r"(ws|revoke)$"
        ),
        "CHAT_DIRECT_MESSAGE_DO_RE": re.compile(
            r"^/api/chat/direct-messages/([0-9a-f]{32})/v([1-9][0-9]*)/"
            r"(ws|revoke)$"
        ),
        "re": re,
        "safe_segment": lambda value, _limit=80: str(value or ""),
    }
    module = ast.fix_missing_locations(
        ast.Module(body=[node], type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["room_key_from_path"](path, account_bi)


def _private_room_current():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    room = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "ForkMeshRoom"
    )
    node = next(
        child for child in room.body
        if isinstance(child, ast.AsyncFunctionDef)
        and child.name == "_private_room_current"
    )
    module = ast.fix_missing_locations(
        ast.Module(body=[node], type_ignores=[]))
    return module


def test_direct_key_is_versioned_and_domain_separated():
    namespace, _clock = _helpers()
    env = _Env()
    direct_id = "a" * 32

    first = asyncio.run(namespace["_chat_direct_passphrase"](env, direct_id, 1))
    rotated = asyncio.run(namespace["_chat_direct_passphrase"](env, direct_id, 2))

    assert first != rotated
    assert len(first) == 64
    assert re.fullmatch(r"[0-9a-f]{64}", first)


def test_direct_ticket_round_trip_and_expiry():
    namespace, clock = _helpers()
    env = _Env()
    direct_id = "a" * 32
    account_bi = "b" * 64

    token = namespace["_chat_direct_ticket"](env, direct_id, 3, account_bi)
    assert namespace["_chat_direct_ticket_claims"](env, token) == {
        "conversation_id": direct_id,
        "key_version": 3,
        "account_bi": account_bi,
    }
    clock[0] += 61_000
    assert namespace["_chat_direct_ticket_claims"](env, token) is None


def test_direct_socket_requires_participant_even_for_an_admin():
    namespace, env, request_type, calls = _socket_harness(participant=True)
    direct_id = "a" * 32
    account_bi = "b" * 64
    token = namespace["_chat_direct_ticket"](env, direct_id, 1, account_bi)
    allowed = asyncio.run(
        namespace["_chat_direct_socket_handler"](
            env, request_type(token), direct_id))
    assert allowed["status"] == 101
    assert ("id", f"chat-direct:{direct_id}:v1") in calls

    denied_namespace, denied_env, denied_request, denied_calls = (
        _socket_harness(participant=False)
    )
    denied_token = denied_namespace["_chat_direct_ticket"](
        denied_env, direct_id, 1, account_bi)
    denied = asyncio.run(
        denied_namespace["_chat_direct_socket_handler"](
            denied_env, denied_request(denied_token), direct_id))
    assert denied == {"status": 404, "data": {"error": "not_found"}}
    assert not denied_calls


def test_direct_socket_rejects_stale_version_and_inactive_account():
    namespace, env, request_type, _calls = _socket_harness(version=2)
    direct_id = "a" * 32
    account_bi = "b" * 64
    token = namespace["_chat_direct_ticket"](env, direct_id, 1, account_bi)
    stale = asyncio.run(
        namespace["_chat_direct_socket_handler"](
            env, request_type(token), direct_id))
    assert stale["status"] == 404

    inactive_ns, inactive_env, inactive_request, _inactive_calls = (
        _socket_harness(account_status="disabled")
    )
    inactive_token = inactive_ns["_chat_direct_ticket"](
        inactive_env, direct_id, 1, account_bi)
    inactive = asyncio.run(
        inactive_ns["_chat_direct_socket_handler"](
            inactive_env, inactive_request(inactive_token), direct_id))
    assert inactive["status"] == 404


def test_internal_direct_room_path_is_isolated_and_carries_identity():
    direct_id = "a" * 32
    account_bi = "b" * 64
    assert _room_key_from_path(
        f"/api/chat/direct-messages/{direct_id}/v1/ws",
        account_bi,
    ) == {
        "key": f"chat-direct:{direct_id}:v1",
        "owner": "",
        "repo": "",
        "room": direct_id,
        "compat": False,
        "direct_id": direct_id,
        "direct_version": 1,
        "account_bi": account_bi,
    }


def test_live_direct_socket_rechecks_participant_membership():
    module = _private_room_current()
    state = {"participant": False}

    async def d1_first(_env, sql, *args):
        if "FROM chat_direct_conversations" in sql:
            return {"data": "sealed-direct", "key_version": 1}
        if "FROM users" in sql:
            return {"data": "sealed-user", "is_admin": 1}
        if "FROM chat_direct_participants" in sql:
            return {"allowed": 1} if state["participant"] else None
        raise AssertionError(sql)

    async def decrypt_row(_env, data):
        if data == "sealed-direct":
            return {"participants": ["alice", "bob"]}
        return {"status": "active", "kind": "user"}

    namespace = {
        "_account_kind": lambda record: record.get("kind", "user"),
        "_ws_attr": lambda ws, key, default=None: ws.get(key, default),
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
        "ensure_schema": lambda _env: asyncio.sleep(0),
        "re": re,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    self = type("Room", (), {"env": _Env()})()
    socket = {
        "direct_id": "a" * 32,
        "direct_version": 1,
        "account_bi": "b" * 64,
    }

    assert asyncio.run(namespace["_private_room_current"](self, socket)) is False
    state["participant"] = True
    assert asyncio.run(namespace["_private_room_current"](self, socket)) is True
