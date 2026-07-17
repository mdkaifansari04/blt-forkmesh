#!/usr/bin/env python3
"""Server-held room-chat key endpoint (/api/chat/room-key).

The room key is derived one-way from DATA_KEY and handed only to authenticated
clients, replacing the old public app-wide constant. Verifies: unauthenticated
callers are rejected; a valid account session token OR a node Ed25519 signature
is accepted; the passphrase is deterministic (so every client derives the same
room key) and never leaks DATA_KEY. AST-extraction harness (test_repo_agents.py
style) with WebCrypto SHA-256 stubbed by hashlib.
"""

import ast
import asyncio
import hashlib
import hmac
import re
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + CATALOG.read_text(encoding="utf-8")
    + "\n" + SCHEMA.read_text(encoding="utf-8"))

FUNCS = {
    "chat_room_key_handler", "_room_key_authorized", "_room_chat_passphrase",
    "_require_data_secret", "_authed_account_name", "_account_session_record",
    "_account_session_token", "_account_session_token_name",
    "_account_session_signature", "_account_session_secret", "_account_kind",
    "valid_node_name", "clean_string", "_owner_pubkey",
}


def _load(extra):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [n for n in tree.body
                if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                and n.name in FUNCS]
    assert {n.name for n in selected} == FUNCS, \
        "missing: %s" % sorted(FUNCS - {n.name for n in selected})
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = dict(extra)
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


class _Headers:
    def __init__(self, m=None):
        self._m = {str(k).lower(): v for k, v in (m or {}).items()}

    def get(self, name, default=None):
        return self._m.get(str(name).lower(), default)


class _Request:
    def __init__(self, method="GET", url="https://forkmesh.test/api/chat/room-key",
                 headers=None):
        self.method = method
        self.url = url
        self.headers = _Headers(headers)


# --- WebCrypto SHA-256 digest stub (hashlib) --------------------------------
class _FakeSubtle:
    async def digest(self, _algo, data):
        return hashlib.sha256(bytes(data)).digest()


class _FakeCrypto:
    subtle = _FakeSubtle()


class _FakeU8:
    @staticmethod
    def new(data):
        class _Arr:
            def __init__(self, d):
                self._d = bytes(d)

            def to_py(self):
                return self._d
        return _Arr(data)


def _harness(accounts, data_key="a-real-secret-data-key"):
    now = [1_000_000_000]

    class _DateStub:
        @staticmethod
        def now():
            return now[0]

    def json_response(data, status=200, **_k):
        return {"status": status, "data": data}

    async def ensure_schema(_e):
        return None

    async def _account_row(_e, name):
        key = str(name or "").strip().lower()
        rec = accounts.get(key)
        if rec is None:
            return "bi:" + key, None
        rec = dict(rec)
        rec.setdefault("name", key)
        return "bi:" + key, rec

    async def ed25519_verify(pubkey, sig, _canonical):
        # A node "signs" by presenting sig == "sig-for:<pubkey>".
        return bool(pubkey) and sig == "sig-for:" + pubkey

    def method_name(request):
        return getattr(request, "method", "GET")

    def _ts_ok(_ts):
        return True

    class _Env:
        DATA_KEY = data_key

    ns = _load({
        "Date": _DateStub,
        "json_response": json_response,
        "ensure_schema": ensure_schema,
        "_account_row": _account_row,
        "ed25519_verify": ed25519_verify,
        "method_name": method_name,
        "_ts_ok": _ts_ok,
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "hmac": hmac,
        "js_crypto": _FakeCrypto(),
        "_to_js": lambda x: x,
        "Uint8Array": _FakeU8,
        "_room_key_cache": {"secret": None, "value": None},
        "_INSECURE_DATA_KEYS": frozenset({"", "forkmesh-dev-data-key"}),
        "MAX_NODE_NAME": 63,
        "NODE_NAME_RE": re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$"),
        "ADMIN_SESSION_TTL_MS": 12 * 60 * 60 * 1000,
    })
    return ns, _Env()


def _user():
    return {"pubkey": "PK-alice", "status": "active", "pass_hash": "h",
            "pass_salt": "s", "is_admin": False}


def test_unauthenticated_is_rejected():
    ns, env = _harness({"alice": _user()})
    resp = asyncio.run(ns["chat_room_key_handler"](env, _Request()))
    assert resp["status"] == 401
    assert "passphrase" not in resp["data"]


def test_session_token_gets_the_passphrase():
    ns, env = _harness({"alice": _user()})
    token = ns["_account_session_token"](env, "alice")
    resp = asyncio.run(ns["chat_room_key_handler"](
        env, _Request(headers={"authorization": "Bearer " + token})))
    assert resp["status"] == 200
    assert isinstance(resp["data"]["passphrase"], str)
    assert len(resp["data"]["passphrase"]) == 64  # sha256 hex


def test_node_signature_gets_the_passphrase():
    ns, env = _harness({"alice": _user()})
    url = ("https://forkmesh.test/api/chat/room-key"
           "?node=alice&ts=1000000000&sig=sig-for:PK-alice")
    resp = asyncio.run(ns["chat_room_key_handler"](env, _Request(url=url)))
    assert resp["status"] == 200
    assert len(resp["data"]["passphrase"]) == 64
    # A wrong signature is rejected.
    bad = asyncio.run(ns["chat_room_key_handler"](env, _Request(
        url="https://forkmesh.test/api/chat/room-key?node=alice&ts=1&sig=nope")))
    assert bad["status"] == 401


def test_passphrase_is_deterministic_and_key_derived_not_raw_data_key():
    ns, env = _harness({"alice": _user()}, data_key="super-secret-data-key")
    token = ns["_account_session_token"](env, "alice")
    a = asyncio.run(ns["chat_room_key_handler"](
        env, _Request(headers={"authorization": "Bearer " + token})))["data"]["passphrase"]
    b = asyncio.run(ns["chat_room_key_handler"](
        env, _Request(headers={"authorization": "Bearer " + token})))["data"]["passphrase"]
    assert a == b  # every client must derive the same room key
    # The passphrase must be a one-way digest, never the DATA_KEY itself.
    assert a != "super-secret-data-key"
    assert "super-secret-data-key" not in a
    expected = hashlib.sha256(
        b"super-secret-data-key:room-chat-passphrase-v1").hexdigest()
    assert a == expected


def test_placeholder_data_key_fails_closed():
    ns, env = _harness({"alice": _user()}, data_key="")
    token = ns["_account_session_token"](env, "alice")
    try:
        asyncio.run(ns["chat_room_key_handler"](
            env, _Request(headers={"authorization": "Bearer " + token})))
    except RuntimeError:
        return  # _require_data_secret refuses a placeholder key
    raise AssertionError("expected RuntimeError on placeholder DATA_KEY")
