#!/usr/bin/env python3
"""Self-rearming scheduled-runner reliability contracts."""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
SOLANA = ROOT / "src" / "solana.py"
WRANGLER = ROOT / "wrangler.toml"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
SOLANA_TEXT = SOLANA.read_text(encoding="utf-8")
WRANGLER_TEXT = WRANGLER.read_text(encoding="utf-8")


class _Storage:
    def __init__(self):
        self.data = {}
        self.alarm_at = None

    async def get(self, key):
        return self.data.get(key)

    async def put(self, key, value):
        self.data[key] = value

    def setAlarm(self, timestamp):
        self.alarm_at = int(timestamp)


def _load_runner(run_jobs):
    wanted_constants = {
        "CRON_RUNNER_INTERVAL_MS",
        "CRON_RUNNER_KICK_DELAY_MS",
        "CRON_RUNNER_ALARM_OFFSET_MS",
    }
    selected = []
    for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body:
        if isinstance(node, ast.Assign):
            names = {
                target.id for target in node.targets
                if isinstance(target, ast.Name)
            }
            if names & wanted_constants:
                selected.append(node)
        elif isinstance(node, ast.ClassDef) and node.name in (
                "_CronJobEntrypoint", "ForkMeshCronRunner"):
            selected.append(node)

    class DurableObject:
        def __init__(self, ctx, env):
            self.ctx = ctx
            self.env = env

    class Date:
        value = 1_020_000

        @classmethod
        def now(cls):
            return cls.value

    class Default:
        _run_scheduled_jobs = staticmethod(run_jobs)

    def json_response(payload, status=200):
        return SimpleNamespace(status=status, payload=payload)

    namespace = {
        "DurableObject": DurableObject,
        "Date": Date,
        "Default": Default,
        "urlparse": urlparse,
        "json_response": json_response,
        "_safe_error_text": str,
    }
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def test_trigger_only_kicks_the_alarm_runner_and_config_registers_it():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    default = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "Default")
    scheduled = ast.unparse(next(
        node for node in default.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "scheduled"))
    jobs = ast.unparse(next(
        node for node in default.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_run_scheduled_jobs"))

    assert "_cron_runner_kick(self.env)" in scheduled
    assert "record_status_sample" not in scheduled
    assert "record_status_sample" in jobs
    assert "https_mirror_health_cron" in jobs
    assert "_cron_watchdog_completion" in jobs

    assert 'name = "FORKMESH_CRON_RUNNER"' in WRANGLER_TEXT
    assert 'class_name = "ForkMeshCronRunner"' in WRANGLER_TEXT
    assert 'tag = "v14"' in WRANGLER_TEXT
    assert 'new_sqlite_classes = ["ForkMeshCronRunner"]' in WRANGLER_TEXT


def test_runner_rearms_before_work_and_deduplicates_completed_minute():
    observed_alarms = []

    async def run_jobs(receiver):
        observed_alarms.append(storage.alarm_at)
        assert receiver.env is env

    ns = _load_runner(run_jobs)
    storage = _Storage()
    ctx = SimpleNamespace(storage=storage)
    env = object()
    runner = ns["ForkMeshCronRunner"](ctx, env)
    request = SimpleNamespace(
        url="https://forkmesh.internal/cron-runner/kick")

    response = asyncio.run(runner.fetch(request))
    assert response.status == 200
    assert storage.alarm_at == (
        ns["Date"].value + ns["CRON_RUNNER_KICK_DELAY_MS"])

    asyncio.run(runner.alarm())
    minute = ns["Date"].value // ns["CRON_RUNNER_INTERVAL_MS"]
    expected_next = (
        (minute + 1) * ns["CRON_RUNNER_INTERVAL_MS"]
        + ns["CRON_RUNNER_ALARM_OFFSET_MS"])
    assert observed_alarms == [expected_next]
    assert storage.data["last_completed_slot"] == minute
    assert storage.alarm_at == expected_next

    asyncio.run(runner.alarm())
    assert observed_alarms == [expected_next]


def test_runner_keeps_successor_alarm_when_a_batch_fails():
    async def fail(_receiver):
        raise RuntimeError("batch failed")

    ns = _load_runner(fail)
    storage = _Storage()
    runner = ns["ForkMeshCronRunner"](
        SimpleNamespace(storage=storage), object())

    asyncio.run(runner.alarm())
    minute = ns["Date"].value // ns["CRON_RUNNER_INTERVAL_MS"]
    assert storage.alarm_at == (
        (minute + 1) * ns["CRON_RUNNER_INTERVAL_MS"]
        + ns["CRON_RUNNER_ALARM_OFFSET_MS"])
    assert "batch failed" in storage.data["last_failure"]
    assert "last_completed_slot" not in storage.data


def test_mid_minute_trigger_kick_reconciles_without_losing_next_slot():
    observed_slots = []

    async def run_jobs(_receiver):
        observed_slots.append(
            ns["Date"].value // ns["CRON_RUNNER_INTERVAL_MS"])

    ns = _load_runner(run_jobs)
    interval = ns["CRON_RUNNER_INTERVAL_MS"]
    offset = ns["CRON_RUNNER_ALARM_OFFSET_MS"]
    ns["Date"].value = 17 * interval + 43_000
    storage = _Storage()
    storage.data["last_completed_slot"] = 17
    expected_minute_18_alarm = 18 * interval + offset
    storage.alarm_at = expected_minute_18_alarm
    runner = ns["ForkMeshCronRunner"](
        SimpleNamespace(storage=storage), object())
    request = SimpleNamespace(
        url="https://forkmesh.internal/cron-runner/kick")

    response = asyncio.run(runner.fetch(request))
    assert response.status == 200
    assert storage.alarm_at == 17 * interval + 44_000

    # The reconciliation alarm lands in the already-completed minute. It must
    # deduplicate the jobs while restoring the canonical :01.5 next wake-up.
    ns["Date"].value = storage.alarm_at
    asyncio.run(runner.alarm())
    assert observed_slots == []
    assert storage.alarm_at == expected_minute_18_alarm

    # The restored alarm owns the following minute exactly once and persists
    # its successor before work.
    ns["Date"].value = expected_minute_18_alarm
    asyncio.run(runner.alarm())
    assert observed_slots == [18]
    assert storage.data["last_completed_slot"] == 18
    assert storage.alarm_at == 19 * interval + offset


def test_cron_reachable_fetches_do_not_create_nested_pyodide_tasks():
    mirror_fetch = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _https_mirror_fetch_text"):
        ENTRY_TEXT.index("\n\n_CLOUDFLARE_EDGE_RANGES_MEMO")
    ]
    mirror_cron = ENTRY_TEXT[
        ENTRY_TEXT.index("async def https_mirror_health_cron"):
        ENTRY_TEXT.index("\n\nasync def _https_mirror_private_candidates")
    ]
    assert "js_fetch_with_timeout(" in mirror_fetch
    assert "asyncio.wait_for" not in mirror_fetch
    assert "asyncio.gather" not in mirror_cron
    assert "for row in rows:" in mirror_cron

    solana_rpc = SOLANA_TEXT[
        SOLANA_TEXT.index("async def _solana_rpc"):
    ]
    assert "AbortSignal.timeout(SOLANA_RPC_TIMEOUT_MS)" in solana_rpc
    assert "asyncio.wait_for" not in solana_rpc
