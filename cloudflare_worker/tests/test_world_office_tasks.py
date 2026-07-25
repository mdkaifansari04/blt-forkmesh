"""Organization-scoped, encrypted Office marketing task persistence."""

import asyncio
import base64
import importlib.util
import json
from pathlib import Path
import sqlite3
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
sys.path.insert(0, str(SRC))

spec = importlib.util.spec_from_file_location(
    "forkmesh_world_office_tasks", SRC / "world_office_tasks.py")
tasks_api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tasks_api)

NOW = 2_100_000_000_000


def run_async_test(function):
    def wrapped(*args, **kwargs):
        return asyncio.run(function(*args, **kwargs))
    return wrapped


class FakeRuntime:
    def __init__(self):
        self.db = sqlite3.connect(":memory:")
        self.db.row_factory = sqlite3.Row
        self.db.executescript(
            (ROOT / "migrations" / "0075_world_office_marketing_tasks.sql")
            .read_text(encoding="utf-8"))
        self.request_method = "GET"
        self.request_data = {}
        self.actor = ""
        self.same_origin_request = True
        self.now_ms = NOW
        self.ids = 0
        self.audits = []
        self.organization_exists = True
        self.users = {
            name: {"bi": "bi-" + name, "name": name, "active": True}
            for name in (
                "alice",
                "mary",
                "wendy",
                "bob",
                "carol",
                "eve",
                "rootadmin",
                "inactive",
            )
        }
        self.users["inactive"]["active"] = False
        self.users["rootadmin"]["is_admin"] = True
        self.memberships = {
            "alice": ("owner", "admin"),
            "mary": ("member", "maintain"),
            "wendy": ("member", "write"),
            "bob": ("member", "read"),
            "carol": ("member", "read"),
            "inactive": ("member", "read"),
        }

    def use(self, method, actor="", data=None, same_origin=True):
        self.request_method = method
        self.actor = actor
        self.request_data = {} if data is None else data
        self.same_origin_request = same_origin
        return self

    def method(self):
        return self.request_method

    def now(self):
        return self.now_ms

    def new_id(self):
        self.ids += 1
        return f"{self.ids:032x}"

    def same_origin(self):
        return self.same_origin_request

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
        return (
            (self.request_data, "")
            if isinstance(self.request_data, dict)
            else (None, "invalid_json")
        )

    async def session(self, _data):
        user = self.users.get(self.actor)
        if not user or not user["active"]:
            return "", None
        return user["bi"], {"name": user["name"]}

    async def organization(self):
        if not self.organization_exists:
            return "org-bi", None
        return "org-bi", {"name": "forkmesh"}

    async def membership(self, _org_bi, name):
        return self.memberships.get(name, ("", ""))

    async def active_user(self, name):
        user = self.users.get(name)
        if not user or not user["active"]:
            return None
        return {"bi": user["bi"], "name": name}

    async def eligible_users(self):
        return sorted(
            name for name, user in self.users.items() if user["active"]
        )

    async def seal(self, value):
        return "sealed:" + base64.urlsafe_b64encode(
            json.dumps(value, sort_keys=True).encode()).decode()

    async def open(self, value):
        if not str(value or "").startswith("sealed:"):
            raise ValueError("not sealed")
        return json.loads(base64.urlsafe_b64decode(
            str(value)[7:].encode()).decode())

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

    async def d1_all(self, sql, *args):
        return [
            dict(row) for row in self.db.execute(sql, args).fetchall()
        ]

    async def d1_first(self, sql, *args):
        row = self.db.execute(sql, args).fetchone()
        return dict(row) if row is not None else None

    async def d1_run(self, sql, *args):
        self.db.execute(sql, args)
        self.db.commit()


async def create_task(runtime, assignee="bob", title="Write launch post",
                      details="Coordinate the release digest"):
    return await tasks_api.handle(
        runtime.use("POST", "alice", {
            "title": title,
            "details": details,
            "assignee": assignee,
        }),
        tasks_api.PREFIX,
    )


