"""Browser-error persistence and administrator World HUD contracts."""

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
REPORTER = (ROOT / "public" / "posthog.js").read_text(encoding="utf-8")
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
WORLD_CSS = (ROOT / "public" / "world" / "world.css").read_text(
    encoding="utf-8")


def _client_error_fields():
    tree = ast.parse(ENTRY)
    names = {
        "CLIENT_ERROR_SURFACES",
        "CLIENT_ERROR_KINDS",
    }
    body = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id in names
            for target in node.targets
        ):
            body.append(node)
        elif (
            isinstance(node, ast.FunctionDef)
            and node.name in {
                "_sanitize_client_error_text",
                "_client_error_fields",
            }
        ):
            body.append(node)
    namespace = {"re": re}
    exec(compile(ast.Module(body=body, type_ignores=[]), "<entry>", "exec"),
         namespace)
    return namespace["_client_error_fields"]


def test_uncaught_errors_and_rejections_use_the_private_collector():
    assert 'window.addEventListener(\n    "error"' in REPORTER
    assert 'window.addEventListener("unhandledrejection"' in REPORTER
    assert 'const CLIENT_ERROR_ENDPOINT = "/api/client-errors"' in REPORTER
    assert 'credentials: "same-origin"' in REPORTER
    assert "keepalive: true" in REPORTER
    assert "recentClientErrors" in REPORTER
    assert "[redacted-secret]" in REPORTER
    assert "[redacted-email]" in REPORTER
    assert "[redacted-id]" in REPORTER
    assert "[redacted-url]" in REPORTER


def test_client_collector_is_bounded_redacted_and_stored_in_error_log():
    assert "async def client_error_handler(env, request):" in ENTRY
    assert "_request_same_origin(request)" in ENTRY
    assert "CLIENT_ERROR_MAX_BODY" in ENTRY
    assert "CLIENT_ERROR_RATE_PER_CLIENT" in ENTRY
    assert "CLIENT_ERROR_RATE_GLOBAL" in ENTRY
    assert "await _write_error_log(" in ENTRY
    assert '"/client-error/" + surface' in ENTRY
    assert 'if url.path in ("/api/client-errors", "/api/client-errors/"):' in ENTRY


def test_client_errors_are_identified_as_javascript_in_admin():
    assert "def _admin_error_source(method, path):" in ENTRY
    assert 'str(method or "").strip().upper() == "JS"' in ENTRY
    assert 'str(path or "").startswith("/client-error/")' in ENTRY
    assert 'return "JavaScript" if is_javascript else "Worker"' in ENTRY
    assert 'class="error-source %s"' in ENTRY


def test_client_error_fields_remove_identity_urls_and_credentials():
    fields = _client_error_fields()({
        "kind": "error",
        "surface": "world",
        "message": (
            "token=super-secret-value user@example.com "
            "https://forkmesh.com/alice/private-repo?token=abc"),
        "stack": (
            "at run (https://forkmesh.com/alice/private-repo/world.js:44:2)"),
        "source": "https://forkmesh.com/private/path/world.js?account=alice",
        "line": 44,
        "column": 2,
    })
    assert fields is not None
    surface, detail = fields
    assert surface == "world"
    assert "super-secret-value" not in detail
    assert "user@example.com" not in detail
    assert "alice/private-repo" not in detail
    assert "world.js:44:2" in detail


def test_admin_hud_counts_only_rows_after_the_local_seen_cursor():
    assert "async def world_admin_errors_handler(env, request):" in ENTRY
    assert '"platform_administrator"' in ENTRY
    assert '"SELECT COUNT(*) AS n FROM error_log WHERE id>?"' in ENTRY
    assert '"/api/world/admin/errors"' in ENTRY
    assert "ADMIN_ERROR_SEEN_KEY" in WORLD
    assert "seen === null" in WORLD
    assert "this.storeAdminErrorSeenId(this.adminErrorLatestId)" in WORLD
    assert "data-world-admin-error-count" in WORLD
    assert "this.identity?.isAdmin !== true" in WORLD
    assert "world-admin-error-arrival" in WORLD_CSS
    assert "prefers-reduced-motion: reduce" in WORLD_CSS
