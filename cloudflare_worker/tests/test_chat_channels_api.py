"""Executable authorization and persistence tests for private chat channels."""

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

import chat_channels_api as api  # noqa: E402


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
            (ROOT / "migrations" / "0069_private_chat_channels.sql")
            .read_text(encoding="utf-8")
        )
        self.actor = ""
        self.request_method = "GET"
        self.request_data = {}
        self.clock = 1_800_000_000_000
        self.admins = {"admin"}
        self.accounts = {"admin", "alice", "bob"}
        self.audits = []
        self.revoked_rooms = []
        self.ids = 0

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

    async def is_admin(self, name):
        return name in self.admins

    async def account(self, name):
        normalized = str(name or "").strip().lower()
        if normalized not in self.accounts:
            return "", ""
        return f"bi:{normalized}", normalized

    async def blind(self, value):
        digest = hashlib.sha256(str(value).strip().lower().encode()).hexdigest()
        return "blind:" + digest

    async def seal(self, value):
        return json.dumps(value, sort_keys=True)

    async def open(self, value):
        return json.loads(value)

    async def audit(self, actor, action, target_type="", target="",
                    outcome="success", details=None):
        self.audits.append({
            "actor": actor,
            "action": action,
            "target_type": target_type,
            "target": target,
            "outcome": outcome,
            "details": details or {},
        })

    async def room_access(self, channel_id, key_version, account_bi):
        return {
            "room": f"private-{channel_id}-v{key_version}",
            "passphrase": f"key:{channel_id}:{key_version}",
            "webSocketUrl": (
                f"/api/chat/channels/{channel_id}/ws?ticket="
                f"ticket:{account_bi}"
            ),
        }

    async def revoke_room(self, channel_id, key_version):
        self.revoked_rooms.append((channel_id, key_version))

    async def d1_all(self, sql, *args):
        return [dict(row) for row in self.db.execute(sql, args).fetchall()]

    async def d1_first(self, sql, *args):
        row = self.db.execute(sql, args).fetchone()
        return dict(row) if row is not None else None

    async def d1_run(self, sql, *args):
        cursor = self.db.execute(sql, args)
        self.db.commit()
        return {"changes": cursor.rowcount}


async def create_channel(runtime, name):
    return await api.handle(
        runtime.use("POST", "admin", {"name": name}),
        "/api/chat/channels",
    )


@run_async_test
async def test_admin_creates_and_non_admin_cannot_create_channel():
    runtime = FakeRuntime()
    denied = await api.handle(
        runtime.use("POST", "alice", {"name": "release-team"}),
        "/api/chat/channels",
    )
    assert denied["status"] == 403
    assert denied["data"] == {"error": "admin_required"}
    assert runtime.audits[-1]["outcome"] == "denied"

    created = await api.handle(
        runtime.use("POST", "admin", {"name": "release-team"}),
        "/api/chat/channels",
    )
    assert created["status"] == 201
    assert created["data"]["channel"] == {
        "id": "00000000000000000000000000000001",
        "name": "release-team",
        "updatedAt": runtime.clock,
        "keyVersion": 1,
        "canManage": True,
    }
    stored = runtime.db.execute(
        "SELECT name_bi,data,created_by_bi,key_version FROM chat_channels"
    ).fetchone()
    assert stored["name_bi"] != "release-team"
    assert json.loads(stored["data"])["name"] == "release-team"
    assert stored["created_by_bi"] == "bi:admin"
    assert stored["key_version"] == 1


@run_async_test
async def test_channel_collection_requires_a_session_and_validates_names():
    runtime = FakeRuntime()
    unauthenticated = await api.handle(
        runtime.use("GET"), "/api/chat/channels")
    assert unauthenticated["status"] == 401
    assert unauthenticated["data"] == {"error": "invalid_session"}

    invalid = await create_channel(runtime, "Release Team")
    assert invalid["status"] == 400
    assert invalid["data"] == {"error": "invalid_channel_name"}

    created = await create_channel(runtime, "release-team")
    duplicate = await create_channel(runtime, "release-team")
    assert created["status"] == 201
    assert duplicate["status"] == 409
    assert duplicate["data"] == {"error": "channel_name_taken"}