@run_async_test
async def test_org_authorization_manager_roles_and_filtered_reads():
    runtime = FakeRuntime()

    unauthenticated = await tasks_api.handle(
        runtime.use("GET"), tasks_api.PREFIX)
    assert unauthenticated["status"] == 401

    # Platform status is deliberately not part of the authorization adapter.
    # An outside user gets only their own (currently empty) assignment view.
    outsider = await tasks_api.handle(
        runtime.use("GET", "rootadmin"), tasks_api.PREFIX)
    assert outsider["status"] == 200
    assert outsider["data"]["canManage"] is False
    assert outsider["data"]["tasks"] == []
    assert "members" not in outsider["data"]
    admin_bypass = await tasks_api.handle(
        runtime.use("POST", "rootadmin", {
            "title": "Platform admin is not an org manager",
            "assignee": "rootadmin",
        }),
        tasks_api.PREFIX,
    )
    assert admin_bypass["status"] == 403

    write_only = await tasks_api.handle(
        runtime.use("POST", "wendy", {
            "title": "Not allowed",
            "assignee": "wendy",
        }),
        tasks_api.PREFIX,
    )
    assert write_only["status"] == 403

    bob_task = await create_task(runtime, "bob", "Bob launch task")
    assert bob_task["status"] == 201
    carol_task = await tasks_api.handle(
        runtime.use("POST", "mary", {
            "title": "Carol community task",
            "assignee": "carol",
        }),
        tasks_api.PREFIX,
    )
    assert carol_task["status"] == 201

    manager_list = await tasks_api.handle(
        runtime.use("GET", "mary"), tasks_api.PREFIX)
    assert manager_list["status"] == 200
    assert manager_list["cache_control"].startswith("no-store")
    assert manager_list["data"]["actor"] == "mary"
    assert manager_list["data"]["canManage"] is True
    assert manager_list["data"]["serverNow"] == NOW
    assert {task["assignee"] for task in manager_list["data"]["tasks"]} == {
        "bob", "carol",
    }
    assert "members" in manager_list["data"]
    assert "inactive" not in manager_list["data"]["members"]
    assert "eve" in manager_list["data"]["members"]

    bob_list = await tasks_api.handle(
        runtime.use("GET", "bob"), tasks_api.PREFIX)
    assert bob_list["status"] == 200
    assert bob_list["data"]["actor"] == "bob"
    assert bob_list["data"]["canManage"] is False
    assert "members" not in bob_list["data"]
    assert [task["assignee"] for task in bob_list["data"]["tasks"]] == ["bob"]

    inactive_assignee = await create_task(runtime, "inactive")
    assert inactive_assignee["status"] == 400
    assert inactive_assignee["data"]["error"] == "assignee_not_active_user"

    external_task = await create_task(runtime, "eve", "External assignment")
    assert external_task["status"] == 201
    external_view = await tasks_api.handle(
        runtime.use("GET", "eve"), tasks_api.PREFIX)
    assert external_view["status"] == 200
    assert [task["assignee"] for task in external_view["data"]["tasks"]] == [
        "eve"
    ]
    external_id = external_task["data"]["task"]["id"]
    external_start = await tasks_api.handle(
        runtime.use("POST", "eve", {}),
        f"{tasks_api.PREFIX}/{external_id}/start",
    )
    assert external_start["status"] == 200
    external_checkin = await tasks_api.handle(
        runtime.use("POST", "eve", {"state": "going_well"}),
        f"{tasks_api.PREFIX}/{external_id}/checkin",
    )
    assert external_checkin["status"] == 200
    external_stop = await tasks_api.handle(
        runtime.use("POST", "eve", {}),
        f"{tasks_api.PREFIX}/stop-active",
    )
    assert external_stop["data"]["stopped"] is True
    external_manage = await tasks_api.handle(
        runtime.use("POST", "eve", {
            "title": "No management bypass",
            "assignee": "eve",
        }),
        tasks_api.PREFIX,
    )
    assert external_manage["status"] == 403


