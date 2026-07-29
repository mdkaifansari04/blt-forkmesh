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
        self.fail_batch_at = None
        # room key -> [(ts, encrypted body)], as the room Durable Object
        # retained it. Bodies are opaque here exactly as they are in D1.
        self.retained = {}
        self.request_query = {}

    def use(self, method, actor="", data=None, query=None):
        self.request_method = method
        self.actor = actor
        self.request_data = {} if data is None else data
        self.request_query = dict(query or {})
        return self

    def query_params(self):
        return dict(self.request_query)

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

    async def room_access(self, channel_id, key_version, account_bi, actor):
        assert actor == self.actor
        return {
            "room": f"private-{channel_id}-v{key_version}",
            "passphrase": f"key:{channel_id}:{key_version}",
            "webSocketUrl": (
                f"/api/chat/channels/{channel_id}/ws?ticket="
                f"ticket:{account_bi}"
            ),
            "meetingWebSocketUrl": (
                f"/api/world/office/channels/{channel_id}/ws?ticket="
                f"meeting:{account_bi}"
            ),
        }

    async def revoke_room(self, channel_id, key_version):
        self.revoked_rooms.append((channel_id, key_version))

    async def channel_passphrase(self, channel_id, key_version):
        return f"key:{channel_id}:{key_version}"

    async def history(self, channel_id, key_version, since_ts=0):
        room = f"chat-channel:{channel_id}:v{key_version}"
        return [
            {"ts": ts, "body": body}
            for ts, body in self.retained.get(room, [])
            if ts > int(since_ts or 0)
        ]

    async def d1_all(self, sql, *args):
        return [dict(row) for row in self.db.execute(sql, args).fetchall()]

    async def d1_first(self, sql, *args):
        row = self.db.execute(sql, args).fetchone()
        return dict(row) if row is not None else None

    async def d1_run(self, sql, *args):
        cursor = self.db.execute(sql, args)
        self.db.commit()
        return {"changes": cursor.rowcount}

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


async def create_channel(runtime, name, visibility=None, members=None):
    data = {"name": name}
    if visibility is not None:
        data["visibility"] = visibility
    if members is not None:
        data["members"] = members
    return await api.handle(
        runtime.use("POST", "admin", data),
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
        "visibility": "private",
    }
    stored = runtime.db.execute(
        "SELECT name_bi,data,created_by_bi,key_version FROM chat_channels"
    ).fetchone()
    assert stored["name_bi"] != "release-team"
    assert json.loads(stored["data"])["name"] == "release-team"
    assert stored["created_by_bi"] == "bi:admin"
    assert stored["key_version"] == 1


@run_async_test
async def test_admin_creates_private_channel_with_initial_registered_members():
    runtime = FakeRuntime()
    created = await create_channel(
        runtime,
        "release-team",
        visibility="private",
        members=["Alice", "alice", "admin", "bob"],
    )

    assert created["status"] == 201
    assert created["data"]["channel"]["visibility"] == "private"
    assert created["data"]["members"] == [
        {"username": "alice", "joinedAt": runtime.clock},
        {"username": "bob", "joinedAt": runtime.clock},
    ]
    rows = runtime.db.execute(
        "SELECT data FROM chat_channel_members ORDER BY member_bi"
    ).fetchall()
    assert sorted(json.loads(row["data"])["username"] for row in rows) == [
        "alice",
        "bob",
    ]


@run_async_test
async def test_channel_creation_rejects_invalid_visibility_and_members():
    runtime = FakeRuntime()

    invalid_visibility = await create_channel(
        runtime, "release-team", visibility="shared")
    assert invalid_visibility["status"] == 400
    assert invalid_visibility["data"] == {"error": "invalid_visibility"}

    invalid_members = await create_channel(
        runtime, "release-team", visibility="private", members="alice")
    assert invalid_members["status"] == 400
    assert invalid_members["data"] == {"error": "invalid_members"}

    invalid_member_item = await create_channel(
        runtime, "release-team", visibility="private", members=[1])
    assert invalid_member_item["status"] == 400
    assert invalid_member_item["data"] == {"error": "invalid_members"}

    too_many_members = await create_channel(
        runtime,
        "release-team",
        visibility="private",
        members=["alice"] * (api.MAX_CHANNEL_MEMBERS + 1),
    )
    assert too_many_members["status"] == 429
    assert too_many_members["data"] == {"error": "too_many_members"}

    public_members = await create_channel(
        runtime, "release-team", visibility="public", members=["alice"])
    assert public_members["status"] == 400
    assert public_members["data"] == {"error": "members_not_allowed"}

    missing_user = await create_channel(
        runtime, "release-team", visibility="private", members=["missing"])
    assert missing_user["status"] == 404
    assert missing_user["data"] == {"error": "user_not_found"}
    assert runtime.db.execute(
        "SELECT COUNT(*) FROM chat_channels").fetchone()[0] == 0
    assert runtime.db.execute(
        "SELECT COUNT(*) FROM chat_channel_members").fetchone()[0] == 0


