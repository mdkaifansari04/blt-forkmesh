#!/usr/bin/env python3
"""Sentry reporting contracts for the Cloudflare Python worker."""

import ast
import asyncio
import traceback
import tomllib
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WRANGLER = ROOT / "wrangler.toml"
CI_WORKFLOW = ROOT.parent / ".forkmesh" / "ci.yml"
DEPLOY_WORKFLOW = ROOT.parent / ".forkmesh" / "deploy.yml"
DEPLOY_SH = ROOT / "deploy.sh"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WRANGLER_DATA = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
CI_WORKFLOW_TEXT = CI_WORKFLOW.read_text(encoding="utf-8")
DEPLOY_WORKFLOW_TEXT = DEPLOY_WORKFLOW.read_text(encoding="utf-8")
DEPLOY_SH_TEXT = DEPLOY_SH.read_text(encoding="utf-8")


def _load_sentry_dsn_parts():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if "SENTRY_CLIENT" in targets:
                selected.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name == "_sentry_dsn_parts":
            selected.append(node)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = {"urlparse": urlparse}
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns["_sentry_dsn_parts"]


def _load_capture_worker_exception():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    want = {
        "method_name",
        "_sentry_header",
        "_sentry_safe_url",
        "_safe_error_text",
        "capture_worker_exception",
    }
    selected = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in want:
            selected.append(node)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    calls = []

    def _boom(*args, **kwargs):
        calls.append(("dsn", args, kwargs))
        raise RuntimeError("dsn read failed")

    async def _capture(*args, **kwargs):
        calls.append(("sentry", args, kwargs))
        raise RuntimeError("sentry failed")

    async def _write(*args, **kwargs):
        calls.append(("write", args, kwargs))
        raise RuntimeError("d1 failed")

    def _console(message):
        calls.append(("console", str(message), {}))

    ns = {
        "traceback": traceback,
        "_sentry_dsn_parts": _boom,
        "capture_sentry_error": _capture,
        "_write_error_log": _write,
        "_console_error": _console,
        "calls": calls,
    }
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


def test_sentry_uses_vanilla_worker_fetch_not_sdk_plugin():
    assert "sentry_sdk" not in ENTRY_TEXT
    assert "capture_sentry_error" in ENTRY_TEXT
    assert "application/x-sentry-envelope" in ENTRY_TEXT
    assert "x-sentry-auth" in ENTRY_TEXT
    assert "from js import fetch as js_fetch" in ENTRY_TEXT


def test_sentry_dsn_parser_builds_envelope_endpoint():
    parse = _load_sentry_dsn_parts()
    parts = parse("https://public@example.ingest.sentry.io/12345")
    assert parts["endpoint"] == "https://example.ingest.sentry.io/api/12345/envelope/"
    assert "sentry_key=public" in parts["auth"]
    assert "sentry_version=7" in parts["auth"]

    self_hosted = parse("https://pub@sentry.example.com/prefix/987")
    assert self_hosted["endpoint"] == "https://sentry.example.com/prefix/api/987/envelope/"


def test_worker_error_log_reports_to_sentry_and_preserves_d1_log():
    assert "await capture_sentry_error(" in ENTRY_TEXT
    assert "INSERT INTO error_log" in ENTRY_TEXT
    assert "request=request" in ENTRY_TEXT
    assert "error=error" in ENTRY_TEXT
    assert '"cloudflare": cf_context' in ENTRY_TEXT


def test_worker_1101_exceptions_are_captured_then_reraised():
    assert "async def capture_worker_exception" in ENTRY_TEXT
    assert 'cloudflare_error_code="1101"' in ENTRY_TEXT
    assert '"cloudflare.error_code"] = str(cloudflare_error_code)' in ENTRY_TEXT
    assert '"cloudflare.ray_id"] = str(ray)' in ENTRY_TEXT
    assert "await capture_worker_exception(self.env, request, url, error)" in ENTRY_TEXT
    assert "raise\n" in ENTRY_TEXT
    assert 'return json_response({"error": "internal_error"}, status=500)' not in ENTRY_TEXT
    assert "Re-raise so Cloudflare records the native Worker failure/Error 1101" in ENTRY_TEXT


