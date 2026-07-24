"""Worker authorization and signed mirror-gateway pull merge contracts."""

import ast
import asyncio
import hashlib
import hmac
import json
from pathlib import Path
import re
import sqlite3
import sys
import threading
import time

import pytest


ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / "cloudflare_worker"
ENTRY = WORKER / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(WORKER / "src"))

import edge_routing  # noqa: E402
import mirror_gateway as gateway  # noqa: E402
import urls  # noqa: E402


OID_A = "a" * 40
OID_B = "b" * 40
OID_C = "c" * 40


def _entry_functions(*names):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    nodes = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in nodes} == set(names)
    namespace = {
        "HTTPS_MIRROR_MERGE_BODY_MAX_BYTES": 8 * 1024,
        "HTTPS_MIRROR_MERGE_REQUEST_RE": re.compile(
            r"^[A-Za-z0-9_-]{12,80}$"),
        "HTTPS_MIRROR_MERGE_JOB_REPO_LIMIT": 256,
        "HTTPS_MIRROR_MERGE_JOB_GLOBAL_LIMIT": 10_000,
        "HTTPS_MIRROR_MERGE_JOB_CLEANUP_BATCH": 512,
        "clean_string": lambda value, maximum: str(value or "")[:maximum],
        "json": json,
        "re": re,
        "hmac": hmac,
    }
    exec(compile(
        ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[])),
        str(ENTRY),
        "exec",
    ), namespace)
    return namespace


def merge_body(**changes):
    value = {
        "schemaVersion": 1,
        "type": "forkmesh.pull-merge-v1",
        "pullNumber": 7,
        "requestId": "merge_request_0001",
        "expectedBaseOid": OID_A,
        "expectedHeadOid": OID_B,
        "expectedPullsOid": OID_C,
    }
    value.update(changes)
    return json.dumps(value, separators=(",", ":")).encode()


def test_gateway_parser_binds_exact_route_number_and_oids():
    parsed = gateway.GatewayApplication._merge_request(
        "mirror2", "forkmesh", merge_body())
    assert parsed == {
        "schemaVersion": 1,
        "type": "forkmesh.pull-merge-executor-v1",
        "owner": "mirror2",
        "repository": "forkmesh",
        "pullNumber": 7,
        "requestId": "merge_request_0001",
        "expectedBaseOid": OID_A,
        "expectedHeadOid": OID_B,
        "expectedPullsOid": OID_C,
    }
    with pytest.raises(gateway.GatewayError):
        gateway.GatewayApplication._merge_request(
            "mirror2", "forkmesh", merge_body(pullNumber=0))
    with pytest.raises(gateway.GatewayError):
        gateway.GatewayApplication._merge_request(
            "mirror2", "forkmesh", merge_body(expectedHeadOid="main"))
    duplicate = (
        merge_body()[:-1]
        + b',"requestId":"merge_request_0002"}'
    )
    with pytest.raises(gateway.GatewayError):
        gateway.GatewayApplication._merge_request(
            "mirror2", "forkmesh", duplicate)


def test_gateway_job_is_async_then_reports_only_published_node_confirmation():
    class Executor:
        def call(self, request, timeout=20):
            del timeout
            action = request["action"]
            if action == "status":
                return {
                    "ok": True, "status": "missing",
                    "requestId": request["requestId"],
                    "generationReady": False,
                }
            if action == "execute":
                return {
                    "ok": True, "status": "merged",
                    "requestId": request["requestId"],
                    "baseBefore": OID_A, "head": OID_B,
                    "pullsBefore": OID_C, "baseAfter": OID_B,
                    "pullsAfter": "d" * 40, "generationReady": True,
                }
            assert action == "register"
            return {
                "ok": True, "status": "registered",
                "requestId": request["requestId"],
            }

    app = object.__new__(gateway.GatewayApplication)
    app.merge_executor = Executor()
    app._merge_lock = threading.Lock()
    app._merge_threads = {}
    app._merge_results = {}
    app._merge_payload_digests = {}
    app._reload_after_merge = lambda: None

    accepted = app._dispatch_merge_pull(
        "mirror2", "forkmesh", merge_body())
    assert accepted.status == 202
    assert json.loads(accepted.body)["status"] == "processing"

    deadline = time.time() + 2
    while time.time() < deadline:
        completed = app._dispatch_merge_pull(
            "mirror2", "forkmesh", merge_body())
        if completed.status == 200:
            break
        time.sleep(0.01)
    assert completed.status == 200
    body = json.loads(completed.body)
    assert body["status"] == "merged"
    assert body["published"] is True
    assert "node" not in body and "origin" not in body

    with pytest.raises(gateway.GatewayError, match="already used"):
        app._dispatch_merge_pull(
            "mirror2", "forkmesh",
            merge_body(expectedHeadOid="e" * 40))


