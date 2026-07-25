"""Persisted, private, repository/commit/run-scoped World workshops."""

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
    "forkmesh_world_workshops", SRC / "world_workshops.py")
workshops = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workshops)

NOW = 2_000_000_000_000
COMMIT = "a" * 40


def run_async_test(function):
    def wrapped(*args, **kwargs):
        return asyncio.run(function(*args, **kwargs))
    return wrapped


class FakeRuntime:
    def __init__(self):
        self.db = sqlite3.connect(":memory:")
        self.db.row_factory = sqlite3.Row
        self.db.executescript(
            (ROOT / "migrations" / "0055_world_workshops.sql")
            .read_text(encoding="utf-8"))
        self.request_method = "GET"
        self.request_data = {}
        self.request_query = {}
        self.actor = ""
        self.ids = 0
        self.audits = []
        self.users = {
            name: {"bi": "bi-" + name, "name": name}
            for name in ("alice", "bob", "mallory")
        }

    def use(self, method, actor="", data=None, query=None):
        self.request_method = method
        self.actor = actor
        self.request_data = {} if data is None else data
        self.request_query = {} if query is None else query
        return self

    def method(self):
        return self.request_method

    def now(self):
        return NOW

    def new_id(self):
        self.ids += 1
        return f"{self.ids:032x}"

    def query_params(self):
        return self.request_query

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
        return ((user or {}).get("bi", ""), user)

    async def account(self, name):
        user = self.users.get(str(name or "").lower())
        return ((user or {}).get("bi", ""), (user or {}).get("name", ""))

    async def repository_access(self, owner, repo, actor):
        return owner == "alice" and repo == "project" and bool(actor)

    async def blind(self, value):
        return "blind:" + base64.urlsafe_b64encode(
            str(value).encode()).decode()

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


def report():
    return {
        "nodes": [{
            "label": "Widget",
            "kind": "Model",
            "detail": "one use",
            "definitionPath": "src/models/widget.py",
            "useSites": [{"path": "src/api/widgets.py", "count": 2}],
        }],
        "edges": [{
            "from": "src/api/widgets.py",
            "to": "src/models/widget.py",
            "path": "src/api/widgets.py",
        }],
        "findings": ["Candidate relationship"],
        "recommendations": ["Verify with a maintainer"],
        "references": ["src/models/widget.py", "src/api/widgets.py"],
        "modelUseSites": [{
            "name": "Widget",
            "definitionPath": "src/models/widget.py",
            "useSites": [{"path": "src/api/widgets.py", "count": 2}],
        }],
    }


@run_async_test
async def test_workshop_results_are_scoped_shared_and_incremental():
    runtime = FakeRuntime()
    unauthenticated = await workshops.handle(
        runtime.use("GET"), "/api/world/workshops")
    assert unauthenticated["status"] == 401

    created = await workshops.handle(
        runtime.use("POST", "alice", {
            "repository": "alice/project",
            "commit": COMMIT,
            "runId": "run_1234567890abcdef",
            "workshopType": "Database-model analysis",
            "report": report(),
        }),
        "/api/world/workshops",
    )
    assert created["status"] == 201
    session = created["data"]["session"]
    session_id = session["id"]
    result_id = created["data"]["resultId"]
    assert len(session_id) == len(result_id) == 32
    assert session["repository"] == "alice/project"
    assert session["commit"] == COMMIT
    assert session["runId"] == "run_1234567890abcdef"
    assert session["results"][0]["id"] == result_id
    assert session["results"][0]["report"]["modelUseSites"][0][
        "useSites"][0]["path"] == "src/api/widgets.py"

    scoped = await workshops.handle(
        runtime.use("GET", "alice", query={
            "repository": "alice/project", "commit": COMMIT}),
        "/api/world/workshops",
    )
    assert [item["id"] for item in scoped["data"]["sessions"]] == [session_id]

    hidden = await workshops.handle(
        runtime.use("GET", "mallory"),
        f"/api/world/workshops/{session_id}",
    )
    assert hidden["status"] == 404

    shared = await workshops.handle(
        runtime.use("POST", "alice", {
            "account": "bob", "role": "editor"}),
        f"/api/world/workshops/{session_id}/participants",
    )
    assert shared["status"] == 201
    bob_view = await workshops.handle(
        runtime.use("GET", "bob"),
        f"/api/world/workshops/{session_id}",
    )
    assert bob_view["status"] == 200
    assert bob_view["data"]["session"]["viewerRole"] == "editor"

    saved = await workshops.handle(
        runtime.use("POST", "bob", {"report": report()}),
        f"/api/world/workshops/{session_id}/results",
    )
    assert saved["status"] == 201
    assert saved["data"]["resultId"] != result_id

    comment = await workshops.handle(
        runtime.use("POST", "bob", {
            "kind": "comment", "message": "Please verify the Widget mapping."}),
        f"/api/world/workshops/{session_id}/events",
    )
    assert comment["status"] == 201
    events = await workshops.handle(
        runtime.use("GET", "alice", query={"after": "0"}),
        f"/api/world/workshops/{session_id}/events",
    )
    assert events["data"]["cursor"] > 0
    assert any(
        event["data"].get("message") == "Please verify the Widget mapping."
        for event in events["data"]["events"])
    no_replay = await workshops.handle(
        runtime.use("GET", "alice", query={
            "after": str(events["data"]["cursor"])}),
        f"/api/world/workshops/{session_id}/events",
    )
    assert no_replay["data"]["events"] == []


