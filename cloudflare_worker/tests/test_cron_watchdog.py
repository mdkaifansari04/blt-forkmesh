#!/usr/bin/env python3
"""Cron completion watchdog transition and runtime-safety contracts."""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WRANGLER = ROOT / "wrangler.toml"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WRANGLER_TEXT = WRANGLER.read_text(encoding="utf-8")


def _load_watchdog():
    wanted_constants = {
        "CRON_WATCHDOG_GRACE_MS",
        "CRON_WATCHDOG_RETRY_MS",
    }
    wanted_functions = {
        "_cron_watchdog_email_content",
        "_send_cron_watchdog_email",
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
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name in wanted_functions:
                selected.append(node)
        elif isinstance(node, ast.ClassDef) and \
                node.name == "ForkMeshCronWatchdog":
            selected.append(node)

    sends = []

    class DurableObject:
        def __init__(self, ctx, env):
            self.ctx = ctx
            self.env = env

    class Date:
        value = 1_000_000

        @classmethod
        def now(cls):
            return cls.value

    async def _repository_monitor_admin_emails(_env):
        return ["admin@example.test"]

    async def _status_alert_emails_enabled(_env):
        return namespace["_alerts_enabled"]

    async def _send_email(_env, email, subject, text, html):
        sends.append((email, subject, text, html))
        return True

    def _flagship_monitor_duration(milliseconds):
        return "%d minutes" % (int(milliseconds) // 60_000)

    def _html_escape(value):
        return str(value)

    def _forkmesh_email_card_html(heading, intro, body, footer=""):
        return heading + intro + body + footer

    def json_response(payload, status=200):
        return SimpleNamespace(status=status, payload=payload)

    namespace = {
        "DurableObject": DurableObject,
        "Date": Date,
        "urlparse": urlparse,
        "_repository_monitor_admin_emails":
            _repository_monitor_admin_emails,
        "_status_alert_emails_enabled": _status_alert_emails_enabled,
        # The flagship repo's admin switch, off in production until an org
        # admin opts in. The transition tests below exercise the on path.
        "_alerts_enabled": True,
        "_send_email": _send_email,
        "_flagship_monitor_duration": _flagship_monitor_duration,
        "_html_escape": _html_escape,
        "_forkmesh_email_card_html": _forkmesh_email_card_html,
        "json_response": json_response,
        "durable_object_traffic_note": lambda *_args, **_kwargs: None,
        "durable_object_traffic_flush":
            (lambda *_args, **_kwargs: asyncio.sleep(0)),
        "_sends": sends,
    }
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Storage:
    def __init__(self):
        self.data = {}
        self.alarm_at = None

    async def get(self, key):
        return self.data.get(key)

    async def put(self, key, value):
        self.data[key] = value

    async def setAlarm(self, timestamp):
        self.alarm_at = int(timestamp)


def test_watchdog_alerts_once_and_recovers_once():
    ns = _load_watchdog()
    storage = _Storage()
    ctx = SimpleNamespace(storage=storage)
    watchdog = ns["ForkMeshCronWatchdog"](ctx, object())
    request = SimpleNamespace(
        url="https://forkmesh.internal/cron-watchdog/completed")
    Date = ns["Date"]
    grace = ns["CRON_WATCHDOG_GRACE_MS"]

    # First successful completion arms the independent alarm without email.
    response = asyncio.run(watchdog.fetch(request))
    assert response.status == 200
    assert storage.data["last_completion_at"] == Date.value
    assert storage.alarm_at == Date.value + grace
    assert ns["_sends"] == []

    # Three missed ticks produce exactly one outage message.
    Date.value += grace
    asyncio.run(watchdog.alarm())
    assert len(ns["_sends"]) == 1
    assert ns["_sends"][0][1].startswith("[ForkMesh outage]")
    assert storage.data["notified_state"] == "down"
    asyncio.run(watchdog.alarm())
    assert len(ns["_sends"]) == 1

    # The next fully completed cron tick produces one recovery and rearms.
    Date.value += 60_000
    asyncio.run(watchdog.fetch(request))
    assert len(ns["_sends"]) == 2
    assert ns["_sends"][1][1].startswith("[ForkMesh recovered]")
    assert storage.data["notified_state"] == "up"
    assert storage.data["outage_started_at"] == 0
    asyncio.run(watchdog.fetch(request))
    assert len(ns["_sends"]) == 2


def test_watchdog_alarm_honors_a_racing_newer_completion():
    ns = _load_watchdog()
    storage = _Storage()
    ctx = SimpleNamespace(storage=storage)
    watchdog = ns["ForkMeshCronWatchdog"](ctx, object())
    Date = ns["Date"]
    grace = ns["CRON_WATCHDOG_GRACE_MS"]
    storage.data["last_completion_at"] = Date.value

    Date.value += grace - 1
    asyncio.run(watchdog.alarm())
    assert storage.alarm_at == storage.data["last_completion_at"] + grace
    assert ns["_sends"] == []


def test_watchdog_rearms_when_alert_delivery_is_unavailable():
    ns = _load_watchdog()
    storage = _Storage()
    ctx = SimpleNamespace(storage=storage)
    watchdog = ns["ForkMeshCronWatchdog"](ctx, object())
    Date = ns["Date"]
    grace = ns["CRON_WATCHDOG_GRACE_MS"]
    retry = ns["CRON_WATCHDOG_RETRY_MS"]
    storage.data["last_completion_at"] = Date.value - grace

    async def unavailable(_env):
        raise RuntimeError("D1 unavailable")

    ns["_repository_monitor_admin_emails"] = unavailable
    asyncio.run(watchdog.alarm())
    assert storage.data.get("notified_state") != "down"
    assert storage.alarm_at == Date.value + retry
    assert ns["_sends"] == []


def test_watchdog_sends_nothing_while_alert_mail_is_switched_off():
    ns = _load_watchdog()
    ns["_alerts_enabled"] = False
    storage = _Storage()
    ctx = SimpleNamespace(storage=storage)
    watchdog = ns["ForkMeshCronWatchdog"](ctx, object())
    request = SimpleNamespace(
        url="https://forkmesh.internal/cron-watchdog/completed")
    Date = ns["Date"]
    grace = ns["CRON_WATCHDOG_GRACE_MS"]
    retry = ns["CRON_WATCHDOG_RETRY_MS"]
    storage.data["last_completion_at"] = Date.value - grace

    # A suppressed outage still settles the transition: rearming the retry
    # alarm would spin forever on a send that is never going to happen.
    asyncio.run(watchdog.alarm())
    assert ns["_sends"] == []
    assert storage.data["notified_state"] == "down"
    assert storage.alarm_at != Date.value + retry

    # The later recovery is suppressed too, and clears the outage state.
    asyncio.run(watchdog.fetch(request))
    assert ns["_sends"] == []
    assert storage.data["notified_state"] == "up"
    assert storage.data["outage_started_at"] == 0


def test_status_alert_mail_is_off_until_a_repo_admin_enables_it():
    # Defaults must stay off: the switch is opt-in, per repository, and the
    # two send paths both consult it.
    defaults = ENTRY_TEXT.split("REPO_ALERT_SETTING_DEFAULTS = {", 1)[1] \
        .split("}", 1)[0]
    assert '"statusEmails": False' in defaults
    assert ENTRY_TEXT.count("await _status_alert_emails_enabled(env)") == 2
    assert "CREATE TABLE IF NOT EXISTS repo_alert_settings" in (
        ROOT / "src" / "schema.py").read_text(encoding="utf-8")


def test_cron_runtime_and_durable_object_are_wired():
    scheduled = ENTRY_TEXT.split("async def scheduled", 1)[1] \
        .split("async def fetch", 1)[0]
    assert "await _cron_watchdog_completion(self.env)" in scheduled
    assert scheduled.index("await _cron_watchdog_completion") > \
        scheduled.index("await _send_feedback_emails")
    assert 'compatibility_date = "2026-07-23"' in WRANGLER_TEXT
    assert 'name = "FORKMESH_CRON_WATCHDOG"' in WRANGLER_TEXT
    assert 'class_name = "ForkMeshCronWatchdog"' in WRANGLER_TEXT
    assert 'tag = "v13"' in WRANGLER_TEXT
    assert 'new_sqlite_classes = ["ForkMeshCronWatchdog"]' in WRANGLER_TEXT
