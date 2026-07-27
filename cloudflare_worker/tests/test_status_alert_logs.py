#!/usr/bin/env python3
"""Bounded Cloudflare Workers Logs excerpts in non-green alert email."""

import ast
import asyncio
import json
from pathlib import Path
import re
from types import SimpleNamespace
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _load_helpers(events=None, status=200):
    wanted_constants = {
        "CLOUDFLARE_ATTENTION_LOG_WINDOW_MS",
        "CLOUDFLARE_ATTENTION_LOG_LIMIT",
        "CLOUDFLARE_ATTENTION_LOG_MAX_CHARS",
    }
    wanted_functions = {
        "_sanitize_attention_log_text",
        "_cloudflare_attention_log_tail",
        "_attention_email_with_logs",
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
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and \
                node.name in wanted_functions:
            selected.append(node)

    calls = []

    class Response:
        def __init__(self):
            self.status = status

        async def text(self):
            return json.dumps({
                "result": {"events": {"events": events or []}},
            })

    async def js_fetch_with_timeout(url, options, timeout):
        calls.append((url, options, timeout))
        return Response()

    def clean_string(value, maximum):
        return str(value or "")[:maximum]

    namespace = {
        "json": json,
        "re": re,
        "quote": quote,
        "clean_string": clean_string,
        "js_fetch_with_timeout": js_fetch_with_timeout,
        "_html_escape": lambda value: (
            str(value).replace("&", "&amp;").replace("<", "&lt;")),
    }
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    namespace["_calls"] = calls
    return namespace


def test_attention_log_query_is_exactly_two_minutes_bounded_and_redacted():
    now = 2_100_000_000_000
    ns = _load_helpers(events=[{
        "timestamp": now - 1_000,
        "$metadata": {
            "level": "error",
            "origin": "scheduled",
            "message": (
                "GET https://forkmesh.com/private?token=visible "
                "authorization: Bearer abc.def password=hunter2"),
        },
        "$workers": {"scriptName": "forkmesh-relay"},
    }])
    env = SimpleNamespace(
        WORKERS_OBSERVABILITY_ACCOUNT_ID="account-id",
        WORKERS_OBSERVABILITY_API_TOKEN="scoped-token",
        WORKERS_OBSERVABILITY_SERVICE="forkmesh-relay",
    )
    tail = asyncio.run(ns["_cloudflare_attention_log_tail"](env, now))
    assert "ERROR scheduled" in tail
    assert "abc.def" not in tail
    assert "hunter2" not in tail
    assert "token=visible" not in tail
    assert "[redacted]" in tail

    url, options, timeout = ns["_calls"][0]
    assert url.endswith(
        "/accounts/account-id/workers/observability/telemetry/query")
    assert timeout == 8
    assert options["redirect"] == "error"
    assert options["headers"]["authorization"] == "Bearer scoped-token"
    query = json.loads(options["body"])
    assert query["timeframe"] == {"from": now - 120_000, "to": now}
    assert query["view"] == "events"
    assert query["limit"] == 50
    assert query["parameters"]["filters"][0]["value"] == "forkmesh-relay"


def test_attention_email_survives_missing_or_failed_log_access():
    ns = _load_helpers(status=503)
    missing = asyncio.run(
        ns["_cloudflare_attention_log_tail"](SimpleNamespace(), 123_000))
    assert "configure" in missing
    failed = asyncio.run(ns["_cloudflare_attention_log_tail"](
        SimpleNamespace(
            WORKERS_OBSERVABILITY_ACCOUNT_ID="account-id",
            WORKERS_OBSERVABILITY_API_TOKEN="token",
        ),
        123_000,
    ))
    assert "still delivered" in failed

    text, html = ns["_attention_email_with_logs"](
        "alert text",
        "<html><body><div><div>alert</div></div></body></html>",
        failed,
    )
    assert "prior 2 minutes" in text
    assert "still delivered" in text
    assert "<pre" in html
    assert html.endswith("</body></html>")


def test_only_non_green_component_mail_receives_cloudflare_log_tail():
    transition = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _record_status_monitor_transitions"):
        ENTRY_TEXT.index("\ndef _status_expected_checks_for_hour")
    ]
    assert 'if any(not alert["is_up"] for alert in pending)' in transition
    assert 'if not alert["is_up"]:' in transition
    assert "_attention_email_with_logs(" in transition
    watchdog = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _send_cron_watchdog_email"):
        ENTRY_TEXT.index("\n\nasync def _cron_watchdog_completion")
    ]
    assert "if not recovered:" in watchdog
    assert "_cloudflare_attention_log_tail(env, now)" in watchdog