def test_worker_exception_capture_does_not_raise_from_reporting_failures():
    ns = _load_capture_worker_exception()

    class HostileEnv:
        @property
        def SENTRY_DSN(self):
            raise RuntimeError("env unavailable")

    class HostileRequest:
        @property
        def headers(self):
            raise RuntimeError("headers unavailable")

        @property
        def method(self):
            raise RuntimeError("method unavailable")

        @property
        def url(self):
            raise RuntimeError("url unavailable")

    class HostileUrl:
        @property
        def path(self):
            raise RuntimeError("path unavailable")

    class HostileError(Exception):
        def __repr__(self):
            raise RuntimeError("repr unavailable")

        def __str__(self):
            raise RuntimeError("str unavailable")

    asyncio.run(ns["capture_worker_exception"](
        HostileEnv(), HostileRequest(), HostileUrl(), HostileError()))

    call_names = [item[0] for item in ns["calls"]]
    assert "sentry" in call_names
    assert "write" in call_names


def test_background_tasks_observe_exceptions_instead_of_default_handler():
    assert "def _fire_and_forget" in ENTRY_TEXT
    assert "def _consume_background_task" in ENTRY_TEXT
    assert "task.result()" in ENTRY_TEXT
    ensure_future_lines = [
        line.strip()
        for line in ENTRY_TEXT.splitlines()
        if "asyncio.ensure_future(" in line
    ]
    assert ensure_future_lines == ["task = asyncio.ensure_future(coro)"]
    assert '_fire_and_forget(self._stream_watchdog(req_id), "git stream watchdog")' in ENTRY_TEXT
    assert '"release download log"' in ENTRY_TEXT


def test_sentry_request_metadata_is_allowlisted_and_sanitized():
    assert 'for name in ("host", "user-agent", "accept", "cf-ray")' in ENTRY_TEXT
    assert '"url": _sentry_safe_url(request)' in ENTRY_TEXT
    assert "return parsed.scheme + \"://\" + parsed.netloc + (parsed.path or \"/\")" in ENTRY_TEXT
    request_payload = ENTRY_TEXT.split("def _sentry_request_payload", 1)[1] \
        .split("def _sentry_cloudflare_context", 1)[0]
    assert '"authorization"' not in request_payload.lower()
    assert '"cookie"' not in request_payload.lower()


def test_simulate_sentry_error_route_is_worker_owned_and_raises():
    run_worker_first = WRANGLER_DATA["assets"]["run_worker_first"]
    assert "/simulate-sentry-error" in run_worker_first
    assert 'url.path in ("/simulate-sentry-error", "/simulate-sentry-error/")' in ENTRY_TEXT
    assert "division_by_zero = 1 / 0" in ENTRY_TEXT


def test_forkmesh_deploy_pushes_sentry_dsn_secret():
    assert "SENTRY_DSN=${{ vars.SENTRY_DSN }}" in DEPLOY_WORKFLOW_TEXT
    assert "\n        push_secrets\n" in DEPLOY_SH_TEXT


def test_forkmesh_actions_run_full_worker_pytest_suite():
    assert "python3 -m pip install --user pytest" in CI_WORKFLOW_TEXT
    assert "python3 -m pytest cloudflare_worker/tests" in CI_WORKFLOW_TEXT
    assert "python3 -m pip install --user pytest" in DEPLOY_WORKFLOW_TEXT
    assert "python3 -m pytest cloudflare_worker/tests" in DEPLOY_WORKFLOW_TEXT
    assert "python3 cloudflare_worker/tests/test_crypto.py" not in CI_WORKFLOW_TEXT
    assert "python3 cloudflare_worker/tests/test_crypto.py" not in DEPLOY_WORKFLOW_TEXT


def test_worker_observability_exports_logs_and_traces_to_sentry_destinations():
    observability = WRANGLER_DATA["observability"]
    assert observability["enabled"] is True
    assert observability["head_sampling_rate"] == 1

    logs = observability["logs"]
    assert logs["enabled"] is True
    assert logs["head_sampling_rate"] == 1
    assert logs["invocation_logs"] is True
    assert logs["destinations"] == ["sentry-logs"]

    traces = observability["traces"]
    assert traces["enabled"] is True
    assert traces["head_sampling_rate"] == 1
    assert traces["destinations"] == ["sentry-traces"]
