#!/usr/bin/env python3
"""Contract tests for the founders-outreach email console (/outreach).

Static AST/text checks over src/entry.py + the public page, in the same style
as test_admin_page.py: no Workers JS runtime is required.
"""

import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
SCHEMA_TEXT = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
MODULE = ast.parse(ENTRY_TEXT)


def _async_func(name):
    for node in ast.walk(MODULE):
        if isinstance(node, ast.AsyncFunctionDef) and node.name == name:
            return node
    raise AssertionError(name + " was not found in entry.py")


def _calls(node):
    return {
        call.func.id
        for call in ast.walk(node)
        if isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
    }


def _load_templates():
    body = [
        node for node in MODULE.body
        if isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in
            ("OUTREACH_TEMPLATES", "OUTREACH_FROM_EMAIL", "OUTREACH_DAILY_LIMIT")
            for t in node.targets
        )
    ]
    module = ast.fix_missing_locations(ast.Module(body=body, type_ignores=[]))
    namespace = {}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def test_schema_has_outreach_tables():
    assert "CREATE TABLE IF NOT EXISTS outreach_team" in SCHEMA_TEXT
    assert "CREATE TABLE IF NOT EXISTS outreach_log" in SCHEMA_TEXT
    assert "idx_outreach_log_sender_ts" in SCHEMA_TEXT


def test_schema_has_enable_outreach_column():
    # Fresh databases get the per-user access flag from ensure_schema; existing
    # ones get it from the ALTER statements (migration 0040).
    assert "enable_outreach INTEGER NOT NULL DEFAULT 0" in SCHEMA_TEXT
    assert (
        "ALTER TABLE users ADD COLUMN enable_outreach INTEGER NOT NULL DEFAULT 0"
        in ENTRY_TEXT)


def test_outreach_handler_honors_enable_outreach_flag():
    handler = _async_func("outreach_handler")
    calls = _calls(handler)
    # The per-user enable_outreach column grants access alongside admin/roster.
    assert "_outreach_enabled" in calls
    reader = _async_func("_outreach_enabled")
    text = ast.get_source_segment(ENTRY_TEXT, reader)
    assert "SELECT enable_outreach FROM users WHERE user_bi=?" in text
    assert "FROM accounts" not in text


def test_outreach_routes_are_wired():
    assert 'url.path == "/api/outreach" or url.path.startswith("/api/outreach/")' in ENTRY_TEXT
    assert "await outreach_handler(self.env, request)" in ENTRY_TEXT


def test_outreach_handler_requires_session_and_gates_roles():
    handler = _async_func("outreach_handler")
    calls = _calls(handler)
    # A signed account session is mandatory before any route logic runs, and
    # the admin/member gates are computed from it.
    assert "_account_session_record" in calls
    assert "_is_admin" in calls
    assert "_outreach_member" in calls
    text = ast.get_source_segment(ENTRY_TEXT, handler)
    assert '"/api/outreach/send"' in text
    assert '"/api/outreach/team"' in text
    # Team management is admin-only; sending needs admin OR team membership.
    assert "admin_required" in text
    assert "forbidden" in text


def test_outreach_send_caps_volume_and_audits():
    send = _async_func("_outreach_send")
    calls = _calls(send)
    text = ast.get_source_segment(ENTRY_TEXT, send)
    assert "_outreach_sent_today" in calls
    assert "OUTREACH_DAILY_LIMIT" in text
    # The audit row's recipient/subject are encrypted at rest.
    assert "encrypt_row" in calls
    assert "INSERT INTO outreach_log" in text
    # Mail goes out from the founders address override, not the default sender.
    assert "_outreach_from" in calls
    assert "from_email=from_email" in text


def test_send_email_supports_sender_override():
    send = _async_func("_send_email")
    args = [a.arg for a in send.args.args + send.args.kwonlyargs]
    assert "from_email" in args
    assert "from_name" in args


def test_outreach_templates_cover_sponsorship():
    ns = _load_templates()
    assert ns["OUTREACH_FROM_EMAIL"] == "founders@forkmesh.com"
    assert ns["OUTREACH_DAILY_LIMIT"] > 0
    keys = [t["key"] for t in ns["OUTREACH_TEMPLATES"]]
    assert "sponsorship" in keys
    assert "investor" in keys
    assert "blank" in keys
    for template in ns["OUTREACH_TEMPLATES"]:
        assert set(template) == {"key", "label", "subject", "body"}


def test_outreach_page_exists_and_is_routed():
    html = (ROOT / "public" / "outreach.html").read_text(encoding="utf-8")
    assert "/outreach.js" in html
    assert 'id="or-compose"' in html
    assert 'id="or-team"' in html
    # Outreach tracker: a list of sent emails on the left, a status pipeline
    # keyed by template type on the right.
    assert 'id="or-log-list"' in html
    assert 'id="or-pipeline"' in html
    js = (ROOT / "public" / "outreach.js").read_text(encoding="utf-8")
    assert "/api/outreach" in js
    assert "forkmesh.session" in js
    assert "renderPipeline" in js
    redirects = (ROOT / "public" / "_redirects").read_text(encoding="utf-8")
    assert "/outreach /outreach.html 200" in redirects