@run_async_test
async def test_channel_and_initial_members_are_written_in_one_transaction():
    runtime = FakeRuntime()
    runtime.fail_batch_at = 1

    try:
        await create_channel(
            runtime,
            "release-team",
            visibility="private",
            members=["alice"],
        )
    except RuntimeError as error:
        assert str(error) == "forced_batch_failure"
    else:
        raise AssertionError("batch failure must escape the API boundary")

    assert runtime.db.execute(
        "SELECT COUNT(*) FROM chat_channels").fetchone()[0] == 0
    assert runtime.db.execute(
        "SELECT COUNT(*) FROM chat_channel_members").fetchone()[0] == 0


@run_async_test
async def test_registered_users_list_and_access_public_but_not_other_private_channels():
    runtime = FakeRuntime()
    public = await create_channel(
        runtime, "announcements", visibility="public")
    private = await create_channel(
        runtime,
        "release-team",
        visibility="private",
        members=["alice"],
    )

    alice_list = await api.handle(
        runtime.use("GET", "alice"), "/api/chat/channels")
    bob_list = await api.handle(
        runtime.use("GET", "bob"), "/api/chat/channels")
    assert [item["name"] for item in alice_list["data"]["channels"]] == [
        "announcements",
        "release-team",
    ]
    assert "members" not in alice_list["data"]["channels"][0]
    assert alice_list["data"]["channels"][1]["members"] == ["admin", "alice"]
    assert [item["name"] for item in bob_list["data"]["channels"]] == [
        "announcements"
    ]

    public_id = public["data"]["channel"]["id"]
    private_id = private["data"]["channel"]["id"]
    public_access = await api.handle(
        runtime.use("GET", "bob"),
        f"/api/chat/channels/{public_id}/room-access",
    )
    private_access = await api.handle(
        runtime.use("GET", "bob"),
        f"/api/chat/channels/{private_id}/room-access",
    )
    assert public_access["status"] == 200
    assert public_access["data"]["channel"]["visibility"] == "public"
    assert private_access["status"] == 404


@run_async_test
async def test_legacy_channel_without_visibility_remains_private():
    runtime = FakeRuntime()
    runtime.db.execute(
        "INSERT INTO chat_channels "
        "(channel_id,name_bi,data,created_by_bi,created_at,updated_at,key_version) "
        "VALUES (?,?,?,?,?,?,1)",
        (
            "f" * 32,
            "legacy-name-bi",
            json.dumps({"name": "legacy"}),
            "bi:admin",
            runtime.clock,
            runtime.clock,
        ),
    )
    runtime.db.commit()

    listed = await api.handle(
        runtime.use("GET", "bob"), "/api/chat/channels")
    access = await api.handle(
        runtime.use("GET", "bob"),
        f"/api/chat/channels/{'f' * 32}/room-access",
    )
    assert listed["data"]["channels"] == []
    assert access["status"] == 404


@run_async_test
async def test_public_channel_rejects_private_membership_management():
    runtime = FakeRuntime()
    created = await create_channel(
        runtime, "announcements", visibility="public")
    channel_id = created["data"]["channel"]["id"]
    path = f"/api/chat/channels/{channel_id}/members"

    for method, data in (
        ("GET", None),
        ("POST", {"username": "alice"}),
        ("DELETE", {"username": "alice"}),
    ):
        response = await api.handle(
            runtime.use(method, "admin", data), path)
        assert response["status"] == 409
        assert response["data"] == {"error": "members_not_allowed"}


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
    assert access["data"]["meetingWebSocketUrl"].startswith(
        f"/api/world/office/channels/{channel_id}/ws?ticket=")
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


