import ast
from pathlib import Path


ENTRY_PATH = Path(__file__).resolve().parents[1] / "src" / "entry.py"
ENTRY_TEXT = ENTRY_PATH.read_text()


def _admin_function():
    module = ast.parse(ENTRY_PATH.read_text())
    for node in ast.walk(module):
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "_admin":
            return node
    raise AssertionError("Relay._admin was not found")


def test_admin_page_preserves_admin_query_local():
    admin = _admin_function()
    assignments = {
        target.id
        for node in ast.walk(admin)
        for target in getattr(node, "targets", [])
        if isinstance(target, ast.Name)
    }
    assert "admin_query" in assignments

    admin_query_assignments = [
        node
        for node in ast.walk(admin)
        if isinstance(node, ast.Assign)
        and any(isinstance(target, ast.Name) and target.id == "admin_query"
                for target in node.targets)
    ]
    assert any(
        isinstance(node.value, ast.Call)
        and isinstance(node.value.func, ast.Name)
        and node.value.func.id == "_admin_query"
        for node in admin_query_assignments
    )


def test_admin_page_requires_signed_login_cookie():
    module = ast.parse(ENTRY_TEXT)
    auth = next(
        node for node in ast.walk(module)
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_check_admin_page_auth"
    )
    calls = {
        node.func.id
        for node in ast.walk(auth)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "_is_admin" in calls
    assert "_admin_session_valid" in calls

    assert "Set-Cookie" in ENTRY_TEXT
    assert "_admin_session_cookie(env, payload[\"nodeName\"])" in ENTRY_TEXT
    assert "_clear_admin_session_cookie()" in ENTRY_TEXT
    assert 'url.path == "/api/accounts/logout"' in ENTRY_TEXT
    assert '\"location\": \"/login?next=\" + quote(next_path)' in ENTRY_TEXT


def test_every_web_logout_calls_the_logout_endpoint():
    # Logout is universal: every client logout goes through
    # POST /api/accounts/logout, because only the Worker can clear the
    # HttpOnly forkmesh_admin cookie. The marketing-page header logout used to
    # skip this and only clear localStorage, so a logged-out admin could still
    # open the admin page (adhoc #184).
    public = ENTRY_PATH.parents[1] / "public"
    for rel in ("site-header.js", "dashboard/js/02-helpers.js", "dashboard.js"):
        text = (public / rel).read_text()
        assert 'fetch("/api/accounts/logout", { method: "POST"' in text, rel


def test_admin_session_reissued_from_session_token():
    # A logged-in admin whose short-lived admin-page cookie has lapsed can
    # re-mint it from their still-valid account session token, so the admin
    # dashboard no longer bounces them to a password re-entry (adhoc #163).
    assert "async def _account_admin_session" in ENTRY_TEXT
    assert 'url.path == "/api/accounts/admin-session"' in ENTRY_TEXT
    assert "_account_session_token_name(env, token)" in ENTRY_TEXT
    assert "_admin_session_cookie(env, name)" in ENTRY_TEXT

    # The grant is gated on is_admin, never on a self-asserted name, and it
    # never mints a session token — it only trades an existing one for the cookie.
    module = ast.parse(ENTRY_TEXT)
    fn = next(
        node for node in ast.walk(module)
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_account_admin_session"
    )
    calls = {
        node.func.id
        for node in ast.walk(fn)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "_is_admin" in calls
    assert "_admin_session_cookie" in calls
    assert "_account_session_token" not in calls


def test_admin_page_auth_derives_admin_from_cookie():
    # The signed cookie is the source of truth for who the admin is; auth must
    # not require the ?admin= query param. Requiring it made a bare admin-path
    # visit always fail, and /login's silent admin-session resume then
    # redirect-looped between /login and the admin page forever (adhoc #168).
    module = ast.parse(ENTRY_TEXT)
    auth = next(
        node for node in ast.walk(module)
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_check_admin_page_auth"
    )
    calls = {
        node.func.id
        for node in ast.walk(auth)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "_admin_cookie_name" in calls
    assert "parse_qs" not in calls

    # The login page never auto-resumes into a redirect loop: a resume attempt
    # seconds ago that bounced back must fall through to the password form.
    login_js = (Path(__file__).resolve().parents[1]
                / "public" / "login.js").read_text()
    assert "forkmesh.adminResumeAt" in login_js


def test_admin_console_dropped_legacy_accounts_migration_tools():
    # The legacy accounts table was dropped (migration 0042), so the console's
    # accounts-drain tooling (Move to users/nodes buttons, bulk verified-user
    # migration) is gone with it and nothing touches the accounts table.
    assert "_admin_account_migration_cell" not in ENTRY_TEXT
    assert "_admin_migrate_account_kind" not in ENTRY_TEXT
    assert "_admin_migrate_verified_users" not in ENTRY_TEXT
    assert 'action="migrate_account"' not in ENTRY_TEXT
    assert 'action="migrate_verified_users"' not in ENTRY_TEXT
    assert "FROM accounts" not in ENTRY_TEXT
    assert "DELETE FROM accounts" not in ENTRY_TEXT


def test_admin_set_password_tool_lives_on_users_table():
    # The password-reset tool moved with the records to the users table view.
    assert 'if table == "users":' in ENTRY_TEXT
    assert 'action="set_password"' in ENTRY_TEXT
    assert "def _admin_set_password" in ENTRY_TEXT


def test_admin_resend_verify_tool_on_users_table():
    # The users table exposes a "Resend verify email" button that dispatches to
    # ?action=resend_verify and re-sends the confirmation link, falling back to
    # the pending_verifications queue when email is not configured.
    assert 'action="resend_verify"' in ENTRY_TEXT
    assert ">Resend verify email</button>" in ENTRY_TEXT
    assert "def _admin_resend_verification" in ENTRY_TEXT

    module = ast.parse(ENTRY_TEXT)
    fn = next(
        node for node in ast.walk(module)
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_admin_resend_verification"
    )
    calls = {
        node.func.id
        for node in ast.walk(fn)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "_account_row" in calls
    assert "_send_verification_email" in calls
    assert "_enqueue_verification" in calls

    # It is wired into the admin POST dispatcher alongside the other actions.
    admin = _admin_function()
    admin_calls = {
        node.func.id
        for node in ast.walk(admin)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }
    assert "_admin_resend_verification" in admin_calls
