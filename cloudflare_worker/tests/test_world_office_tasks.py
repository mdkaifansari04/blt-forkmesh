"""Organization-scoped, encrypted Office marketing task persistence."""

import asyncio
import base64
import importlib.util
import hashlib
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
        self.db.executescript(
            (ROOT / "migrations" / "0096_world_office_marketing_proofs.sql")
            .read_text(encoding="utf-8"))
        self.db.executescript(
            (ROOT / "migrations" / "0101_world_office_marketing_initiatives.sql")
            .read_text(encoding="utf-8"))
        self.db.executescript(
            (ROOT / "migrations" / "0105_organization_tasks.sql")
            .read_text(encoding="utf-8"))
        self.request_method = "GET"
        self.request_data = {}
        self.query_data = {}
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
        self.teams = {
            "engineering": {"alice", "bob"},
            "marketing": {"alice", "bob", "carol"},
            "quality-assurance": {"alice", "carol"},
        }

    def use(self, method, actor="", data=None, same_origin=True, query=None):
        self.request_method = method
        self.actor = actor
        self.request_data = {} if data is None else data
        self.same_origin_request = same_origin
        self.query_data = dict(query or {})
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

    def query(self, name):
        return self.query_data.get(name, "")

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

    async def organization_members(self, _org_bi):
        return [
            {"bi": self.users[name]["bi"], "name": name}
            for name in sorted(self.memberships)
            if self.users.get(name, {}).get("active")
        ]

    async def organization_member(self, org_bi, name):
        return next(
            (
                member
                for member in await self.organization_members(org_bi)
                if member["name"] == name
            ),
            None,
        )

    async def organization_teams(self, _org_bi):
        return [
            {"team": team, "permission": "read", "members": len(members)}
            for team, members in sorted(self.teams.items())
        ]

    async def team_member(self, _org_bi, team, name):
        return name in self.teams.get(team, set())

    async def marketing_members(self, _org_bi):
        return [
            {"bi": self.users[name]["bi"], "name": name}
            for name in ("bob", "carol")
        ]

    async def marketing_member(self, org_bi, name):
        return next(
            (
                member
                for member in await self.marketing_members(org_bi)
                if member["name"] == name
            ),
            None,
        )

    async def marketing_attendance(self, members, _now):
        return [{
            "date": "2036-07-18",
            "label": "FRI 07/18",
            "hours": [
                {"member": member["name"], "hours": 1.25}
                for member in members
            ],
        }]

    async def seal(self, value):
        return "sealed:" + base64.urlsafe_b64encode(
            json.dumps(value, sort_keys=True).encode()).decode()

    async def open(self, value):
        if not str(value or "").startswith("sealed:"):
            raise ValueError("not sealed")
        return json.loads(base64.urlsafe_b64decode(
            str(value)[7:].encode()).decode())

    async def blind(self, value):
        return hashlib.sha256(
            ("test-blind:" + str(value)).encode()
        ).hexdigest()

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
async def test_universal_tasks_are_org_private_routable_and_marketing_compatible():
    runtime = FakeRuntime()
    generic = await tasks_api.handle(
        runtime.use("POST", "bob", {
            "title": "Fix the engineering HUD",
            "details": "Keep this inside the organization.",
            "department": "engineering",
            "team": "engineering",
            "destination": "department",
            "assigneeKind": "user",
            "assignee": "bob",
        }),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert generic["status"] == 201
    task = generic["data"]["task"]
    assert task["department"] == "engineering"
    assert task["team"] == "engineering"
    assert task["assigneeKind"] == "user"
    assert task["qa"]["status"] == "unknown"

    org_view = await tasks_api.handle(
        runtime.use("GET", "carol"),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert [item["id"] for item in org_view["data"]["tasks"]] == [task["id"]]
    assert org_view["data"]["privacyBoundary"] == (
        "organization-private-encrypted-at-rest"
    )
    filtered = await tasks_api.handle(
        runtime.use(
            "GET", "alice", query={"department": "engineering"}),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert [item["id"] for item in filtered["data"]["tasks"]] == [task["id"]]

    legacy_view = await tasks_api.handle(
        runtime.use("GET", "alice"),
        tasks_api.LEGACY_MARKETING_PREFIX,
    )
    assert legacy_view["data"]["tasks"] == []
    outsider = await tasks_api.handle(
        runtime.use("GET", "eve"),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert outsider["status"] == 403
    assert outsider["data"]["error"] == "org_member_required"


@run_async_test
async def test_universal_tasks_route_to_agents_and_private_qa():
    runtime = FakeRuntime()
    denied_assignment = await tasks_api.handle(
        runtime.use("POST", "bob", {
            "title": "Assign somebody else",
            "department": "engineering",
            "assigneeKind": "user",
            "assignee": "carol",
        }),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert denied_assignment["status"] == 403
    assert denied_assignment["data"]["error"] == "cannot_assign_other_member"

    missing_repo = await tasks_api.handle(
        runtime.use("POST", "mary", {
            "title": "Run an agent",
            "department": "engineering",
            "assigneeKind": "codex",
        }),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert missing_repo["status"] == 400
    assert missing_repo["data"]["error"] == "repository_required"

    agent = await tasks_api.handle(
        runtime.use("POST", "mary", {
            "title": "Run the private verification",
            "details": "Agent task details",
            "department": "engineering",
            "destination": "repository",
            "repository": "forkmesh/forkmesh",
            "assigneeKind": "codex",
            "sendToQa": True,
            "howToTest": "Open the World and verify the result.",
        }),
        tasks_api.UNIVERSAL_PREFIX,
    )
    assert agent["status"] == 201
    task = agent["data"]["task"]
    assert task["destination"] == "agent"
    assert task["assignee"] == "codex"
    assert task["qa"]["requestedAt"] == runtime.now_ms
    assert task["repository"] == "forkmesh/forkmesh"
    stored = runtime.db.execute(
        "SELECT data FROM organization_tasks WHERE task_id=?",
        (task["id"],),
    ).fetchone()[0]
    assert "Run the private verification" not in stored
    assert "Agent task details" not in stored


@run_async_test
async def test_org_authorization_manager_roles_and_filtered_reads():
    runtime = FakeRuntime()

    unauthenticated = await tasks_api.handle(
        runtime.use("GET"), tasks_api.PREFIX)
    assert unauthenticated["status"] == 401

    # Platform status is deliberately not part of the authorization adapter.
    # A platform administrator outside the organization cannot infer tasks.
    outsider = await tasks_api.handle(
        runtime.use("GET", "rootadmin"), tasks_api.PREFIX)
    assert outsider["status"] == 403
    assert outsider["data"]["error"] == "org_member_required"
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
    assert manager_list["data"]["members"] == ["bob", "carol"]
    # Managers can assign only to Marketing members, but private Marketing
    # roster/attendance/proof data stays hidden unless the actor is on Marketing.
    assert manager_list["data"]["marketingMembers"] == []
    assert manager_list["data"]["attendanceDays"] == []
    assert manager_list["data"]["proofs"] == []

    bob_list = await tasks_api.handle(
        runtime.use("GET", "bob"), tasks_api.PREFIX)
    assert bob_list["status"] == 200
    assert bob_list["data"]["actor"] == "bob"
    assert bob_list["data"]["canManage"] is False
    assert "members" not in bob_list["data"]
    assert [task["assignee"] for task in bob_list["data"]["tasks"]] == ["bob"]
    assert bob_list["data"]["marketingMembers"] == ["bob", "carol"]
    assert bob_list["data"]["attendanceDays"][0]["hours"][0] == {
        "member": "bob",
        "hours": 1.25,
    }

    inactive_assignee = await create_task(runtime, "inactive")
    assert inactive_assignee["status"] == 400
    assert inactive_assignee["data"]["error"] == (
        "assignee_not_marketing_member"
    )

    external_task = await create_task(runtime, "eve", "External assignment")
    assert external_task["status"] == 400
    assert external_task["data"]["error"] == "assignee_not_marketing_member"
    external_view = await tasks_api.handle(
        runtime.use("GET", "eve"), tasks_api.PREFIX)
    assert external_view["status"] == 403
    assert external_view["data"]["error"] == "org_member_required"
    external_manage = await tasks_api.handle(
        runtime.use("POST", "eve", {
            "title": "No management bypass",
            "assignee": "eve",
        }),
        tasks_api.PREFIX,
    )
    assert external_manage["status"] == 403


@run_async_test
async def test_marketing_proofs_are_encrypted_and_marketing_team_only():
    runtime = FakeRuntime()
    url = "https://social.example/posts/forkmesh-launch"
    label = "Launch announcement"
    denied = await tasks_api.handle(
        runtime.use("POST", "mary", {"url": url, "label": label}),
        f"{tasks_api.PREFIX}/proofs",
    )
    assert denied["status"] == 403
    assert denied["data"]["error"] == "marketing_team_only"

    insecure = await tasks_api.handle(
        runtime.use("POST", "bob", {"url": "http://example.com/post"}),
        f"{tasks_api.PREFIX}/proofs",
    )
    assert insecure["status"] == 400
    assert insecure["data"]["error"] == "invalid_social_url"

    created = await tasks_api.handle(
        runtime.use("POST", "bob", {"url": url, "label": label}),
        f"{tasks_api.PREFIX}/proofs",
    )
    assert created["status"] == 201
    assert created["data"]["proof"]["member"] == "bob"
    assert created["data"]["proof"]["url"] == url
    assert created["data"]["proof"]["label"] == label
    stored = runtime.db.execute(
        "SELECT url_bi,data FROM world_office_marketing_proofs"
    ).fetchone()
    assert url not in stored[0]
    assert url not in stored[1]
    assert label not in stored[1]
    assert url not in json.dumps(runtime.audits)
    assert "social.example" not in json.dumps(runtime.audits)

    marketing_view = await tasks_api.handle(
        runtime.use("GET", "carol"),
        f"{tasks_api.PREFIX}/proofs",
    )
    assert marketing_view["status"] == 200
    assert marketing_view["data"]["proofs"][0]["url"] == url
    outside_view = await tasks_api.handle(
        runtime.use("GET", "eve"),
        f"{tasks_api.PREFIX}/proofs",
    )
    assert outside_view["status"] == 403


@run_async_test
async def test_marketing_initiatives_are_encrypted_deduplicated_and_team_private():
    runtime = FakeRuntime()
    issue = {
        "owner": "forkmesh",
        "repo": "forkmesh",
        "number": 541,
        "title": "Update globe navigation",
    }

    denied = await tasks_api.handle(
        runtime.use("POST", "wendy", issue),
        f"{tasks_api.PREFIX}/initiatives",
    )
    assert denied["status"] == 403
    assert denied["data"]["error"] == "marketing_team_only"

    created = await tasks_api.handle(
        runtime.use("POST", "alice", issue),
        f"{tasks_api.PREFIX}/initiatives",
    )
    assert created["status"] == 201
    initiative = created["data"]["initiative"]
    assert initiative["repo"] == "forkmesh/forkmesh"
    assert initiative["number"] == 541
    assert initiative["href"] == "/forkmesh/forkmesh/issues/541"

    duplicate = await tasks_api.handle(
        runtime.use("POST", "bob", issue),
        f"{tasks_api.PREFIX}/initiatives",
    )
    assert duplicate["status"] == 200
    assert duplicate["data"]["existing"] is True
    assert duplicate["data"]["initiative"]["id"] == initiative["id"]

    stored = runtime.db.execute(
        "SELECT source_bi,data FROM world_office_marketing_initiatives"
    ).fetchone()
    assert "forkmesh" not in stored[0]
    assert issue["title"] not in stored[1]
    assert "forkmesh" not in json.dumps(runtime.audits)
    assert issue["title"] not in json.dumps(runtime.audits)

    marketing_view = await tasks_api.handle(
        runtime.use("GET", "carol"),
        f"{tasks_api.PREFIX}/initiatives",
    )
    assert marketing_view["status"] == 200
    assert marketing_view["data"]["initiatives"][0]["number"] == 541
    outside_view = await tasks_api.handle(
        runtime.use("GET", "wendy"),
        f"{tasks_api.PREFIX}/initiatives",
    )
    assert outside_view["status"] == 403

    manager_board = await tasks_api.handle(
        runtime.use("GET", "alice"),
        tasks_api.PREFIX,
    )
    assert manager_board["status"] == 200
    assert manager_board["data"]["initiatives"] == []
    marketing_board = await tasks_api.handle(
        runtime.use("GET", "bob"),
        tasks_api.PREFIX,
    )
    assert marketing_board["data"]["initiatives"][0]["title"] == issue["title"]


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
        "FROM organization_tasks WHERE task_id=?",
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
async def test_assignee_or_manager_can_complete_and_only_manager_can_delete():
    runtime = FakeRuntime()
    created = await create_task(runtime, "bob", "Finish and retain")
    task_id = created["data"]["task"]["id"]
    started = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/start",
    )
    assert started["status"] == 200
    runtime.now_ms += 7_500

    outsider_done = await tasks_api.handle(
        runtime.use("POST", "carol", {}),
        f"{tasks_api.PREFIX}/{task_id}/complete",
    )
    assert outsider_done["status"] == 403
    completed = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/complete",
    )
    assert completed["status"] == 200
    assert completed["data"]["task"]["status"] == "done"
    assert completed["data"]["task"]["elapsedMs"] == 7_500
    assert completed["data"]["task"]["completedAt"] == runtime.now_ms

    restart = await tasks_api.handle(
        runtime.use("POST", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}/start",
    )
    assert restart["status"] == 409
    assert restart["data"]["error"] == "task_completed"

    member_delete = await tasks_api.handle(
        runtime.use("DELETE", "bob", {}),
        f"{tasks_api.PREFIX}/{task_id}",
    )
    assert member_delete["status"] == 403
    deleted = await tasks_api.handle(
        runtime.use("DELETE", "mary", {}),
        f"{tasks_api.PREFIX}/{task_id}",
    )
    assert deleted["status"] == 200
    assert deleted["data"]["deleted"] is True
    assert await runtime.d1_first(
        "SELECT task_id FROM organization_tasks WHERE task_id=?",
        task_id,
    ) is None
    assert {
        item["action"] for item in runtime.audits
    }.issuperset({
        "office.marketing_task_completed",
        "office.marketing_task_deleted",
    })


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
        "SELECT COUNT(*) FROM organization_tasks"
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
        "SELECT data FROM organization_tasks"
    ).fetchone()[0]
    checkin_storage = runtime.db.execute(
        "SELECT data FROM organization_task_checkins"
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
