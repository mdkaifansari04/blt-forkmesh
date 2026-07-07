#!/usr/bin/env python3
"""Sentry reporting contracts for the Cloudflare Python worker."""

import ast
import tomllib
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WRANGLER = ROOT / "wrangler.toml"
DEPLOY_WORKFLOW = ROOT.parent / ".forkmesh" / "deploy.yml"
DEPLOY_SH = ROOT / "deploy.sh"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WRANGLER_DATA = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
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