@run_async_test
async def test_assignee_only_server_timer_checkins_and_single_active_task():
    runtime = FakeRuntime()
    first = await create_task(runtime, "bob", "First task")
    first_id = first["data"]["task"]["id"]
    second = await create_task(runtime, "bob", "Second task")
    second_id = second["data"]["task"]["id"]

    manager_cannot_start = await tasks_api.handle(
        runtime.use("POST", "alice", {}),
        f"{tasks_api.PREFIX}/{first_id}/start",
    )
    assert manager_cannot_start["status"] == 403
    assert manager_cannot_start["data"]["error"] == "assignee_only"

    started = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{first_id}/start",
    )
    assert started["status"] == 200
    started_task = started["data"]["task"]
    assert started_task["status"] == "active"
    assert started_task["startedAt"] == NOW
    assert (
        NOW + tasks_api.CHECKIN_MIN_MS
        <= started_task["nextCheckinAt"]
        <= NOW + tasks_api.CHECKIN_MAX_MS
    )

    duplicate_start = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{second_id}/start",
    )
    assert duplicate_start["status"] == 409
    assert duplicate_start["data"]["error"] == "active_task_exists"

    runtime.now_ms += 5000
    running = await tasks_api.handle(
        runtime.use("GET", "bob"),
        f"{tasks_api.PREFIX}/{first_id}",
    )
    assert running["data"]["task"]["elapsedMs"] == 5000

    invalid_checkin = await tasks_api.handle(
        runtime.use("POST", "bob", {"state": "fine"}),
        f"{tasks_api.PREFIX}/{first_id}/checkin",
    )
    assert invalid_checkin["status"] == 400

    checked_in = await tasks_api.handle(
        runtime.use("POST", "bob", {
            "state": "needs_help",
            "note": "Waiting for review",
        }),
        f"{tasks_api.PREFIX}/{first_id}/checkin",
    )
    checked_task = checked_in["data"]["task"]
    assert checked_task["lastCheckin"] == {
        "state": "needs_help",
        "note": "Waiting for review",
        "at": runtime.now_ms,
    }
    assert (
        runtime.now_ms + tasks_api.CHECKIN_MIN_MS
        <= checked_task["nextCheckinAt"]
        <= runtime.now_ms + tasks_api.CHECKIN_MAX_MS
    )

    other_member = await tasks_api.handle(
        runtime.use("POST", "carol", {"state": "going_well"}),
        f"{tasks_api.PREFIX}/{first_id}/checkin",
    )
    assert other_member["status"] == 403

    runtime.now_ms += 4000
    stopped = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{first_id}/stop",
    )
    stopped_task = stopped["data"]["task"]
    assert stopped_task["status"] == "idle"
    assert stopped_task["elapsedMs"] == 9000
    assert stopped_task["startedAt"] == 0
    assert stopped_task["nextCheckinAt"] == 0

    stopped_checkin = await tasks_api.handle(
        runtime.use("POST", "bob", {"state": "going_well"}),
        f"{tasks_api.PREFIX}/{first_id}/checkin",
    )
    assert stopped_checkin["status"] == 409

    now_allowed = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{second_id}/start",
    )
    assert now_allowed["status"] == 200


@run_async_test
async def test_world_exit_stop_active_is_server_timed_idempotent_and_private():
    runtime = FakeRuntime()
    created = await create_task(runtime, "bob", "Stop on World exit")
    task_id = created["data"]["task"]["id"]
    started = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/start",
    )
    assert started["status"] == 200
    runtime.now_ms += 12_345

    cross_origin = await tasks_api.handle(
        runtime.use("POST", "bob", {}, same_origin=False),
        f"{tasks_api.PREFIX}/stop-active",
    )
    assert cross_origin["status"] == 403

    stopped = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/stop-active",
    )
    assert stopped == {
        "status": 200,
        "data": {
            "ok": True,
            "stopped": True,
            "serverNow": runtime.now_ms,
        },
        "cache_control": "no-store, max-age=0, must-revalidate",
        "headers": {"x-content-type-options": "nosniff"},
    }
    row = runtime.db.execute(
        "SELECT status,elapsed_ms,started_at,next_checkin_at "
        "FROM world_office_marketing_tasks WHERE task_id=?",
        (task_id,),
    ).fetchone()
    assert tuple(row) == ("idle", 12_345, 0, 0)

    repeated = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/stop-active",
    )
    assert repeated["status"] == 200
    assert repeated["data"]["stopped"] is False
    assert "task" not in repeated["data"]

    unauthenticated = await tasks_api.handle(
        runtime.use("POST", "", {}),
        f"{tasks_api.PREFIX}/stop-active",
    )
    assert unauthenticated["status"] == 401


