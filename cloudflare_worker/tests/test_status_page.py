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
        "HOST_PRESENCE_STALE_MS", "STATUS_SYSTEMS", "STATUS_HISTORY_DAYS",
        "STATUS_HISTORY_RETAIN_MS", "STATUS_SAMPLE_WINDOW_MS",
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
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in names:
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


def _sample_env(now, error_paths, host_online=True, db_ok=True, error_rows=None):
    """Stub env for record_status_sample: error_log rows + host_presence count."""
    inserted = []
    hourly = []

    async def d1_first(_env, sql, *_args):
        if "SELECT 1 AS ok" in sql:
            if not db_ok:
                raise RuntimeError("db down")
            return {"ok": 1}
        if "host_presence" in sql:
            return {"n": 1 if host_online else 0}
        return {}

    async def d1_all(_env, sql, *_args):
        if "error_log" in sql:
            if error_rows is not None:
                return error_rows
            return [{"path": p} for p in error_paths]
        return []

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO system_status_daily"):
            inserted.append({"system": args[1], "failure": args[2]})
        elif sql.startswith("INSERT INTO system_status_hourly"):
            hourly.append({"system": args[1], "failure": args[2], "reason": args[3]})

    async def noop(*_a, **_k):
        return None

    extra = {
        "Date": _Clock,
        "ensure_schema": noop,
        "d1_first": d1_first,
        "d1_all": d1_all,
        "d1_run": d1_run,
    }
    return extra, inserted, hourly


def _run_sample(error_paths=(), host_online=True, db_ok=True, error_rows=None):
    extra, inserted, hourly = _sample_env(
        _Clock.value, error_paths, host_online, db_ok, error_rows=error_rows,
    )
    g = _load("record_status_sample", extra_globals=extra)
    asyncio.run(g["record_status_sample"](object()))
    return {row["system"]: row["failure"] for row in inserted}, {
        row["system"]: row["reason"] for row in hourly
    }


# --- record_status_sample ---------------------------------------------------

def test_all_systems_recorded_ok_with_no_errors_and_a_live_host():
    results, reasons = _run_sample(error_paths=[], host_online=True, db_ok=True)
    assert set(results) == {"website", "api", "database", "git_hosting", "realtime"}
    assert all(failure == 0 for failure in results.values())
    assert all(reason is None for reason in reasons.values())