def test_worker_body_rejects_duplicates_and_result_pins_reviewed_oids():
    ns = _entry_functions(
        "_https_mirror_merge_body", "_https_mirror_merge_result")
    parse = ns["_https_mirror_merge_body"]
    validate = ns["_https_mirror_merge_result"]
    parsed = parse(merge_body().decode(), 7)
    assert parsed and "sessionToken" not in parsed
    assert parse(
        merge_body().decode()[:-1]
        + ',"requestId":"merge_request_0002"}',
        7,
    ) is None
    assert parse(merge_body(pullNumber=8).decode(), 7) is None
    merged = {
        "ok": True,
        "status": "merged",
        "requestId": parsed["requestId"],
        "published": True,
        "baseBefore": OID_A,
        "head": OID_B,
        "pullsBefore": OID_C,
        "baseAfter": OID_B,
        "pullsAfter": "d" * 40,
    }
    assert validate(merged, parsed)["status"] == "merged"
    merged["head"] = "e" * 40
    assert validate(merged, parsed) is None


def test_worker_route_auth_idempotency_and_no_failover_contracts():
    assert urls.REPO_PULL_MERGE_RE.fullmatch(
        "/api/repo/forkmesh/forkmesh/pulls/7/merge")
    handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _repo_merge_jobs_make_room"):
        ENTRY_TEXT.index("\n\n\nasync def _https_mirror_proxy")
    ]
    for required in (
        "_account_session_record",
        "_account_owns_node",
        "_org_write_allowed",
        "repo_merge_jobs",
        "request_id_reused",
        "selected_node",
        "_audit_sensitive_action",
    ):
        assert required in handler
    assert "merge_node_unavailable" in handler
    assert "An uncertain transport result never fails over" in handler
    assert "_https_mirror_merge_proxy(" in handler


def test_merge_capability_is_explicit_signed_and_schema_bounded():
    assert "merge-pull" in edge_routing.PUBLIC_OPERATIONS
    assert "merge-pull" not in gateway.DEFAULT_PUBLIC_OPERATIONS
    target = edge_routing.masked_target_url(
        "https://mirror.example.test",
        "mirror2",
        "forkmesh",
        "merge-pull",
    )
    assert target.endswith(
        "/v1/repositories/mirror2/forkmesh/merge-pull")
    source = (ROOT / "tools" / "mirror_gateway.py").read_text()
    assert "merge-pull requires an operator-owned mergeExecutorCommand" in source
    assert "self._authorize(method, target, headers, body)" in source
    schema = (WORKER / "src" / "schema.py").read_text()
    migration = (
        WORKER / "migrations" / "0070_repository_pull_merge_jobs.sql"
    ).read_text()
    for text in (schema, migration):
        assert "CREATE TABLE IF NOT EXISTS repo_merge_jobs" in text
        assert "request_id TEXT PRIMARY KEY" in text
        assert "selected_node TEXT NOT NULL" in text
        assert "expires_at INTEGER NOT NULL" in text
        assert "idx_repo_merge_jobs_expiry" in text
        assert "repo_bi, status, updated_at" in text
        assert "trg_repo_merge_jobs_repo_bound" in text
        assert "trg_repo_merge_jobs_global_bound" in text
        assert "repository bytes" in text.lower()
    handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _repo_merge_jobs_make_room"):
        ENTRY_TEXT.index("\n\n\nasync def _https_mirror_proxy")
    ]
    for policy in (
        "HTTPS_MIRROR_MERGE_JOB_RETENTION_MS",
        "HTTPS_MIRROR_MERGE_JOB_REPO_LIMIT",
        "HTTPS_MIRROR_MERGE_JOB_GLOBAL_LIMIT",
        "_repo_merge_jobs_make_room",
        "merge_queue_full",
    ):
        assert policy in handler