@run_async_test
async def test_encrypted_copy_same_origin_reassignment_and_metadata_only_audit():
    runtime = FakeRuntime()
    denied_origin = await tasks_api.handle(
        runtime.use(
            "POST",
            "alice",
            {"title": "No cross-site writes", "assignee": "bob"},
            same_origin=False,
        ),
        tasks_api.PREFIX,
    )
    assert denied_origin["status"] == 403
    assert runtime.db.execute(
        "SELECT COUNT(*) FROM world_office_marketing_tasks"
    ).fetchone()[0] == 0

    title = "Distinctive confidential launch phrase"
    details = "Partner sequence stays inside the encrypted task row"
    created = await create_task(runtime, "bob", title, details)
    task_id = created["data"]["task"]["id"]

    started = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/start",
    )
    assert started["status"] == 200
    active_reassign = await tasks_api.handle(
        runtime.use("PATCH", "alice", {"assignee": "carol"}),
        f"{tasks_api.PREFIX}/{task_id}",
    )
    assert active_reassign["status"] == 409

    note = "Blocked on a private campaign dependency"
    checkin = await tasks_api.handle(
        runtime.use("POST", "bob", {
            "state": "blocked",
            "note": note,
        }),
        f"{tasks_api.PREFIX}/{task_id}/checkin",
    )
    assert checkin["status"] == 200

    task_storage = runtime.db.execute(
        "SELECT data FROM world_office_marketing_tasks"
    ).fetchone()[0]
    checkin_storage = runtime.db.execute(
        "SELECT data FROM world_office_marketing_checkins"
    ).fetchone()[0]
    assert title not in task_storage
    assert details not in task_storage
    assert note not in checkin_storage

    audit_dump = json.dumps(runtime.audits, sort_keys=True)
    assert title not in audit_dump
    assert details not in audit_dump
    assert note not in audit_dump
    assert all(
        set(item["details"]).issubset({
            "assigned", "fields", "state", "source"})
        for item in runtime.audits
    )

    await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/stop",
    )
    reassigned = await tasks_api.handle(
        runtime.use("PATCH", "mary", {
            "assignee": "carol",
            "details": "New bounded details",
        }),
        f"{tasks_api.PREFIX}/{task_id}",
    )
    assert reassigned["status"] == 200
    assert reassigned["data"]["task"]["assignee"] == "carol"

    member_update = await tasks_api.handle(
        runtime.use("PATCH", "bob", {"title": "Not allowed"}),
        f"{tasks_api.PREFIX}/{task_id}",
    )
    assert member_update["status"] == 403


def test_migration_has_conditional_active_constraint_and_bounded_tables():
    migration = (
        ROOT / "migrations" / "0075_world_office_marketing_tasks.sql"
    ).read_text(encoding="utf-8")
    assert "CREATE UNIQUE INDEX" in migration
    assert "WHERE status = 'active' AND active_assignee_bi <> ''" in migration
    assert "trg_world_office_marketing_task_limit" in migration
    assert "trg_world_office_marketing_checkin_limit" in migration


def test_entry_wiring_is_http_only_and_has_no_admin_bypass():
    entry = (SRC / "entry.py").read_text(encoding="utf-8")
    module = (SRC / "world_office_tasks.py").read_text(encoding="utf-8")
    assert "import world_office_tasks" in entry
    assert '"/api/world/office/marketing-tasks"' in entry
    assert "_OfficeMarketingTasksRuntime" in entry
    assert "OFFICE_MARKETING_ORG" in entry
    assert "is_admin" not in module
    assert "DurableObject" not in module
    assert "WebSocket" not in module