def test_database_failure_is_isolated_to_the_database_system():
    results, reasons = _run_sample(error_paths=[], host_online=True, db_ok=False)
    assert results["database"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert "db down" in reasons["database"]
    assert reasons["website"] is None


def test_no_live_host_fails_only_git_hosting():
    results, reasons = _run_sample(error_paths=[], host_online=False, db_ok=True)
    assert results["git_hosting"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert "no desktop hosts" in reasons["git_hosting"].lower()


def test_api_error_does_not_fail_website():
    results, reasons = _run_sample(error_paths=["/api/repositories"])
    assert results["api"] == 1
    assert results["website"] == 0
    assert results["realtime"] == 0
    assert "/api/repositories" in reasons["api"]
    assert reasons["website"] is None


def test_static_page_error_does_not_fail_api():
    results, reasons = _run_sample(error_paths=["/dashboard/index.html"])
    assert results["website"] == 1
    assert results["api"] == 0
    assert "/dashboard/index.html" in reasons["website"]


def test_git_clone_and_room_errors_are_bucketed_as_realtime():
    results, reasons = _run_sample(error_paths=[
        "/someowner/somerepo/info/refs",
        "/api/repo/owner/repo/rooms/main/ws",
    ])
    assert results["realtime"] == 1
    assert results["website"] == 0
    assert results["api"] == 0
    assert "info/refs" in reasons["realtime"]


def test_reason_includes_status_and_message_and_extra_count():
    results, reasons = _run_sample(error_rows=[
        {"path": "/api/repositories", "status": 500, "message": "boom"},
        {"path": "/api/other", "status": 502, "message": "boom2"},
    ])
    assert results["api"] == 1
    assert reasons["api"] == "500 on /api/repositories: boom (+1 more)"


# --- status_history ----------------------------------------------------------

def _history_env(rows, hour_rows=()):
    async def noop(*_a, **_k):
        return None

    async def d1_all(_env, sql, *_args):
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


def _run_history(rows, hour_rows=()):
    extra, captured = _history_env(rows, hour_rows)
    g = _load("status_history", extra_globals=extra)
    asyncio.run(g["status_history"](object()))
    return captured


def test_no_data_yields_unknown_status_and_null_uptime():
    out = _run_history([])
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["website"]["status"] == "unknown"
    assert by_id["website"]["uptimePct"] is None
    assert len(by_id["website"]["days"]) == 30


def test_all_checks_passing_today_is_operational():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    rows = [{"day_ts": cur_day, "system": "website", "checks": 60, "failures": 0}]
    out = _run_history(rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["website"]["status"] == "operational"
    assert by_id["website"]["uptimePct"] == 100.0


def test_all_checks_failing_today_is_down():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    rows = [{"day_ts": cur_day, "system": "api", "checks": 10, "failures": 10}]
    out = _run_history(rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["api"]["status"] == "down"
    assert by_id["api"]["uptimePct"] == 0.0


def test_some_checks_failing_today_is_degraded():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    rows = [{"day_ts": cur_day, "system": "database", "checks": 10, "failures": 3}]
    out = _run_history(rows)
    by_id = {s["id"]: s for s in out["systems"]}
    assert by_id["database"]["status"] == "degraded"
    assert by_id["database"]["uptimePct"] == 70.0


def test_current_status_uses_latest_day_not_a_stale_incident_weeks_ago():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    old_day = cur_day - 20 * DAY_MS
    rows = [
        {"day_ts": old_day, "system": "website", "checks": 60, "failures": 60},
        {"day_ts": cur_day, "system": "website", "checks": 60, "failures": 0},
    ]
    out = _run_history(rows)
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
        {"hour_ts": cur_hour - 2 * HOUR_MS, "system": "api", "checks": 1,
         "failures": 1, "reason": "502 on /api/x: boom"},
        {"hour_ts": cur_hour - 1 * HOUR_MS, "system": "api", "checks": 1,
         "failures": 0, "reason": None},
        {"hour_ts": cur_hour, "system": "api", "checks": 1,
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
    rows = [{"day_ts": cur_day, "system": "api", "checks": 2, "failures": 1}]
    hour_rows = [
        {"hour_ts": cur_hour, "system": "api", "checks": 2, "failures": 1,
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


def test_hour_with_no_checks_is_unknown_and_has_no_reason():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    out = _run_history([], hour_rows=())
    by_id = {s["id"]: s for s in out["systems"]}
    today = next(d for d in by_id["website"]["days"] if d["dayTs"] == cur_day)
    assert all(h["status"] == "unknown" for h in today["hours"])
    assert all(h["reason"] is None for h in today["hours"])


def test_operational_hour_does_not_carry_a_stale_reason():
    cur_day = (_Clock.value // DAY_MS) * DAY_MS
    cur_hour = (_Clock.value // HOUR_MS) * HOUR_MS
    hour_rows = [
        {"hour_ts": cur_hour, "system": "website", "checks": 1, "failures": 0,
         "reason": "stale reason from an earlier failure this hour"},
    ]
    out = _run_history([], hour_rows)
    by_id = {s["id"]: s for s in out["systems"]}
    today = next(d for d in by_id["website"]["days"] if d["dayTs"] == cur_day)
    this_hour = today["hours"][-1]
    assert this_hour["status"] == "operational"
    assert this_hour["reason"] is None


# --- current-state snapshot (issue #356) ------------------------------------

def _run_history_current(
    repo_count=7, online_labels=("alice", "bob"), error_count=3,
    mainnode_online=True,
):
    async def noop(*_a, **_k):
        return None

    async def d1_all(_env, sql, *_args):
        if "FROM repositories" in sql:
            return [{"key_bi": "mirror_bi", "data": "enc"}] if mainnode_online else []
        if "FROM host_presence" in sql:
            return [{"repo_bi": "mirror_bi", "ts": _Clock.value}] if mainnode_online else []
        return []

    async def d1_first(_env, sql, *_args):
        if "FROM repositories" in sql:
            return {"n": repo_count}
        if "FROM error_log" in sql:
            return {"n": error_count}
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


def test_current_mainnode_online_via_forkmesh_mirror_heartbeat():
    # Regression (adhoc #189): no node ever registers a host tunnel under the
    # literal "mainnode" owner, so host_presence for mainnode/forkmesh is never
    # written. The banner must still read online when a real node hosts a live
    # "forkmesh" mirror, whose fresh heartbeat is folded into the last-seen ts.
    def _load_with_mirror():
        async def noop(*_a, **_k):
            return None

        async def d1_all(_env, sql, *_args):
            if "FROM repositories" in sql:
                return [{"key_bi": "mirror_bi", "data": "enc"},
                        {"key_bi": "other_bi", "data": "enc2"}]
            if "FROM host_presence" in sql:
                return [{"repo_bi": "mirror_bi", "ts": _Clock.value - 1000}]
            return []

        async def d1_first(_env, sql, *_args):
            if "FROM repositories" in sql:
                return {"n": 1}
            if "FROM error_log" in sql:
                return {"n": 0}
            # The literal mainnode/forkmesh presence row never exists.
            if "FROM host_presence" in sql:
                return None
            return {}

        async def decrypt_row(_env, data):
            return {"owner": "alice", "name": "forkmesh"} if data == "enc" \
                else {"owner": "bob", "name": "notforkmesh"}

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
            "d1_first": d1_first, "decrypt_row": decrypt_row,
            "safe_segment": lambda s: str(s or "").strip().lower(),
            "_is_blocked_catalog_identity": lambda *_a: False,
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
    assert len(out["systems"]) == 5


def test_status_page_renders_current_state_grid():
    status_html = (ROOT / "public" / "status.html").read_text(encoding="utf-8")
    assert 'id="status-current"' in status_html
    assert "stat-mainnode" in status_html
    assert "stat-nodes" in status_html
    assert "stat-repos" in status_html
    assert "stat-errors" in status_html
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
    assert "hourTooltip" in status_html

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
    # The 429 banner uses the outage styling, and other fetch failures keep
    # the generic message.
    assert 'banner.classList.add("is-down");' in render
    assert "Status unavailable right now." in render


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
