"""Behavioral regressions for durable FIFO organization-agent job claims."""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


def _load_handler(store):
    tree = ast.parse((
    # entry.py + its lazily-split domain modules
    (ENTRY.parent / "forkbot.py").read_text(encoding="utf-8")
    + "\n\n\n"
    + (ENTRY.parent / "fediverse_routes.py").read_text(encoding="utf-8")
    + "\n\n\n"
    + ENTRY.read_text(encoding="utf-8")
    + "\n\n\n"
), filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "repo_org_agent_jobs_handler"
    ]
    assert len(selected) == 1
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))

    class Clock:
        @staticmethod
        def now():
            return 2_100_000_000_000

    ids = iter(f"lease-{number}" for number in range(1, 100))

    async def noop(*_args, **_kwargs):
        return None

    def response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    namespace = {
        "Date": Clock,
        "MAX_NODE_NAME": 64,
        "MAX_REPO_SEGMENT": 100,
        "ORG_AGENT_JOB_LEASE_MS": 120_000,
        "ensure_schema": noop,
        "method_name": lambda request: request.method,
        "_authorize_owner": lambda *_args: asyncio.sleep(0, result=True),
        "clean_string": lambda value, maximum: str(value or "")[:maximum],
        "d1_run": store.run,
        "d1_first": store.first,
        "decrypt_row": lambda _env, value: asyncio.sleep(
            0, result=None if value == "corrupt" else value),
        "_ap_uuid": lambda: next(ids),
        "json_response": response,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["repo_org_agent_jobs_handler"]


class QueueStore:
    def __init__(self, count=0):
        self.rows = [
            {
                "id": number,
                "session_id": f"session-{number}",
                "target_node": "desktop1",
                "repo": "forkmesh",
                "status": "queued",
                "lease_id": "",
                "updated_at": 2_100_000_000_000,
                "data": {
                    "sessionId": f"session-{number}",
                    "prompt": f"task {number}",
                    "securityCheck": {
                        "provider": "claude-code",
                        "model": "haiku",
                        "tools": False,
                        "failClosed": True,
                    },
                },
            }
            for number in range(1, count + 1)
        ]

    async def run(self, _env, sql, *args):
        if sql.startswith(
                "UPDATE org_agent_jobs SET status='queued',lease_id=''"):
            now, owner, repo, threshold = args
            for row in self.rows:
                if (
                    row["target_node"] == owner
                    and row["repo"] == repo
                    and row["status"] == "leased"
                    and row["updated_at"] < threshold
                ):
                    row.update(
                        status="queued", lease_id="", updated_at=now)
            return
        if "SET status='failed'" in sql:
            now, job_id, owner = args
            for row in self.rows:
                if (
                    row["id"] == job_id
                    and row["target_node"] == owner
                    and row["status"] == "queued"
                ):
                    row.update(status="failed", updated_at=now)
            return
        if "SET status='leased'" in sql:
            lease_id, now, job_id, owner = args
            for row in self.rows:
                if (
                    row["id"] == job_id
                    and row["target_node"] == owner
                    and row["status"] == "queued"
                ):
                    row.update(
                        status="leased",
                        lease_id=lease_id,
                        updated_at=now,
                    )
            return
        raise AssertionError("unexpected mutation: " + sql)

    async def first(self, _env, sql, *args):
        if "FROM org_agent_jobs queued" in sql:
            owner, repo, leased_owner, leased_repo = args
            assert (owner, repo) == (leased_owner, leased_repo)
            if any(
                row["target_node"] == owner
                and row["repo"] == repo
                and row["status"] == "leased"
                for row in self.rows
            ):
                return None
            candidate = next(
                (
                    dict(row)
                    for row in self.rows
                    if row["target_node"] == owner
                    and row["repo"] == repo
                    and row["status"] == "queued"
                ),
                None,
            )
            # Capture before yielding so concurrent callers exercise the
            # conditional-update verification rather than serializing here.
            await asyncio.sleep(0)
            return candidate
        if "AND status='leased' AND lease_id=?" in sql:
            job_id, owner, repo, lease_id = args
            return next(
                (
                    {"id": row["id"]}
                    for row in self.rows
                    if row["id"] == job_id
                    and row["target_node"] == owner
                    and row["repo"] == repo
                    and row["status"] == "leased"
                    and row["lease_id"] == lease_id
                ),
                None,
            )
        raise AssertionError("unexpected query: " + sql)


def _request():
    return SimpleNamespace(method="GET")


def test_claims_one_job_at_a_time_in_fifo_order_without_admission_cap():
    async def scenario():
        store = QueueStore(17)
        handler = _load_handler(store)
        claimed = []
        for _index in range(17):
            response = await handler(
                object(), _request(), "desktop1", "forkmesh")
            assert response["status"] == 200
            jobs = response["data"]["jobs"]
            assert len(jobs) == 1
            claimed.append(jobs[0]["jobId"])
            row = next(
                row for row in store.rows if row["id"] == jobs[0]["jobId"])
            # A successful running report releases the preflight lease and lets
            # the next oldest durable row start.
            row["status"] = "running"
        assert claimed == list(range(1, 18))

    asyncio.run(scenario())


def test_concurrent_claimers_never_receive_the_same_job():
    async def scenario():
        store = QueueStore(1)
        handler = _load_handler(store)
        responses = await asyncio.gather(
            handler(object(), _request(), "desktop1", "forkmesh"),
            handler(object(), _request(), "desktop1", "forkmesh"),
        )
        jobs = [
            job
            for response in responses
            for job in response["data"]["jobs"]
        ]
        assert [job["jobId"] for job in jobs] == [1]

    asyncio.run(scenario())


def test_corrupt_and_expired_rows_do_not_starve_the_fifo():
    async def scenario():
        store = QueueStore(3)
        store.rows[0]["data"] = "corrupt"
        store.rows[1].update(
            status="leased",
            lease_id="expired",
            updated_at=2_100_000_000_000 - 120_001,
        )
        handler = _load_handler(store)
        response = await handler(
            object(), _request(), "desktop1", "forkmesh")
        assert store.rows[0]["status"] == "failed"
        assert response["data"]["jobs"][0]["jobId"] == 2

    asyncio.run(scenario())