async def read_history(runtime, actor, channel_id, since=None):
    return await api.handle(
        runtime.use(
            "GET", actor,
            query={} if since is None else {"since": str(since)}),
        f"/api/chat/channels/{channel_id}/history",
    )


@run_async_test
async def test_history_replays_encrypted_backlog_to_a_reader():
    runtime = FakeRuntime()
    created = await create_channel(
        runtime, "design", visibility="private", members=["alice"])
    channel_id = created["data"]["channel"]["id"]
    runtime.retained[f"chat-channel:{channel_id}:v1"] = [
        (100, '{"iv":"a","data":"first"}'),
        (200, '{"iv":"b","data":"second"}'),
    ]

    replayed = await read_history(runtime, "alice", channel_id)
    assert replayed["status"] == 200
    assert replayed["data"]["room"] == f"chat-channel:{channel_id}:v1"
    assert replayed["data"]["keyVersion"] == 1
    assert replayed["data"]["passphrase"] == f"key:{channel_id}:1"
    assert replayed["data"]["channel"]["name"] == "design"
    assert replayed["data"]["messages"] == [
        {"ts": 100, "body": '{"iv":"a","data":"first"}'},
        {"ts": 200, "body": '{"iv":"b","data":"second"}'},
    ]
    assert replayed["data"]["latestTs"] == 200
    assert replayed["cache_control"] == "no-store, max-age=0, must-revalidate"

    tail = await read_history(runtime, "alice", channel_id, since=100)
    assert [m["ts"] for m in tail["data"]["messages"]] == [200]
    assert tail["data"]["latestTs"] == 200
    caught_up = await read_history(runtime, "alice", channel_id, since=200)
    assert caught_up["data"]["messages"] == []
    assert caught_up["data"]["latestTs"] == 200


@run_async_test
async def test_history_is_gated_by_the_same_membership_as_room_access():
    runtime = FakeRuntime()
    created = await create_channel(
        runtime, "design", visibility="private", members=["alice"])
    channel_id = created["data"]["channel"]["id"]
    runtime.retained[f"chat-channel:{channel_id}:v1"] = [
        (100, '{"iv":"a","data":"first"}'),
    ]

    denied = await read_history(runtime, "bob", channel_id)
    assert denied["status"] == 404
    assert denied["data"] == {"error": "not_found"}
    assert (await read_history(runtime, "admin", channel_id))["status"] == 200
    unauthenticated = await read_history(runtime, "nobody", channel_id)
    assert unauthenticated["status"] == 401
    assert unauthenticated["data"] == {"error": "invalid_session"}
    missing = await read_history(runtime, "alice", "f" * 32)
    assert missing["status"] == 404


@run_async_test
async def test_public_channel_history_is_readable_without_membership():
    runtime = FakeRuntime()
    created = await create_channel(runtime, "lobby", visibility="public")
    channel_id = created["data"]["channel"]["id"]
    runtime.retained[f"chat-channel:{channel_id}:v1"] = [
        (100, '{"iv":"a","data":"first"}'),
    ]

    read = await read_history(runtime, "bob", channel_id)
    assert read["status"] == 200
    assert [m["ts"] for m in read["data"]["messages"]] == [100]


@run_async_test
async def test_history_rejects_writes_and_malformed_since():
    runtime = FakeRuntime()
    created = await create_channel(runtime, "lobby", visibility="public")
    channel_id = created["data"]["channel"]["id"]
    runtime.retained[f"chat-channel:{channel_id}:v1"] = [
        (100, '{"iv":"a","data":"first"}'),
    ]

    posted = await api.handle(
        runtime.use("POST", "admin", {}),
        f"/api/chat/channels/{channel_id}/history",
    )
    assert posted["status"] == 405
    assert posted["headers"]["allow"] == "GET"

    junk = await read_history(runtime, "bob", channel_id, since="tomorrow")
    assert junk["status"] == 200
    assert [m["ts"] for m in junk["data"]["messages"]] == [100]