@run_async_test
async def test_invited_user_lists_and_accesses_only_their_channel():
    runtime = FakeRuntime()
    first = await create_channel(runtime, "release-team")
    second = await create_channel(runtime, "security")
    channel_id = first["data"]["channel"]["id"]
    granted = await api.handle(
        runtime.use("POST", "admin", {"username": "alice"}),
        f"/api/chat/channels/{channel_id}/members",
    )
    assert granted["status"] == 201
    assert granted["data"]["member"]["username"] == "alice"

    listed = await api.handle(
        runtime.use("GET", "alice"), "/api/chat/channels")
    assert [item["name"] for item in listed["data"]["channels"]] == [
        "release-team"
    ]
    assert second["data"]["channel"]["id"] not in json.dumps(listed["data"])

    access = await api.handle(
        runtime.use("GET", "alice"),
        f"/api/chat/channels/{channel_id}/room-access",
    )
    assert access["status"] == 200
    assert access["data"]["channel"]["name"] == "release-team"
    assert access["data"]["passphrase"].startswith("key:")
    assert access["cache_control"].startswith("no-store")


@run_async_test
async def test_member_management_is_admin_only_and_idempotent():
    runtime = FakeRuntime()
    created = await create_channel(runtime, "release-team")
    channel_id = created["data"]["channel"]["id"]
    path = f"/api/chat/channels/{channel_id}/members"

    denied = await api.handle(
        runtime.use("POST", "alice", {"username": "bob"}), path)
    assert denied["status"] == 403
    assert denied["data"] == {"error": "admin_required"}
    assert runtime.audits[-1]["outcome"] == "denied"

    first = await api.handle(
        runtime.use("POST", "admin", {"username": "alice"}), path)
    again = await api.handle(
        runtime.use("POST", "admin", {"username": "alice"}), path)
    assert first["status"] == 201
    assert again["status"] == 200
    members = await api.handle(runtime.use("GET", "admin"), path)
    assert members["data"]["members"] == [
        {"username": "alice", "joinedAt": runtime.clock}
    ]

    runtime.accounts.remove("bob")
    missing = await api.handle(
        runtime.use("POST", "admin", {"username": "bob"}), path)
    assert missing["status"] == 404
    assert missing["data"] == {"error": "user_not_found"}


@run_async_test
async def test_member_removal_rotates_once_and_revokes_access():
    runtime = FakeRuntime()
    created = await create_channel(runtime, "release-team")
    channel_id = created["data"]["channel"]["id"]
    path = f"/api/chat/channels/{channel_id}/members"
    await api.handle(
        runtime.use("POST", "admin", {"username": "alice"}), path)

    removed = await api.handle(
        runtime.use("DELETE", "admin", {"username": "alice"}), path)
    assert removed["status"] == 200
    assert removed["data"]["removed"] is True
    assert removed["data"]["keyVersion"] == 2
    assert runtime.revoked_rooms == [(channel_id, 1)]

    again = await api.handle(
        runtime.use("DELETE", "admin", {"username": "alice"}), path)
    assert again["status"] == 200
    assert again["data"]["removed"] is False
    assert again["data"]["keyVersion"] == 2
    assert runtime.revoked_rooms == [(channel_id, 1)]

    denied = await api.handle(
        runtime.use("GET", "alice"),
        f"/api/chat/channels/{channel_id}/room-access",
    )
    missing = await api.handle(
        runtime.use("GET", "alice"),
        f"/api/chat/channels/{'f' * 32}/room-access",
    )
    assert denied["status"] == missing["status"] == 404
    assert denied["data"] == missing["data"] == {"error": "not_found"}


@run_async_test
async def test_member_removal_does_not_depend_on_d1_run_metadata():
    class ProductionLikeRuntime(FakeRuntime):
        async def d1_run(self, sql, *args):
            await super().d1_run(sql, *args)
            return None

    runtime = ProductionLikeRuntime()
    created = await create_channel(runtime, "release-team")
    channel_id = created["data"]["channel"]["id"]
    path = f"/api/chat/channels/{channel_id}/members"
    await api.handle(
        runtime.use("POST", "admin", {"username": "alice"}), path)

    removed = await api.handle(
        runtime.use("DELETE", "admin", {"username": "alice"}), path)
    assert removed["data"]["removed"] is True
    assert removed["data"]["keyVersion"] == 2


@run_async_test
async def test_all_admins_have_implicit_access_without_membership_rows():
    runtime = FakeRuntime()
    runtime.admins.add("bob")
    created = await create_channel(runtime, "release-team")
    channel_id = created["data"]["channel"]["id"]

    listed = await api.handle(
        runtime.use("GET", "bob"), "/api/chat/channels")
    assert [item["id"] for item in listed["data"]["channels"]] == [channel_id]
    access = await api.handle(
        runtime.use("GET", "bob"),
        f"/api/chat/channels/{channel_id}/room-access",
    )
    assert access["status"] == 200
    count = runtime.db.execute(
        "SELECT COUNT(*) FROM chat_channel_members"
    ).fetchone()[0]
    assert count == 0