@run_async_test
async def test_workshop_storage_encrypts_names_paths_participants_and_comments():
    runtime = FakeRuntime()
    created = await workshops.handle(
        runtime.use("POST", "alice", {
            "repository": "alice/project",
            "commit": COMMIT,
            "runId": "run_private_12345678",
            "workshopType": "Database-model analysis",
            "report": report(),
        }),
        "/api/world/workshops",
    )
    session_id = created["data"]["session"]["id"]
    await workshops.handle(
        runtime.use("POST", "alice", {
            "account": "bob", "role": "viewer"}),
        f"/api/world/workshops/{session_id}/participants",
    )
    await workshops.handle(
        runtime.use("POST", "bob", {
            "kind": "comment", "message": "private collaboration note"}),
        f"/api/world/workshops/{session_id}/events",
    )
    dump = "\n".join(runtime.db.iterdump())
    for private_value in (
        "alice/project",
        "src/models/widget.py",
        "src/api/widgets.py",
        "private collaboration note",
        "'alice'",
        "'bob'",
    ):
        assert private_value not in dump
    assert "blind:" in dump
    assert "sealed:" in dump


@run_async_test
async def test_workshop_collision_does_not_disclose_an_unshared_session_id():
    runtime = FakeRuntime()
    body = {
        "repository": "alice/project",
        "commit": COMMIT,
        "runId": "run_collision_123456",
        "workshopType": "Architecture analysis",
        "report": report(),
    }
    created = await workshops.handle(
        runtime.use("POST", "alice", body),
        "/api/world/workshops",
    )
    session_id = created["data"]["session"]["id"]

    unshared = await workshops.handle(
        runtime.use("POST", "mallory", body),
        "/api/world/workshops",
    )
    assert unshared["status"] == 409
    assert unshared["data"] == {"error": "run_exists"}

    owner_retry = await workshops.handle(
        runtime.use("POST", "alice", body),
        "/api/world/workshops",
    )
    assert owner_retry["data"]["sessionId"] == session_id


def test_workshop_report_normalization_rejects_collection_type_confusion():
    normalized = workshops.normalize_report({
        "nodes": {"label": "not-a-list"},
        "edges": "not-a-list",
        "modelUseSites": {"name": "not-a-list"},
        "findings": {"also": "not-a-list"},
    })
    assert normalized["nodes"] == []
    assert normalized["edges"] == []
    assert normalized["modelUseSites"] == []
    assert normalized["findings"] == []


def test_workshop_schema_and_worker_route_are_wired():
    migration = (
        ROOT / "migrations" / "0055_world_workshops.sql"
    ).read_text(encoding="utf-8")
    schema = (SRC / "schema.py").read_text(encoding="utf-8")
    entry = (SRC / "entry.py").read_text(encoding="utf-8")
    for table in (
        "world_workshop_sessions",
        "world_workshop_participants",
        "world_workshop_results",
        "world_workshop_events",
    ):
        assert table in migration
        assert table in schema
    assert "import world_workshops" in entry
    assert "world_workshops_handler" in entry
    assert 'url.path.startswith("/api/world/workshops/")' in entry
    assert "encrypt_row" in entry
    assert "blind_index" in entry