def test_merge_job_cleanup_enforces_expiry_repo_and_global_hard_bounds():
    ns = _entry_functions("_repo_merge_jobs_make_room")
    ns["HTTPS_MIRROR_MERGE_JOB_REPO_LIMIT"] = 3
    ns["HTTPS_MIRROR_MERGE_JOB_GLOBAL_LIMIT"] = 5
    ns["HTTPS_MIRROR_MERGE_JOB_CLEANUP_BATCH"] = 8
    database = sqlite3.connect(":memory:")
    database.row_factory = sqlite3.Row
    migration = (
        WORKER / "migrations" / "0070_repository_pull_merge_jobs.sql"
    ).read_text(encoding="utf-8")
    database.executescript(migration)

    async def d1_run(_env, statement, *arguments):
        database.execute(statement, arguments)
        database.commit()

    async def d1_first(_env, statement, *arguments):
        row = database.execute(statement, arguments).fetchone()
        return dict(row) if row is not None else None

    ns["d1_run"] = d1_run
    ns["d1_first"] = d1_first
    make_room = ns["_repo_merge_jobs_make_room"]

    def add(identifier, repo, status, created, expires):
        database.execute(
            """INSERT INTO repo_merge_jobs
                 (request_id,request_digest,repo_bi,actor_bi,pull_number,
                  selected_node,status,result,created_at,updated_at,expires_at)
               VALUES (?,?,?,?,?,?,?,?,?,?,?)""",
            (
                identifier, "d" * 64, repo, "a" * 64, 1, "mirror2",
                status, "", created, created, expires,
            ),
        )
        database.commit()

    add("expired", "repo-a", "failed", 1, 2)
    add("old-terminal", "repo-a", "succeeded", 3, 10_000)
    add("active-a", "repo-a", "requested", 4, 10_000)
    assert asyncio.run(make_room(None, "repo-a", 100)) is True
    ids = {
        row[0] for row in database.execute(
            "SELECT request_id FROM repo_merge_jobs")
    }
    assert "expired" not in ids

    add("active-b", "repo-a", "requested", 5, 10_000)
    # At the per-repo ceiling, the oldest terminal row is evicted, while both
    # in-flight rows survive and one slot becomes available.
    assert asyncio.run(make_room(None, "repo-a", 100)) is True
    ids = {
        row[0] for row in database.execute(
            "SELECT request_id FROM repo_merge_jobs")
    }
    assert "old-terminal" not in ids
    assert {"active-a", "active-b"} <= ids

    add("active-c", "repo-a", "requested", 6, 10_000)
    # A full repo made entirely of in-flight work fails closed instead of
    # evicting an uncertain merge.
    assert asyncio.run(make_room(None, "repo-a", 100)) is False
    assert database.execute(
        "SELECT COUNT(*) FROM repo_merge_jobs WHERE repo_bi='repo-a'"
    ).fetchone()[0] == 3

    add("terminal-b", "repo-b", "failed", 7, 10_000)
    add("active-d", "repo-c", "requested", 8, 10_000)
    assert database.execute(
        "SELECT COUNT(*) FROM repo_merge_jobs").fetchone()[0] == 5
    # Global pressure evicts a terminal row but never an in-flight row.
    assert asyncio.run(make_room(None, "repo-z", 100)) is True
    ids = {
        row[0] for row in database.execute(
            "SELECT request_id FROM repo_merge_jobs")
    }
    assert "terminal-b" not in ids
    assert {"active-a", "active-b", "active-c", "active-d"} <= ids

    for number in range(256):
        add(
            "hard-%03d" % number,
            "repo-hard",
            "requested",
            1000 + number,
            20_000,
        )
    with pytest.raises(
        sqlite3.IntegrityError, match="repo_merge_jobs_repo_limit"
    ):
        add("hard-overflow", "repo-hard", "requested", 2000, 20_000)


def test_executor_command_receives_json_stdin_without_shell_or_ambient_secrets():
    calls = []

    def runner(command, **kwargs):
        calls.append((command, kwargs))
        return type("Done", (), {
            "stdout": json.dumps({
                "ok": True,
                "status": "missing",
                "requestId": "merge_request_0001",
            })
        })()

    executor = gateway.ExternalMergeExecutor(
        ["/usr/bin/python3", "/opt/forkmesh/merge.py"], runner=runner)
    result = executor.call({
        "schemaVersion": 1,
        "type": "forkmesh.pull-merge-executor-v1",
        "requestId": "merge_request_0001",
    })
    assert result["status"] == "missing"
    command, kwargs = calls[0]
    assert command == ["/usr/bin/python3", "/opt/forkmesh/merge.py"]
    assert isinstance(kwargs["input"], str)
    assert kwargs["text"] is True and kwargs["capture_output"] is True
    assert "shell" not in kwargs
    environment = kwargs["env"]
    assert not any(
        marker in key.upper()
        for key in environment
        for marker in ("TOKEN", "SECRET", "PASSWORD", "PRIVATE")
    )
