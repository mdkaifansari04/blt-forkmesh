"""Executable persistence and authorization tests for direct messages."""

import asyncio
import hashlib
import json
from pathlib import Path
import sqlite3
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import chat_direct_messages_api as api  # noqa: E402


def run_async_test(function):
    def wrapper():
        return asyncio.run(function())

    wrapper.__name__ = function.__name__
    return wrapper


class FakeRuntime:
    def __init__(self):
        self.db = sqlite3.connect(":memory:")
        self.db.row_factory = sqlite3.Row
        self.db.executescript(
            (ROOT / "migrations" / "0079_chat_direct_messages.sql")
            .read_text(encoding="utf-8")
        )
        self.actor = ""
        self.request_method = "GET"
        self.request_data = {}
        self.clock = 1_800_000_000_000
        self.accounts = {"admin", "alice", "bob", "carol"}
        self.ids = 0
        self.fail_batch_at = None
        self.room_access_calls = []

    def use(self, method, actor="", data=None):
        self.request_method = method
        self.actor = actor
        self.request_data = {} if data is None else data
        return self

    def method(self):
        return self.request_method

    def now(self):
        return self.clock

    def new_id(self):
        self.ids += 1
        return f"{self.ids:032x}"

    def response(self, data, status=200, cache_control=None,
                 extra_headers=None):
        return {
            "status": status,
            "data": data,
            "cache_control": cache_control,
            "headers": dict(extra_headers or {}),
        }

    async def ensure_schema(self):
        return None

    async def json_body(self, _limit):
        if not isinstance(self.request_data, dict):
            return None, "invalid_json"
        return self.request_data, ""

    async def session(self, _data=None):
        if self.actor not in self.accounts:
            return "", None
        return f"bi:{self.actor}", {
            "name": self.actor,
            "status": "active",
        }

    async def account(self, name):
        normalized = str(name or "").strip().lower()
        if normalized not in self.accounts:
            return "", ""
        return f"bi:{normalized}", normalized

    async def blind(self, value):
        digest = hashlib.sha256(str(value).encode()).hexdigest()
        return "blind:" + digest

    async def seal(self, value):
        return json.dumps(value, sort_keys=True)

    async def open(self, value):
        return json.loads(value)

    async def room_access(self, conversation_id, key_version, account_bi):
        self.room_access_calls.append(
            (conversation_id, key_version, account_bi)
        )
        return {
            "room": f"chat-direct:{conversation_id}:v{key_version}",
            "passphrase": f"key:{conversation_id}:{key_version}",
            "webSocketUrl": (
                f"/api/chat/direct-messages/{conversation_id}/ws?ticket="
                f"ticket:{account_bi}"
            ),
        }

    async def d1_all(self, sql, *args):
        return [dict(row) for row in self.db.execute(sql, args).fetchall()]

    async def d1_first(self, sql, *args):
        row = self.db.execute(sql, args).fetchone()
        return dict(row) if row is not None else None

    async def batch(self, statements):
        self.db.execute("BEGIN")
        try:
            for index, (sql, args) in enumerate(statements):
                if self.fail_batch_at == index:
                    raise RuntimeError("forced_batch_failure")
                self.db.execute(sql, args)
            self.db.commit()
        except Exception:
            self.db.rollback()
            raise

    def scalar(self, sql):
        return self.db.execute(sql).fetchone()[0]


async def start_dm(runtime, actor, username):
    return await api.handle(
        runtime.use("POST", actor, {"username": username}),
        "/api/chat/direct-messages",
    )


async def list_dms(runtime, actor):
    return await api.handle(
        runtime.use("GET", actor),
        "/api/chat/direct-messages",
    )


async def room_access(runtime, actor, conversation_id):
    return await api.handle(
        runtime.use("GET", actor),
        f"/api/chat/direct-messages/{conversation_id}/room-access",
    )


