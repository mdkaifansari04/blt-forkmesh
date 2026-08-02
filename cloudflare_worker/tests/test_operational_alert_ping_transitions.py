#!/usr/bin/env python3
"""Outage pings survive alongside recovery pings.

Pings are on by default and alert mail is not, so the ping channel needs its
own delivered-transition marker. Sharing the mail channel's notified_state left
every sample after a transition re-announcing it: recoveries key off a stamp
that moves each minute, so a new "recovered" row was written every cron tick
until the per-recipient cap trimmed the outage pings out of the inbox.
"""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

SYSTEMS = [("edge_api", "Edge API"), ("flagship_repository", "Flagship repo")]


def _load(pings_enabled=True):
    wanted_constants = {"STATUS_DEPLOY_GRACE_MS"}
    wanted_functions = {
        "_record_status_monitor_transitions",
        "_enqueue_operational_alert_pings",
        "_record_operational_alert_pings_sent",
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

    monitors = {}
    pings = []

    async def d1_all(_env, sql, *_args):
        if "FROM users" in sql:
            return [{"data": "admin"}]
        if "FROM repository_monitor_state" in sql:
            return [dict(row) for row in monitors.values()]
        return []

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO repository_monitor_state"):
            for index in range(0, len(args), 8):
                chunk = args[index:index + 8]
                monitors[chunk[0]] = {
                    "monitor_id": chunk[0], "is_up": chunk[1],
                    "changed_at": chunk[2], "outage_started_at": chunk[3],
                    "checked_at": chunk[4], "reason": chunk[5],
                    "notified_state": chunk[6], "pinged_state": chunk[7],
                }
            return None
        if "SET pinged_state" in sql:
            state, monitor_id, is_up = args
            row = monitors.get(monitor_id)
            if row and int(row["is_up"]) == int(is_up):
                row["pinged_state"] = state
            return None
        return None

    async def enqueue_notification(_env, recipient, kind, title, **kwargs):
        pings.append({
            "recipient": recipient, "kind": kind, "title": title,
            "dedupe": kwargs.get("dedupe", ""),
            "state": (kwargs.get("meta") or {}).get("state", ""),
        })
        return True

    async def decrypt_row(_env, _data):
        return {"name": "admin"}

    async def _status_alert_pings_enabled(_env):
        return pings_enabled

    async def _status_alert_emails_enabled(_env):
        return False

    namespace = {
        "d1_all": d1_all,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "enqueue_notification": enqueue_notification,
        "clean_string": lambda value, size: str(value or "")[:size],
        "valid_node_name": lambda value: bool(value),
        "MAX_NODE_NAME": 40,
        "STATUS_SYSTEMS": SYSTEMS,
        "STATUS_MIRROR_PREFIX": "mirror:",
        "_status_alert_pings_enabled": _status_alert_pings_enabled,
        "_status_alert_emails_enabled": _status_alert_emails_enabled,
    }
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace, monitors, pings


def _sample(namespace, now, down=(), reason=""):
    ok = {system: system not in down for system, _ in SYSTEMS}
    reasons = {system: reason for system in down}
    asyncio.run(namespace["_record_status_monitor_transitions"](
        SimpleNamespace(), ok, reasons, now, SYSTEMS))


def test_one_ping_per_transition_in_both_directions():
    namespace, monitors, pings = _load()
    minute = 60_000

    _sample(namespace, 10 * minute)
    assert pings == []

    _sample(namespace, 11 * minute, down=("edge_api",), reason="502 from edge")
    assert [(ping["state"], ping["title"]) for ping in pings] == [
        ("down", "Edge API needs attention")]
    assert monitors["status:edge_api"]["pinged_state"] == "down"

    # A sustained outage repeats the same sample, not a new ping.
    _sample(namespace, 12 * minute, down=("edge_api",), reason="502 from edge")
    _sample(namespace, 13 * minute, down=("edge_api",), reason="502 from edge")
    assert len(pings) == 1

    _sample(namespace, 14 * minute)
    assert [(ping["state"], ping["title"]) for ping in pings] == [
        ("down", "Edge API needs attention"),
        ("up", "Edge API recovered"),
    ]
    assert monitors["status:edge_api"]["pinged_state"] == "up"

    # The regression: every later green sample used to enqueue another
    # "recovered" ping under a fresh dedupe key, burying the outage ping.
    for tick in range(15, 25):
        _sample(namespace, tick * minute)
    assert len(pings) == 2
    assert len({ping["dedupe"] for ping in pings}) == 2


def test_ping_state_is_only_recorded_once_delivered():
    namespace, monitors, pings = _load(pings_enabled=False)
    _sample(namespace, 10 * 60_000)
    _sample(namespace, 11 * 60_000, down=("edge_api",), reason="502")
    assert pings == []
    # Nothing was delivered, so turning Pings on later still announces what is
    # red instead of the switch silently swallowing the current outage.
    assert monitors["status:edge_api"]["pinged_state"] == ""


def test_flagship_grace_defers_the_ping_until_the_failure_is_sustained():
    namespace, monitors, pings = _load()
    grace = namespace["STATUS_DEPLOY_GRACE_MS"]
    _sample(namespace, 10 * 60_000)
    _sample(namespace, 11 * 60_000, down=("flagship_repository",))
    assert pings == []

    # Recovering inside the grace window must not ping a recovery for an
    # outage that was never announced.
    _sample(namespace, 12 * 60_000)
    assert pings == []
    assert monitors["status:flagship_repository"]["pinged_state"] == "up"

    _sample(namespace, 13 * 60_000, down=("flagship_repository",))
    _sample(namespace, 13 * 60_000 + grace, down=("flagship_repository",))
    assert [ping["state"] for ping in pings] == ["down"]
