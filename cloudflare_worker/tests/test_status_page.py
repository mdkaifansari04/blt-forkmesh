#!/usr/bin/env python3
"""Public /status page contracts (30-day per-system uptime history).

record_status_sample (called once a minute by the cron) folds one health
check per system into today's UTC-day bucket; status_history reads those
buckets back into the 30-day series the /status page renders. These tests
load the real functions straight out of src/entry.py (no Workers runtime)
and drive them against in-memory D1 stubs.
"""

import ast
import asyncio
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + SCHEMA.read_text(encoding="utf-8"))
# Route regexes now live in the extracted urls.py module (imported by entry.py);
# parse it alongside entry.py so the assign nodes below still resolve.
URLS = ROOT / "src" / "urls.py"
URLS_TEXT = URLS.read_text(encoding="utf-8")

DAY_MS = 86400000


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    urls_tree = ast.parse(URLS_TEXT, filename=str(URLS))
    want_assigns = {
        "ROOM_RE", "REPO_ROOM_RE", "GIT_INFO_RE", "GIT_PACK_RE",
        "RELEASE_BLOB_RE", "REPO_HOST_RE", "GIT_RECEIVE_RE",
        "HOST_PRESENCE_STALE_MS", "HTTPS_MIRROR_STATUS_FRESH_MS",
        "STATUS_SYSTEMS", "STATUS_SYSTEM_CHECKS",
        "STATUS_HISTORY_DAYS",
        "STATUS_HISTORY_RETAIN_MS", "STATUS_SAMPLE_WINDOW_MS",
        "STATUS_HOUR_MS", "STATUS_DAY_MS",
        "STATUS_MINUTES_SHOWN", "STATUS_MINUTE_RETAIN_MS",
        "STATUS_MIRROR_PREFIX", "STATUS_MIRROR_MAX",
        "STATUS_DEPLOY_GRACE_MS", "STATUS_DEPLOY_MAX_MS",
    }
    helper_names = {
        "_status_expected_checks_for_hour", "_status_effective_hour",
        "_status_minute", "_is_tunnel_content_path",
        "_status_deploy_semaphore_active",
        "_record_status_deploy_sample",
    }
    selected = []
    for node in list(urls_tree.body) + list(tree.body):
        if isinstance(node, (ast.Import, ast.ImportFrom)) and any(
            alias.name == "re" for alias in node.names
        ):
            selected.append(node)
        elif isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if targets & want_assigns:
                selected.append(node)
        elif (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and node.name in set(names) | helper_names
        ):
            selected.append(node)
    found = {n.name for n in selected if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Clock:
    value = 1_700_000_000_000  # arbitrary fixed instant

    @classmethod
    def now(cls):
        return cls.value


def _sample_env(
        now, error_paths, host_online=True, db_ok=True, error_rows=None,
        mirror_rows=None):
    """Stub error rows plus the signed direct-HTTPS mirror health count."""
    inserted = []
    hourly = []
    minutely = []

    async def d1_first(_env, sql, *_args):
        if "SELECT 1 AS ok" in sql:
            if not db_ok:
                raise RuntimeError("db down")
            return {"ok": 1}
        if "mirror_https_endpoints" in sql:
            return {"n": 1 if host_online else 0}
        return {}

    async def d1_all(_env, sql, *_args):
        if "error_log" in sql:
            if error_rows is not None:
                return error_rows
            return [{"path": p} for p in error_paths]
        if "mirror_https_endpoints" in sql:
            return list(mirror_rows or [])
        return []

    async def d1_run(_env, sql, *args):
        # The samples are written as one multi-row upsert per table (see
        # record_status_sample): decode the flat arg list back into rows.
        if sql.startswith("INSERT INTO system_status_daily"):
            for i in range(0, len(args), 3):
                inserted.append({"system": args[i + 1], "failure": args[i + 2]})
        elif sql.startswith("INSERT INTO system_status_hourly"):
            for i in range(0, len(args), 4):
                hourly.append({"system": args[i + 1], "failure": args[i + 2],
                               "reason": args[i + 3]})
        elif sql.startswith("INSERT INTO system_status_minute"):
            for i in range(0, len(args), 4):
                minutely.append({"system": args[i + 1], "ok": args[i + 2],
                                 "reason": args[i + 3]})

    async def noop(*_a, **_k):
        return None
    async def repository_probe(_env):
        return True, ""
    async def installer_status(_env, _now):
        return True, ""

    extra = {
        "Date": _Clock,
        "ensure_schema": noop,
        "d1_first": d1_first,
        "d1_all": d1_all,
        "d1_run": d1_run,
        "_flagship_repository_probe": repository_probe,
        "_record_status_monitor_transitions": noop,
        "_installer_delivery_status": installer_status,
    }
    return extra, inserted, hourly, minutely


def _run_sample(
        error_paths=(), host_online=True, db_ok=True, error_rows=None,
        mirror_rows=None):
    extra, inserted, hourly, minutely = _sample_env(
        _Clock.value, error_paths, host_online, db_ok,
        error_rows=error_rows, mirror_rows=mirror_rows,
    )
    g = _load("record_status_sample", extra_globals=extra)
    asyncio.run(g["record_status_sample"](object()))
    return (
        {row["system"]: row["failure"] for row in inserted},
        {row["system"]: row["reason"] for row in hourly},
        {row["system"]: (row["ok"], row["reason"]) for row in minutely},
    )


# --- record_status_sample ---------------------------------------------------

def test_all_systems_recorded_ok_with_no_errors_and_a_live_https_mirror():
    results, reasons, _minutes = _run_sample(error_paths=[], host_online=True, db_ok=True)
    assert set(results) == {
        "website", "api", "errors", "database", "flagship_repository",
        "installer", "git_hosting", "realtime", "durable_objects",
    }
    assert all(failure == 0 for failure in results.values())
    assert all(reason is None for reason in reasons.values())


def test_deploy_semaphore_records_a_neutral_minute_without_alerting():
    calls = []

    async def d1_first(_env, sql, *_args):
        if "world_deploy_status" in sql:
            return {
                "state": "deploying",
                "started_at": _Clock.value,
                "finished_at": 0,
            }
        return {}

    async def d1_all(_env, _sql, *_args):
        return [{"system": "mirror:mirror2"}]

    async def d1_run(_env, sql, *args):
        calls.append((sql, args))

    runtime = _load(
        "_status_deploy_semaphore_active",
        "_record_status_deploy_sample",
        extra_globals={
            "d1_first": d1_first,
            "d1_all": d1_all,
            "d1_run": d1_run,
        },
    )
    assert asyncio.run(
        runtime["_status_deploy_semaphore_active"](object(), _Clock.value)
    ) is True
    asyncio.run(
        runtime["_record_status_deploy_sample"](object(), _Clock.value)
    )
    assert len(calls) == 3
    minute_sql, minute_args = calls[-1]
    assert "system_status_minute" in minute_sql
    assert "mirror:mirror2" in minute_args
    assert "ok=1, reason=NULL" in minute_sql


def test_deploy_semaphore_has_post_ready_grace_and_fails_open_when_stale():
    row = {}

    async def d1_first(_env, _sql, *_args):
        return dict(row)

    runtime = _load(
        "_status_deploy_semaphore_active",
        extra_globals={"d1_first": d1_first},
    )
    active = runtime["_status_deploy_semaphore_active"]
    grace = runtime["STATUS_DEPLOY_GRACE_MS"]
    maximum = runtime["STATUS_DEPLOY_MAX_MS"]
    now = _Clock.value

    row.update(state="deploying", started_at=now, finished_at=0)
    assert asyncio.run(active(object(), now + maximum - 1)) is True
    assert asyncio.run(active(object(), now + maximum)) is False

    row.update(state="ready", finished_at=now)
    assert asyncio.run(active(object(), now + grace - 1)) is True
    assert asyncio.run(active(object(), now + grace)) is False

    row.update(state="failed", finished_at=now)
    assert asyncio.run(active(object(), now)) is False


def test_database_failure_is_isolated_to_the_database_system():
    results, reasons, _minutes = _run_sample(error_paths=[], host_online=True, db_ok=False)
    assert results["database"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert results["errors"] == 0
    assert "db down" in reasons["database"]
    assert reasons["website"] is None


def test_no_healthy_https_mirror_fails_only_git_hosting():
    results, reasons, _minutes = _run_sample(error_paths=[], host_online=False, db_ok=True)
    assert results["git_hosting"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert "no healthy direct https mirror" in reasons["git_hosting"].lower()


def test_signed_mirror_endpoints_get_independent_status_samples():
    fresh = _Clock.value - 30_000
    rows = [
        {"node_name": "mirror2", "checked_at": fresh, "healthy": 1,
         "integrity": "ok", "forkmesh_active": 1,
         "forkmesh_verified_at": fresh},
        {"node_name": "mirror3", "checked_at": fresh, "healthy": 0,
         "integrity": "ok", "forkmesh_active": 1,
         "forkmesh_verified_at": fresh},
        # Invalid registry text can never become a public node/system ID.
        {"node_name": "Jett user", "checked_at": fresh, "healthy": 1,
         "integrity": "ok", "forkmesh_active": 1,
         "forkmesh_verified_at": fresh},
    ]
    results, reasons, minutes = _run_sample(mirror_rows=rows)
    assert results["mirror:mirror2"] == 0
    assert minutes["mirror:mirror2"] == (1, None)
    assert results["mirror:mirror3"] == 1
    assert minutes["mirror:mirror3"][0] == 0
    assert "failed its signed HTTPS health check" in reasons["mirror:mirror3"]
    assert all("jett" not in system for system in results)


def test_stale_signed_mirror_stays_visible_as_down():
    stale = _Clock.value - 11 * 60_000
    rows = [
        {"node_name": "mirror2", "checked_at": stale, "healthy": 1,
         "integrity": "ok", "forkmesh_active": 1,
         "forkmesh_verified_at": stale},
    ]
    results, reasons, _minutes = _run_sample(mirror_rows=rows)
    assert results["mirror:mirror2"] == 1
    assert "within the last 10 minutes" in reasons["mirror:mirror2"]


def test_api_error_does_not_fail_website():
    results, reasons, _minutes = _run_sample(error_paths=["/api/repositories"])
    assert results["api"] == 1
    assert results["errors"] == 1
    assert results["website"] == 0
    assert results["realtime"] == 0
    assert "/api/repositories" in reasons["api"]
    assert reasons["website"] is None


def test_static_page_error_does_not_fail_api():
    results, reasons, _minutes = _run_sample(error_paths=["/dashboard/index.html"])
    assert results["website"] == 1
    assert results["api"] == 0
    assert results["errors"] == 1
    assert "/dashboard/index.html" in reasons["website"]


def test_git_clone_and_room_errors_are_bucketed_as_realtime():
    results, reasons, _minutes = _run_sample(error_paths=[
        "/someowner/somerepo/info/refs",
        "/api/repo/owner/repo/rooms/main/ws",
    ])
    assert results["realtime"] == 1
    assert results["errors"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert "info/refs" in reasons["realtime"]


def test_do_duration_abort_fails_its_own_bucket_and_realtime():
    # A free-tier DO duration abort is a real room failure (still counts
    # toward "realtime"), but must also have its own dedicated bucket so it's
    # visible as its own row on /status instead of hiding among other
    # realtime incidents.
    results, reasons, _minutes = _run_sample(error_rows=[
        {"path": "/api/repo/mainnode/forkmesh/rooms/general/ws", "status": 503,
         "message": "durable object aborted: Exceeded allowed duration in "
                     "Durable Objects free tier."},
    ])
    assert results["durable_objects"] == 1
    assert results["realtime"] == 1
    assert results["errors"] == 1
    assert results["api"] == 0
    assert "Exceeded allowed duration" in reasons["durable_objects"]


def test_reason_includes_status_and_message_and_extra_count():
    results, reasons, _minutes = _run_sample(error_rows=[
        {"path": "/api/repositories", "status": 500, "message": "boom"},
        {"path": "/api/other", "status": 502, "message": "boom2"},
    ])
    assert results["api"] == 1
    assert results["errors"] == 1
    assert reasons["api"] == "500 on /api/repositories: boom (+1 more)"
    assert reasons["errors"] == "500 on /api/repositories: boom (+1 more)"


def test_offline_direct_mirror_503s_do_not_fail_any_system():
    # 502/503/504 on direct-mirror content paths mean an upstream endpoint is
    # unreachable — node availability (tracked by signed HTTPS health), not an
    # API outage. A single offline node's
    # release blob being re-requested every few minutes used to paint the
    # whole "api" system red on /status.
    blob = "/api/repo/somenode/forkmesh/releases/blob/sha256/" + "a" * 64
    results, reasons, _minutes = _run_sample(error_rows=[
        {"path": blob, "status": 503, "message": "response status 503"},
        {"path": "/api/repo/somenode/forkmesh/tree", "status": 504,
         "message": "mirror timeout"},
        {"path": "/somenode/forkmesh/info/refs", "status": 502,
         "message": "no healthy mirror"},
    ])
    assert results["api"] == 0
    assert results["realtime"] == 0
    assert results["website"] == 0
    assert results["errors"] == 0
    assert reasons["api"] is None


def test_a_500_on_a_direct_mirror_path_still_fails_the_api_bucket():
    # Only upstream-unavailability statuses are excused; a real worker bug
    # (500) on the same path must still count.
    blob = "/api/repo/somenode/forkmesh/releases/blob/sha256/" + "b" * 64
    results, reasons, _minutes = _run_sample(error_rows=[
        {"path": blob, "status": 500, "message": "boom"},
    ])
    assert results["api"] == 1
    assert results["errors"] == 1
    assert "boom" in reasons["api"]


def test_repository_content_paths_are_classified_and_room_paths_are_not():
    g = _load()
    is_tunnel = g["_is_tunnel_content_path"]
    assert is_tunnel(
        "/api/repo/somenode/forkmesh/releases/blob/sha256/" + "a" * 64)
    assert is_tunnel("/api/repo/somenode/forkmesh/tree")
    assert is_tunnel("/somenode/forkmesh/info/refs")
    assert is_tunnel("/somenode/forkmesh/git-upload-pack")
    assert is_tunnel("/somenode/forkmesh/git-receive-pack")
    # A room 5xx is the worker's own Durable Object failing — must stay
    # visible in error_log / Sentry, so room paths are never excused.
    assert not is_tunnel("/api/repo/mainnode/forkmesh/rooms/general/ws")
    assert not is_tunnel("/api/repositories")


def test_fetch_skips_logging_offline_node_5xx_on_repository_content_paths():
    # Source contract: Default.fetch must not log_error (blocking Sentry call
    # + D1 write per hit) for 502/503/504 on direct-mirror content paths — an
    # offline node's re-requested release blob used to generate hundreds of
    # noise rows a day. Everything else >= 500 still logs.
    fetch_src = ENTRY_TEXT.split("async def fetch", 1)[1] \
        .split("async def _admin", 1)[0]
    guard_at = fetch_src.index("_is_tunnel_content_path(url.path)")
    log_at = fetch_src.index("await log_error(")
    assert "status in (502, 503, 504)" in fetch_src
    assert guard_at < log_at


def test_room_do_fetch_is_retried_once_on_a_transient_abort():
    # Source contract: a room DO abort (free-tier duration cap, or a
    # co-located DO resetting the isolate) is transient — the router must
    # retry once with a fresh stub before answering 503, and only the
    # persistent failure is logged.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    src = None
    for node in ast.walk(tree):
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "_route":
            src = ast.unparse(node)
    assert src, "_route not found in entry.py"
    room_block = src[src.index("room_key_from_path(url.path)"):]
    retry_at = room_block.index("for _attempt in range(2)")
    stub_at = room_block.index("FORKMESH_MAINNODE_ROOM.get(room_id)")
    fetch_at = room_block.index("room_object.fetch")
    # The stub is re-acquired inside the retry loop (a crashed isolate needs
    # a fresh stub), and the fetch happens inside the loop too.
    assert retry_at < stub_at < fetch_at
    assert "log_durable_object_abort" in room_block[fetch_at:]


def test_cron_samples_run_every_tick_and_heavy_jobs_are_staggered():
    # Source contract for the scheduled() handler: the two once-a-minute
    # samples must run unconditionally (and first, so a tick that dies later
    # has already landed its data point), while every other job sits behind a
    # minute-modulo gate. Running everything every minute is what blew the
    # invocation's resource limits and showed as downtime on /status.
    scheduled = ENTRY_TEXT.split("async def scheduled", 1)[1] \
        .split("async def fetch", 1)[0]
    sample_at = scheduled.index("await record_status_sample")
    online_at = scheduled.index("await record_online_sample")
    first_gate_at = scheduled.index("if minute % ")
    assert online_at < first_gate_at
    assert sample_at < first_gate_at
    # The /status sample runs before the heavier online sample, so a tick
    # that dies partway has already landed the publicly-visible data point.
    assert sample_at < online_at
    for job in ("verify_submitted_chain_intents", "_federation_cron",
                "send_notification_digests", "purge_stale_registered_nodes",
                "purge_blocked_catalog", "_distribute_central_fund",
                "chat_history_prune_expired"):
        job_at = scheduled.index(job + "(")
        gate = scheduled.rindex("if minute % ", 0, job_at)
        # The nearest preceding modulo gate must belong to this job's block —
        # i.e. no other job call sits between the gate and this call.
        between = scheduled[gate:job_at]
        assert not any(other + "(" in between for other in (
            "verify_submitted_chain_intents", "_federation_cron",
            "send_notification_digests", "purge_stale_registered_nodes",
            "purge_blocked_catalog", "_distribute_central_fund",
            "chat_history_prune_expired") if other != job), job


def test_ensure_schema_skips_ddl_when_fingerprint_matches():
    fake_pre_alters = ["ALTER TABLE t ADD COLUMN indexed_y"]
    fake_statements = ["CREATE TABLE IF NOT EXISTS t (x)"]
    fake_alters = ["ALTER TABLE t ADD COLUMN y"]
    prepared = []

    class _Stmt:
        def __init__(self, sql):
            self.sql = sql

        async def run(self):
            prepared.append(self.sql)

    class _DB:
        @staticmethod
        def prepare(sql):
            return _Stmt(sql)

    class _Env:
        DB = _DB

    fingerprint = "fp-current"
    d1_runs = []

    def make_globals(stored_fingerprint, select_error=None):
        async def d1_first(_env, sql, *args):
            assert "schema_meta" in sql
            if select_error is not None:
                raise RuntimeError(select_error)
            if stored_fingerprint is None:
                raise RuntimeError("no such table: schema_meta")
            return {"v": stored_fingerprint}

        async def d1_run(_env, sql, *args):
            d1_runs.append((sql, args))

        async def custody_ready(_env):
            return None

        return {
            "asyncio": asyncio,
            "SCHEMA_PRE_CREATE_ALTER_STATEMENTS": fake_pre_alters,
            "SCHEMA_STATEMENTS": fake_statements,
            "SCHEMA_ALTER_STATEMENTS": fake_alters,
            "_SCHEMA_FINGERPRINT": fingerprint,
            "_schema_ready": False,
            "_schema_lock": None,
            "d1_first": d1_first,
            "d1_run": d1_run,
            "_assert_legacy_wallet_custody_ready": custody_ready,
        }

    def load_ensure_schema(**kwargs):
        return _load("ensure_schema", "_apply_schema",
                     extra_globals=make_globals(**kwargs))

    # Fingerprint matches: one SELECT, zero DDL statements replayed.
    g = load_ensure_schema(stored_fingerprint=fingerprint)
    asyncio.run(g["ensure_schema"](_Env()))
    assert prepared == []
    assert d1_runs == []

    # No schema_meta yet (first run): full DDL replay + fingerprint recorded.
    prepared.clear()
    d1_runs.clear()
    g = load_ensure_schema(stored_fingerprint=None)
    asyncio.run(g["ensure_schema"](_Env()))
    assert prepared == fake_pre_alters + fake_statements + fake_alters
    assert len(d1_runs) == 1 and "schema_meta" in d1_runs[0][0]
    assert d1_runs[0][1] == (fingerprint,)

    # Stale fingerprint (schema changed since): replay + re-record.
    prepared.clear()
    d1_runs.clear()
    g = load_ensure_schema(stored_fingerprint="fp-older")
    asyncio.run(g["ensure_schema"](_Env()))
    assert prepared == fake_pre_alters + fake_statements + fake_alters
    assert len(d1_runs) == 1

    # Transient D1 failure (overload / internal error) on the fingerprint
    # SELECT must propagate — NOT fall through to the full DDL replay, which
    # would pile ~110 more statements onto an already-overloaded database.
    prepared.clear()
    d1_runs.clear()
    g = load_ensure_schema(stored_fingerprint=fingerprint,
                           select_error="D1_ERROR: D1 DB is overloaded.")
    try:
        asyncio.run(g["ensure_schema"](_Env()))
        raise AssertionError("expected the transient D1 error to propagate")
    except RuntimeError as exc:
        assert "overloaded" in str(exc)
    assert prepared == []
    assert d1_runs == []

    # Concurrent requests on a cold isolate share ONE apply (single-flight)
    # instead of each replaying the full DDL in parallel.
    prepared.clear()
    d1_runs.clear()
    g = load_ensure_schema(stored_fingerprint=None)

    async def _concurrent():
        await asyncio.gather(*(g["ensure_schema"](_Env()) for _ in range(5)))

    asyncio.run(_concurrent())
    assert prepared == fake_pre_alters + fake_statements + fake_alters
    assert len(d1_runs) == 1


# --- status_history ----------------------------------------------------------

def _history_env(rows, hour_rows=(), minute_rows=()):
    async def noop(*_a, **_k):
        return None

    async def d1_all(_env, sql, *_args):
        if "system_status_minute" in sql:
            return list(minute_rows)
        if "system_status_hourly" in sql:
            return list(hour_rows)
        return rows

    captured = {}

    def json_response(payload, cache_seconds=None):
        captured.update(payload)
        return payload

    extra = {
        "Date": _Clock,
        "ensure_schema": noop,
        "d1_all": d1_all,
        "json_response": json_response,
    }
    return extra, captured


def _run_history(rows, hour_rows=(), minute_rows=()):
    extra, captured = _history_env(rows, hour_rows, minute_rows)
    g = _load("status_history", extra_globals=extra)
    asyncio.run(g["status_history"](object()))
    return captured


def test_no_data_is_a_red_monitoring_failure_without_fabricated_uptime():
    # Missing expected samples mean the monitoring system failed. Render that
    # state red/down, while keeping the uptime value unset because no service
    # probe actually ran.
    out = _run_history([])
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["website"]["status"] == "down"
    assert by_id["website"]["uptimePct"] is None
    assert by_id["website"]["uptime24hPct"] is None
    assert len(by_id["website"]["days"]) == 30


def test_world_status_projection_keeps_visual_windows_without_nested_history():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    hour_rows = [
        {
            "hour_ts": cur_hour,
            "system": "website",
            "checks": 60,
            "failures": 1,
            "reason": "one failed check",
        },
    ]
    minute_rows = [
        {
            "minute_ts": (_Clock.value // 60000) * 60000,
            "system": "website",
            "ok": 1,
            "reason": None,
        },
    ]
    extra, captured = _history_env(
        [{"day_ts": cur_day, "system": "website", "checks": 60, "failures": 1}],
        hour_rows,
        minute_rows,
    )
    g = _load("status_history", extra_globals=extra)
    asyncio.run(g["status_history"](object(), "world"))

    website = next(
        system for system in captured["systems"] if system["id"] == "website")
    assert len(website["days"]) == 30
    assert all("hours" not in day for day in website["days"])
    assert website["days"][-1]["status"] == "degraded"
    assert len(website["hours"]) == 24
    assert len(website["minutes"]) == 60
    assert "checkDescription" not in website


def test_all_checks_passing_today_is_operational():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    rows = [{"day_ts": cur_day, "system": "website", "checks": 60, "failures": 0}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "website", "checks": 60,
         "failures": 0, "reason": None},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["website"]["status"] == "operational"
    # Uptime covers RECORDED samples only — every recorded check passed, so
    # 100%, with the sampling gaps reported separately as coverage instead of
    # being silently folded in as fake downtime.
    assert by_id["website"]["uptime24hPct"] == 100.0
    assert by_id["website"]["coverage24hPct"] < 100.0
    assert by_id["website"]["missing24h"] > 0


def test_all_checks_failing_today_is_down():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    rows = [{"day_ts": cur_day, "system": "api", "checks": 10, "failures": 10}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "api", "checks": 60,
         "failures": 60, "reason": "all checks failed"},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["api"]["status"] == "down"
    assert by_id["api"]["uptimePct"] == 0.0


def test_some_checks_failing_today_is_degraded():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    rows = [{"day_ts": cur_day, "system": "database", "checks": 10, "failures": 3}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "database", "checks": 60,
         "failures": 18, "reason": "db timeouts"},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["database"]["status"] == "degraded"
    assert by_id["database"]["uptime24hPct"] < 100.0


def test_current_status_uses_latest_day_not_a_stale_incident_weeks_ago():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    old_day = cur_day - 20 * DAY_MS
    rows = [
        {"day_ts": old_day, "system": "website", "checks": 60, "failures": 60},
        {"day_ts": cur_day, "system": "website", "checks": 60, "failures": 0},
    ]
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    hour_rows = [
        {"hour_ts": old_day, "system": "website", "checks": 60,
         "failures": 60, "reason": "old outage"},
        {"hour_ts": cur_hour, "system": "website", "checks": 60,
         "failures": 0, "reason": None},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    # Status reflects today (operational), even though the 30-day aggregate
    # uptime is dragged down by the old incident.
    assert by_id["website"]["status"] == "operational"
    assert by_id["website"]["uptimePct"] < 100.0


def test_current_status_uses_latest_hour_not_the_whole_days_aggregate():
    # A blip two hours ago that has since cleared shouldn't keep today's badge
    # degraded for the rest of the day — the banner should track the most
    # recent hour, not the day's cumulative failure count.
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    rows = [{"day_ts": cur_day, "system": "api", "checks": 3, "failures": 1}]
    hour_rows = [
        {"hour_ts": cur_hour - 2 * HOUR_MS, "system": "api", "checks": 60,
         "failures": 60, "reason": "502 on /api/x: boom"},
        {"hour_ts": cur_hour - 1 * HOUR_MS, "system": "api", "checks": 60,
         "failures": 0, "reason": None},
        {"hour_ts": cur_hour, "system": "api", "checks": 60,
         "failures": 0, "reason": None},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["api"]["status"] == "operational"
    assert by_id["api"]["reason"] is None


HOUR_MS = 3600000


def test_recovered_git_hosting_earlier_today_clears_the_badge():
    # Hosts dropped off overnight (a failing hour) but are back now (the
    # current hour is operational): the headline badge must read operational
    # and drop the stale "no desktop hosts checked in" reason, even though the
    # whole-day aggregate still counts the earlier failures. Regression for
    # "we have hosts online but /status shows git hosting down (1h 34m ago)".
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    prev_hour = cur_hour - HOUR_MS
    rows = [{"day_ts": cur_day, "system": "git_hosting",
             "checks": 120, "failures": 60}]
    hour_rows = [
        {"hour_ts": prev_hour, "system": "git_hosting", "checks": 60,
         "failures": 60,
         "reason": "No desktop hosts have checked in within the last 10 minutes"},
        {"hour_ts": cur_hour, "system": "git_hosting", "checks": 60,
         "failures": 0, "reason": None},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["git_hosting"]["status"] == "operational"
    assert by_id["git_hosting"]["reason"] is None
    assert by_id["git_hosting"]["reasonTs"] is None


def test_hours_breakdown_present_for_today_with_reason_on_degraded_hour():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    rows = [{"day_ts": cur_day, "system": "api", "checks": 60, "failures": 30}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "api", "checks": 60, "failures": 30,
         "reason": "500 on /api/x: boom"},
    ]
    out = _run_history(rows, hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    today = next(d for d in by_id["api"]["days"] if d["dayTs"] == cur_day)
    expected_hour_count = (cur_hour - cur_day) // HOUR_MS + 1
    assert len(today["hours"]) == expected_hour_count
    this_hour = today["hours"][-1]
    assert this_hour["status"] == "degraded"
    assert this_hour["reason"] == "500 on /api/x: boom"


def test_hour_with_no_checks_is_down_and_explains_monitoring_failure():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    out = _run_history([], hour_rows=())
    by_id = {s["id"]: s for s in out["systems"]}
    today = next(d for d in by_id["website"]["days"] if d["dayTs"] == cur_day)
    elapsed = [h for h in today["hours"] if h["expectedChecks"]]
    assert elapsed
    assert all(h["status"] == "down" for h in elapsed)
    assert all("Monitoring failed" in h["reason"] for h in elapsed)
    assert all(h["missingChecks"] == h["expectedChecks"] for h in elapsed)


def test_uptime_counts_only_recorded_samples_and_reports_coverage():
    # 10 samples recorded this hour (5 failed) out of a near-full day of
    # expected minutes: uptime must be 50% of what was RECORDED, with the
    # gaps surfaced as coverage — not a near-0% number fabricated from the
    # sampler's own absence.
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    hour_rows = [
        {"hour_ts": cur_hour, "system": "api", "checks": 10, "failures": 5,
         "reason": "500 on /api/x: boom"},
    ]
    out = _run_history([], hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    api = by_id["api"]
    assert api["status"] == "degraded"
    assert api["uptime24hPct"] == 50.0
    assert api["uptimePct"] == 50.0
    assert api["checks24h"] == 10
    assert api["failures24h"] == 5
    assert api["missing24h"] > 0
    assert api["coverage24hPct"] < 100.0


def test_stale_samples_flip_the_badge_to_monitoring_failure():
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    stale_hour = cur_hour - 5 * HOUR_MS
    hour_rows = [
        {"hour_ts": stale_hour, "system": "website", "checks": 60,
         "failures": 0, "reason": None},
    ]
    out = _run_history([], hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    website = by_id["website"]
    assert website["status"] == "down"
    assert "Monitoring failed" in website["reason"]
    assert website["reasonTs"] == stale_hour
    # The stale-but-recorded samples still count toward uptime honestly.
    assert website["uptime24hPct"] == 100.0


def test_each_system_describes_exactly_what_its_check_tests():
    # Every /status row is click-expandable to show what its health check
    # actually verifies; the descriptions must exist, be distinct per system
    # (the rows share one sampler cron, so identical-looking bars need the
    # difference spelled out), and match the real checks in
    # record_status_sample.
    out = _run_history([])
    by_id = {s["id"]: s for s in out["systems"]}
    descriptions = {sid: s["checkDescription"] for sid, s in by_id.items()}
    assert all(d and len(d) > 40 for d in descriptions.values())
    assert len(set(descriptions.values())) == len(descriptions)
    assert "D1" in descriptions["database"]
    assert "10 minutes" in descriptions["git_hosting"]
    assert "/api/" in descriptions["api"]
    assert "error log" in descriptions["errors"]
    assert "Expected degraded" in descriptions["errors"]
    assert "Exceeded allowed duration" in descriptions["durable_objects"]
    assert "not an external HTTP probe" in descriptions["website"]


def test_status_page_wires_the_click_to_expand_check_details():
    status_html = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    assert "checkDescription" in status_html
    assert "status-row-detail-panel" in status_html
    assert "What this check tests" in status_html
    assert 'head.setAttribute("aria-expanded"' in status_html
    assert "coverage24hPct" in status_html


def test_operational_hour_does_not_carry_a_stale_reason():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    hour_rows = [
        {"hour_ts": cur_hour, "system": "website", "checks": 60, "failures": 0,
         "reason": "stale reason from an earlier failure this hour"},
    ]
    out = _run_history([], hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    today = next(d for d in by_id["website"]["days"] if d["dayTs"] == cur_day)
    this_hour = today["hours"][-1]
    assert this_hour["status"] == "operational"
    assert this_hour["reason"] is None


# --- per-minute strip (60-minute row under the hour strip) -----------------

MINUTE_MS = 60000


def test_record_status_sample_writes_one_minute_row_per_system():
    _results, _reasons, minutes = _run_sample(error_paths=["/api/repositories"])
    # database/git_hosting/website/realtime all pass this tick; only "api"
    # (matched by the /api/ path) fails. minutely's "ok" is the schema's own
    # ok column (1 == passed), the inverse of the daily/hourly "failure" flag.
    ok, reason = minutes["api"]
    assert ok == 0
    assert "/api/repositories" in reason
    ok_db, no_reason = minutes["database"]
    assert ok_db == 1
    assert no_reason is None


def test_minute_strip_is_sixty_buckets_oldest_to_newest():
    out = _run_history([])
    by_id = {s["id"]: s for s in out["systems"]}
    minutes = by_id["website"]["minutes"]
    assert len(minutes) == 60
    for i in range(59):
        assert minutes[i]["minuteTs"] < minutes[i + 1]["minuteTs"]
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    assert minutes[-1]["minuteTs"] == cur_minute


def test_minute_with_no_row_is_future_only_for_the_current_bucket():
    out = _run_history([])
    by_id = {s["id"]: s for s in out["systems"]}
    minutes = by_id["website"]["minutes"]
    # The newest bucket (this minute) simply hasn't been sampled by the cron
    # yet — that's not evidence of an outage.
    assert minutes[-1]["status"] == "future"
    # Every older bucket was expected to have data and is therefore a red
    # monitoring failure.
    assert all(m["status"] == "down" for m in minutes[:-1])
    assert all("Monitoring failed" in m["reason"] for m in minutes[:-1])


def test_minute_row_reflects_ok_and_carries_its_failure_reason():
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    prev_minute = cur_minute - MINUTE_MS
    minute_rows = [
        {"minute_ts": prev_minute, "system": "api", "ok": 0,
         "reason": "500 on /api/x: boom"},
        {"minute_ts": cur_minute, "system": "api", "ok": 1, "reason": None},
    ]
    out = _run_history([], minute_rows=minute_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    minutes = {m["minuteTs"]: m for m in by_id["api"]["minutes"]}
    assert minutes[prev_minute]["status"] == "down"
    assert minutes[prev_minute]["reason"] == "500 on /api/x: boom"
    # The current minute already has a row (the cron beat us to it this
    # time), so it reflects that sample rather than reading as "future".
    assert minutes[cur_minute]["status"] == "operational"
    assert minutes[cur_minute]["reason"] is None


def test_recorded_signed_mirror_appears_as_a_full_status_system():
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    minute_rows = [
        {"minute_ts": cur_minute, "system": "mirror:mirror2",
         "ok": 1, "reason": None},
    ]
    out = _run_history([], minute_rows=minute_rows)
    system = next(s for s in out["systems"] if s["id"] == "mirror:mirror2")
    assert system["label"] == "Mirror node — mirror2"
    assert system["status"] == "operational"
    assert len(system["days"]) == 30
    assert len(system["minutes"]) == 60
    assert "account-bound direct HTTPS endpoint" in system["checkDescription"]
    assert all(s["id"] != "mirror:jett" for s in out["systems"])


def test_latest_passing_minute_clears_failure_from_hourly_rollup():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    rows = [{"day_ts": cur_day, "system": "flagship_repository",
             "checks": 20, "failures": 5}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "flagship_repository",
         "checks": 20, "failures": 5,
         "reason": "README.md body did not load"},
    ]
    minute_rows = [
        {"minute_ts": cur_minute, "system": "flagship_repository",
         "ok": 1, "reason": None},
    ]
    out = _run_history(rows, hour_rows, minute_rows)
    system = next(
        s for s in out["systems"] if s["id"] == "flagship_repository")
    # The aggregate remains below 100% and preserves the historical amber
    # dots, but the current badge follows the completed reachability probe.
    assert system["uptime24hPct"] < 100.0
    assert system["status"] == "operational"
    assert system["reason"] is None
    assert system["reasonTs"] is None


def test_latest_failing_minute_is_down_even_if_hour_is_mostly_green():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    rows = [{"day_ts": cur_day, "system": "installer",
             "checks": 59, "failures": 1}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "installer",
         "checks": 59, "failures": 1, "reason": "installer unreachable"},
    ]
    minute_rows = [
        {"minute_ts": cur_minute, "system": "installer",
         "ok": 0, "reason": "installer unreachable"},
    ]
    out = _run_history(rows, hour_rows, minute_rows)
    system = next(s for s in out["systems"] if s["id"] == "installer")
    assert system["status"] == "down"
    assert system["reason"] == "installer unreachable"
    assert system["reasonTs"] == cur_minute


def test_stale_latest_minute_reports_monitoring_failure_not_online():
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    minute_rows = [
        {"minute_ts": cur_minute - 3 * MINUTE_MS, "system": "git_hosting",
         "ok": 1, "reason": None},
    ]
    out = _run_history([], minute_rows=minute_rows)
    system = next(s for s in out["systems"] if s["id"] == "git_hosting")
    assert system["status"] == "down"
    assert "last two minutes" in system["reason"]


# --- current-state snapshot (issue #356) ------------------------------------

def _run_history_current(
    repo_count=7, online_labels=("alice", "bob"), error_count=3,
    mainnode_online=True, do_abort_count=0,
):
    async def noop(*_a, **_k):
        return None

    async def d1_all(_env, sql, *_args):
        if "FROM repositories" in sql:
            return [{"key_bi": "mirror_bi", "data": "enc"}] if mainnode_online else []
        if "FROM host_presence" in sql:
            return [{"repo_bi": "mirror_bi", "ts": _Clock.value}] if mainnode_online else []
        return []

    async def d1_first(_env, sql, *args):
        if "FROM repositories" in sql:
            return {"n": repo_count}
        if "FROM error_log" in sql:
            if "message LIKE" in sql:
                # The dedicated DO-duration-abort counter must filter on the
                # platform's abort text, not count every error row.
                assert any(
                    "Exceeded allowed duration" in str(a) for a in args
                ), sql
                return {"n": do_abort_count}
            return {"n": error_count}
        if "MAX(forkmesh_verified_at)" in sql:
            return {"ts": _Clock.value if mainnode_online else 0}
        return {}

    async def decrypt_row(_env, _data):
        return {"owner": "alice", "name": "forkmesh"}

    async def _live_online_nodes(_env, _now):
        # owner_bi -> label, same shape as the real helper
        return {"bi%d" % i: label for i, label in enumerate(online_labels)}

    async def blind_index(_env, _value):
        return "mainnode_bi"

    captured = {}

    def json_response(payload, cache_seconds=None):
        captured.update(payload)
        return payload

    extra = {
        "Date": _Clock,
        "ensure_schema": noop,
        "d1_all": d1_all,
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
        "safe_segment": lambda s: str(s or "").strip().lower(),
        "_is_blocked_catalog_identity": lambda *_a: False,
        "_live_online_nodes": _live_online_nodes,
        "blind_index": blind_index,
        "json_response": json_response,
    }
    g = _load("status_history", extra_globals=extra)
    asyncio.run(g["status_history"](object()))
    return captured


def test_current_snapshot_reports_the_four_headline_metrics():
    out = _run_history_current(
        repo_count=42, online_labels=("alice", "bob", "carol"),
        error_count=5, mainnode_online=True,
    )
    current = out["current"]
    assert current["catalogRepos"] == 42
    assert current["onlineNodes"] == 3
    assert current["errors24h"] == 5
    assert current["mainnodeOnline"] is True


def test_current_online_count_dedupes_by_node_label_not_row_count():
    # _live_online_nodes keys by owner, but two entries can carry the same label
    # (e.g. an ad-hoc re-key); the headline count must be distinct labels.
    out = _run_history_current(online_labels=("alice", "alice", "bob"))
    assert out["current"]["onlineNodes"] == 2


def test_current_snapshot_offline_mainnode_is_false():
    out = _run_history_current(mainnode_online=False)
    assert out["current"]["mainnodeOnline"] is False


def test_current_snapshot_counts_do_duration_aborts_separately():
    # AbortError("Exceeded allowed duration in Durable Objects free tier."):
    # the /status page gets its own counter for platform-killed DO requests so
    # plan-limit churn is distinguishable from real bugs in errors24h.
    out = _run_history_current(error_count=9, do_abort_count=4)
    current = out["current"]
    assert current["errors24h"] == 9
    assert current["doDurationAborts24h"] == 4


def test_room_route_turns_a_do_duration_abort_into_a_retryable_503():
    # Regression: a room DO request that outlives the free-tier duration cap
    # dies with pyodide.http.AbortError, which used to escape _route and
    # surface as a Worker Error 1101. The room fetch must be guarded, log the
    # cause (so the /status counter above sees it), and answer 503 without
    # leaking exception detail to the client.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    src = None
    for node in ast.walk(tree):
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "_route":
            src = ast.unparse(node)
    assert src, "_route not found in entry.py"
    room_block = src[src.index("room_key_from_path(url.path)"):]
    fetch_at = room_block.index("room_object.fetch")
    assert "try:" in room_block[:fetch_at]
    after_fetch = room_block[fetch_at:]
    assert "log_durable_object_abort" in after_fetch
    assert "status=503" in after_fetch
    # Repository browse keeps no second DO fetch to guard: it is ordinary
    # direct HTTPS and leaves the multiplayer room socket intact.
    browse_block = src[src.index("REPO_HOST_RE.match"):src.index(
        "room_key_from_path(url.path)")]
    assert "host_object.fetch" not in browse_block
    assert "_https_mirror_proxy" in browse_block


def test_current_mainnode_online_via_signed_forkmesh_https_proof():
    # The banner reflects a fresh, integrity-matching direct HTTPS proof for
    # forkmesh/forkmesh. A control-socket heartbeat alone is not evidence that
    # repository bytes are available.
    def _load_with_mirror():
        async def noop(*_a, **_k):
            return None

        async def d1_all(_env, sql, *_args):
            return []

        async def d1_first(_env, sql, *_args):
            if "FROM repositories" in sql:
                return {"n": 1}
            if "FROM error_log" in sql:
                return {"n": 0}
            if "MAX(forkmesh_verified_at)" in sql:
                return {"ts": _Clock.value - 1000}
            return {}

        async def _live_online_nodes(_env, _now):
            return {"bi0": "alice"}

        async def blind_index(_env, _value):
            return "mainnode_bi"

        captured = {}

        def json_response(payload, cache_seconds=None):
            captured.update(payload)
            return payload

        extra = {
            "Date": _Clock, "ensure_schema": noop, "d1_all": d1_all,
            "d1_first": d1_first,
            "_live_online_nodes": _live_online_nodes,
            "blind_index": blind_index, "json_response": json_response,
        }
        g = _load("status_history", extra_globals=extra)
        asyncio.run(g["status_history"](object()))
        return captured

    out = _load_with_mirror()
    assert out["current"]["mainnodeOnline"] is True
    assert out["current"]["mainnodeLastSeenTs"] == _Clock.value - 1000


def test_current_snapshot_survives_a_failing_read():
    # A blank/failing metric is None, and it must not blank the rest of the page.
    def _load_broken():
        async def noop(*_a, **_k):
            return None

        async def d1_all(_env, sql, *_args):
            return []

        async def d1_first(_env, sql, *_args):
            if "FROM repositories" in sql:
                raise RuntimeError("boom")
            return {"n": 0}

        async def _live_online_nodes(_env, _now):
            return {}

        async def blind_index(_env, _value):
            return "bi"

        captured = {}

        def json_response(payload, cache_seconds=None):
            captured.update(payload)
            return payload

        extra = {
            "Date": _Clock, "ensure_schema": noop, "d1_all": d1_all,
            "d1_first": d1_first, "_live_online_nodes": _live_online_nodes,
            "blind_index": blind_index, "json_response": json_response,
        }
        g = _load("status_history", extra_globals=extra)
        asyncio.run(g["status_history"](object()))
        return captured

    out = _load_broken()
    assert out["current"]["catalogRepos"] is None
    assert out["current"]["onlineNodes"] == 0
    # systems still rendered despite the failed metric
    assert len(out["systems"]) == 9


def test_flagship_repository_monitor_is_public_and_deduplicates_email_states():
    assert '("flagship_repository", "forkmesh/forkmesh repository page")' in ENTRY_TEXT
    assert "FLAGSHIP_REPOSITORY_URL = \"https://forkmesh.com/forkmesh/forkmesh\"" in ENTRY_TEXT
    assert 'str(item.get("name") or "").lower() == "readme.md"' in ENTRY_TEXT
    assert "Repository page shell did not load" in ENTRY_TEXT
    assert "await org_alias_rewrite(env, request, route_url)" in ENTRY_TEXT
    assert "Root repository tree did not contain README.md" in ENTRY_TEXT
    assert "README.md body did not load" in ENTRY_TEXT
    assert "repository_monitor_state" in ENTRY_TEXT
    assert "notified_state" in ENTRY_TEXT
    assert "[ForkMesh outage]" in ENTRY_TEXT
    assert "[ForkMesh recovered]" in ENTRY_TEXT
    assert "is passing again after " in ENTRY_TEXT
    assert "Suggested first step" in ENTRY_TEXT
    assert "WHERE monitor_id LIKE 'status:%'" in ENTRY_TEXT


def test_installer_delivery_is_checked_every_ten_minutes_and_public():
    assert '("installer", "Installer delivery")' in ENTRY_TEXT
    assert "INSTALLER_CHECK_INTERVAL_MS = 10 * 60 * 1000" in ENTRY_TEXT
    assert "async def _installer_delivery_probe" in ENTRY_TEXT
    assert '"release.json", "release.json.sig", "SHASUMS256.txt"' in ENTRY_TEXT
    assert "releases/blob/sha256/" in ENTRY_TEXT
    assert "Cloudflare cannot execute Bash" in ENTRY_TEXT
    assert "source-build fallback" in ENTRY_TEXT


def test_status_page_renders_current_state_grid():
    status_html = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    assert 'id="status-current"' in status_html
    assert "stat-mainnode" in status_html
    assert "stat-nodes" in status_html
    assert "stat-repos" in status_html
    assert "stat-errors" in status_html
    assert "stat-do-aborts" in status_html
    assert "doDurationAborts24h" in status_html
    assert "data.current" in status_html


# --- static wiring -----------------------------------------------------------

def test_worker_exposes_status_route_and_schema():
    assert 'url.path in ("/api/status", "/api/status/")' in ENTRY_TEXT
    assert "async def status_history" in ENTRY_TEXT
    assert "async def record_status_sample" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS system_status_daily" in ENTRY_TEXT
    assert "idx_system_status_daily_day" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS system_status_hourly" in ENTRY_TEXT
    assert "idx_system_status_hourly_hour" in ENTRY_TEXT


def test_cron_samples_status_every_tick():
    assert "await record_status_sample(self.env)" in ENTRY_TEXT


def test_status_page_asset_and_redirect_exist():
    status_html = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    assert 'fetch("/api/status"' in status_html
    assert "status-day" in status_html
    assert "status-day-hour" in status_html
    assert "status-day-stack" in status_html
    assert "status-hour-bar" in status_html
    assert "status-hour-detail" in status_html
    assert "hourTooltip" in status_html
    assert "uptime24hPct" in status_html
    assert "status-row-metrics" in status_html
    assert "last 24 hourly checks" in status_html
    assert "currentHour - 23 * 3600000" in status_html
    # Missing samples are monitoring gaps, never fabricated downtime — the
    # copy must say so, and the old "counts as downtime" claim must be gone.
    assert "monitoring gap" in status_html
    assert "count as downtime" not in status_html
    assert "optimizing traffic usage for bots" in status_html
    assert "marker.classList.add(\"is-hovered\")" in status_html
    assert "status-day-hour is-" in status_html
    assert "status-hour-slice is-" in status_html

    redirects = (ROOT / "public" / "_redirects").read_text(encoding="utf-8")
    assert "/status /status.html 200" in redirects


def test_status_page_names_cloudflare_rate_limiting_on_429():
    # When the free-plan daily quota runs out Cloudflare answers every route —
    # even /api/status — with an HTML 429. The status page must say exactly
    # that (a plan limit that resets on its own, not an outage) instead of the
    # generic "unavailable" message (adhoc #80).
    status_html = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    render = status_html[
        status_html.index("async function render()"):
        status_html.index("render();")
    ]
    assert "if (res.status === 429) rateLimited = true;" in render
    assert "Rate limited by Cloudflare" in render
    assert "not an outage" in render
    assert "resets automatically" in render
    # The 429 banner uses the outage styling, and other fetch failures show
    # their actual HTTP/content/body details instead of a generic message.
    assert 'banner.classList.add("is-down");' in render
    assert "Status unavailable right now." not in render
    assert "failedStatusDetail" in render
    assert "Status API unavailable" in render
    assert "Content-Type:" in status_html
    assert "Body:" in status_html
    assert "escapeHtml(issue.detail)" in render


def test_migration_file_matches_worker_schema():
    migration = (ROOT / "migrations" / "0022_system_status.sql").read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS system_status_daily" in migration
    hourly_migration = (
        ROOT / "migrations" / "0023_system_status_hourly.sql"
    ).read_text(encoding="utf-8")
    assert "CREATE TABLE IF NOT EXISTS system_status_hourly" in hourly_migration


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("ok")


def test_status_reports_cron_liveness_for_the_banner():
    # current.lastCronSampleTs = newest minute with a recorded sample, so the
    # page can say "the sampling cron is behind" instead of letting missing
    # samples read as a confirmed outage.
    cur_minute = (_Clock.value // MINUTE_MS) * MINUTE_MS
    minute_rows = [
        {"minute_ts": cur_minute - 7 * MINUTE_MS, "system": "website",
         "ok": 1, "reason": None},
        {"minute_ts": cur_minute - 5 * MINUTE_MS, "system": "api",
         "ok": 1, "reason": None},
    ]
    out = _run_history([], minute_rows=minute_rows)
    assert out["current"]["lastCronSampleTs"] == cur_minute - 5 * MINUTE_MS
    # No samples at all -> null, not 0 (the page treats it as "unknown").
    out = _run_history([])
    assert out["current"]["lastCronSampleTs"] is None

    page = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    assert "lastCronSampleTs" in page
    assert "sampling cron is behind" in page
