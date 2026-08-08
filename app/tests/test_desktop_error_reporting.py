#!/usr/bin/env python3
"""Desktop-app error reports become operational rows, and so become pings.

A warning dialog on a machine nobody is sitting in front of, or an error toast
on a headless mirror, used to be the entire record of a failure: nothing was
written anywhere central, so nothing pinged. POST /api/desktop-errors puts those
reports in the same error_log as browser and Worker errors, which raises the
existing "new error group" ping on the first sighting (adhoc #1538).

Two properties matter enough to pin: an unsigned report is refused (the endpoint
must not become a second anonymous write path into D1), and a report is bounded
and redacted exactly the way the browser collector's are.
"""

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
ADMIN = (ROOT / "src" / "admin_console.py").read_text(encoding="utf-8")
WORLD = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text(encoding="utf-8")
WORLD_CSS = (ROOT.parent / "world" / "public" / "world" / "world.css").read_text(
    encoding="utf-8")
QT_REPORTS_HEADER = (
    ROOT.parent / "desktop" / "src" / "ClientErrorReports.h"
).read_text(encoding="utf-8")
QT_REPORTS = (
    ROOT.parent / "desktop" / "src" / "ClientErrorReports.cpp"
).read_text(encoding="utf-8")
QT_EVENT_FILTER = (
    ROOT.parent / "desktop" / "src" / "MainWindowIssues.cpp"
).read_text(encoding="utf-8")
QT_MESSAGES = (
    ROOT.parent / "desktop" / "src" / "MainWindowMessages.cpp"
).read_text(encoding="utf-8")
QT_SETTINGS = (
    ROOT.parent / "desktop" / "src" / "MainWindowSettings.cpp"
).read_text(encoding="utf-8")


def _desktop_error_fields():
    """Run the real validator with nothing but its own dependencies."""
    tree = ast.parse(ENTRY)
    names = {"DESKTOP_ERROR_SURFACES", "DESKTOP_ERROR_KINDS"}
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
                "_desktop_error_fields",
            }
        ):
            body.append(node)
    namespace = {"re": re}
    exec(compile(ast.Module(body=body, type_ignores=[]), "<entry>", "exec"),
         namespace)
    return namespace["_desktop_error_fields"]


def test_the_collector_is_signed_bounded_and_stored_in_error_log():
    assert "async def desktop_error_handler(env, request):" in ENTRY
    assert 'if url.path in ("/api/desktop-errors", "/api/desktop-errors/"):' in ENTRY
    assert "return await desktop_error_handler(self.env, request)" in ENTRY
    # Signed owner/ts/sig proof — the same one GET /api/sync requires.
    assert "if not owner or not await _authorize_owner(env, request, owner):" in ENTRY
    assert "DESKTOP_ERROR_MAX_BODY" in ENTRY
    assert "DESKTOP_ERROR_RATE_PER_OWNER" in ENTRY
    assert "DESKTOP_ERROR_RATE_GLOBAL" in ENTRY
    assert '"/desktop-error/" + surface' in ENTRY
    body = ENTRY[ENTRY.index("async def desktop_error_handler(env, request):"):]
    body = body[:body.index("async def world_admin_errors_handler(")]
    # Cheap shape checks before the signature check, and the signature check
    # before any bounded-row read.
    assert body.index('method_name(request) != "POST"') < body.index(
        "_authorize_owner(")
    assert body.index("_authorize_owner(") < body.index("SELECT COUNT(*)")
    assert body.count("AND method='APP'") == 3  # per-owner, global, duplicate
    assert '"app:" + str(owner_hash)[:32]' in body
    assert "await _write_error_log(" in body
    assert "521," in body and '"APP",' in body
    # An unauthorized or over-quota report is never a new user-visible error.
    assert body.count('{"ok": True, "stored": False}') == 2
    assert "except Exception:" in body