@run_async_test
async def test_pair_creation_is_atomic_and_reverse_order_is_idempotent():
    runtime = FakeRuntime()
    created = await start_dm(runtime, "alice", "bob")
    reopened = await start_dm(runtime, "bob", "alice")

    assert created["status"] == 201
    assert reopened["status"] == 200
    assert reopened["data"]["conversation"]["id"] == (
        created["data"]["conversation"]["id"]
    )
    assert created["data"]["conversation"]["otherUser"] == "bob"
    assert reopened["data"]["conversation"]["otherUser"] == "alice"
    assert runtime.scalar(
        "SELECT COUNT(*) FROM chat_direct_conversations"
    ) == 1
    assert runtime.scalar(
        "SELECT COUNT(*) FROM chat_direct_participants"
    ) == 2


@run_async_test
async def test_pair_creation_rolls_back_every_row_when_batch_fails():
    runtime = FakeRuntime()
    runtime.fail_batch_at = 1

    try:
        await start_dm(runtime, "alice", "bob")
    except RuntimeError as error:
        assert str(error) == "forced_batch_failure"
    else:
        raise AssertionError("batch failure must escape the API boundary")

    assert runtime.scalar(
        "SELECT COUNT(*) FROM chat_direct_conversations"
    ) == 0
    assert runtime.scalar(
        "SELECT COUNT(*) FROM chat_direct_participants"
    ) == 0


@run_async_test
async def test_only_participants_list_and_access_a_direct_message():
    runtime = FakeRuntime()
    created = await start_dm(runtime, "alice", "bob")
    conversation_id = created["data"]["conversation"]["id"]

    alice = await list_dms(runtime, "alice")
    bob = await list_dms(runtime, "bob")
    admin = await list_dms(runtime, "admin")
    assert alice["data"]["conversations"] == [{
        "id": conversation_id,
        "otherUser": "bob",
        "updatedAt": runtime.clock,
        "keyVersion": 1,
    }]
    assert bob["data"]["conversations"][0]["otherUser"] == "alice"
    assert admin["data"]["conversations"] == []

    allowed = await room_access(runtime, "alice", conversation_id)
    denied = await room_access(runtime, "admin", conversation_id)
    missing = await room_access(runtime, "admin", "f" * 32)
    assert allowed["status"] == 200
    assert allowed["data"]["conversation"]["otherUser"] == "bob"
    assert allowed["cache_control"].startswith("no-store")
    assert denied["status"] == missing["status"] == 404
    assert denied["data"] == missing["data"] == {"error": "not_found"}
    assert runtime.room_access_calls == [(conversation_id, 1, "bi:alice")]


@run_async_test
async def test_self_missing_and_invalid_sessions_are_rejected():
    runtime = FakeRuntime()

    self_message = await start_dm(runtime, "alice", "alice")
    missing_user = await start_dm(runtime, "alice", "missing")
    unauthenticated = await list_dms(runtime, "guest")

    assert self_message["status"] == 400
    assert self_message["data"] == {"error": "cannot_message_self"}
    assert missing_user["status"] == 404
    assert missing_user["data"] == {"error": "user_not_found"}
    assert unauthenticated["status"] == 401
    assert unauthenticated["data"] == {"error": "invalid_session"}
    assert runtime.scalar(
        "SELECT COUNT(*) FROM chat_direct_conversations"
    ) == 0


@run_async_test
async def test_collection_validates_json_and_methods():
    runtime = FakeRuntime()
    invalid_json = await api.handle(
        runtime.use("POST", "alice", []),
        "/api/chat/direct-messages",
    )
    wrong_method = await api.handle(
        runtime.use("DELETE", "alice"),
        "/api/chat/direct-messages",
    )
    wrong_resource_method = await api.handle(
        runtime.use("POST", "alice", {"username": "bob"}),
        f"/api/chat/direct-messages/{'a' * 32}/room-access",
    )

    assert invalid_json["status"] == 400
    assert invalid_json["data"] == {"error": "invalid_json"}
    assert wrong_method["status"] == 405
    assert wrong_method["headers"]["allow"] == "GET, POST"
    assert wrong_resource_method["status"] == 405
    assert wrong_resource_method["headers"]["allow"] == "GET"