def test_a_report_names_its_kind_and_install_but_no_free_text_route():
    fields = _desktop_error_fields()
    assert fields({"kind": "dialog", "surface": "app", "message": "boom"}) == (
        "app", "desktop dialog [app] boom")
    assert fields(
        {"kind": "toast", "surface": "headless", "title": "Sync inbox",
         "message": "relay is rate-limited"}
    ) == (
        "headless",
        "desktop toast [headless] Sync inbox — relay is rate-limited")
    # Unknown areas and kinds are refused rather than becoming their own group.
    assert fields({"kind": "dialog", "surface": "../admin", "message": "x"}) is None
    assert fields({"kind": "chatter", "surface": "app", "message": "x"}) is None
    assert fields({"kind": "dialog", "surface": "app", "message": " "}) is None
    assert fields("not a payload") is None


def test_a_report_cannot_carry_identity_paths_or_credentials():
    surface, detail = _desktop_error_fields()({
        "kind": "dialog",
        "surface": "app",
        "title": "Push to alice@example.com",
        "message": (
            "token=super-secret-value could not push "
            "https://forkmesh.com/alice/private-repo "
            "from /home/alice/work/private-repo"),
    })
    assert surface == "app"
    assert "super-secret-value" not in detail
    assert "alice@example.com" not in detail
    assert "forkmesh.com" not in detail
    assert "/home/alice" not in detail
    assert "could not push" in detail


def test_desktop_rows_read_apart_from_browser_and_worker_rows():
    assert 'str(method or "").strip().upper() == "APP"' in ADMIN
    assert 'str(path or "").startswith("/desktop-error/")' in ADMIN
    assert 'return "Desktop"' in ADMIN
    assert ".error-source.desktop{" in ADMIN
    assert 'item.method === "APP"' in WORLD
    assert 'String(item.path || "").startsWith("/desktop-error/")' in WORLD
    assert '"Desktop"' in WORLD
    assert ".world-error-source--desktop {" in WORLD_CSS


def test_the_desktop_client_reports_every_error_surface_once():
    # The modal path is caught application-wide, so no call site has to remember.
    assert "qobject_cast<QMessageBox *>(obj)" in QT_EVENT_FILTER
    assert "icon == QMessageBox::Warning" in QT_EVENT_FILTER
    assert "icon == QMessageBox::Critical" in QT_EVENT_FILTER
    # A warning that asks something is a confirmation prompt, not a failure.
    assert "const bool informational =" in QT_EVENT_FILTER
    assert "~(QMessageBox::Ok | QMessageBox::Close)" in QT_EVENT_FILTER
    assert 'reportUserVisibleError(QStringLiteral("dialog")' in QT_EVENT_FILTER
    # The toast path reports from flashMessage, the half that records the
    # failure, not from showTopMessage, the half that paints a bubble: a headless
    # node has no toast widget at all (and its failures are the invisible ones),
    # while the logged-error hook paints its own cards through showTopMessage and
    # must not report a line a second time.
    flash = QT_SETTINGS[QT_SETTINGS.index("void MainWindow::flashMessage("):]
    show = flash[flash.index("void MainWindow::showTopMessage("):]
    flash = flash[:flash.index("void MainWindow::showTopMessage(")]
    assert 'reportUserVisibleError(QStringLiteral("toast")' in flash
    assert 'reportUserVisibleError(QStringLiteral("toast")' not in show
    assert "if (!m_topMessage)" not in flash
    assert "/api/desktop-errors" in QT_MESSAGES
    assert "signedInboxQuery(owner)" in QT_MESSAGES
    assert "hostInCooldown(url.host())" in QT_MESSAGES


def test_the_client_bounds_and_redacts_before_anything_leaves_the_machine():
    assert "kMaxPerWindow = 8" in QT_REPORTS_HEADER
    assert "kMaxDeferred = 8" in QT_REPORTS_HEADER
    assert "kMaxAttempts = 3" in QT_REPORTS_HEADER
    assert 'QStringLiteral("diagnostics/reportErrors")' in QT_REPORTS_HEADER
    for placeholder in ("<secret>", "<email>", "<url>", "<id>", "<path>"):
        assert placeholder in QT_REPORTS
