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


def test_admin_accounts_table_can_migrate_account_kind():
    assert "def _admin_account_migration_cell" in ENTRY_TEXT
    assert 'name="account_migration"' in ENTRY_TEXT
    assert 'action="migrate_account"' in ENTRY_TEXT
    assert "elif action == \"migrate_account\":" in ENTRY_TEXT
    assert "_admin_migrate_account_kind(" in ENTRY_TEXT
    assert "event.submitter" in ENTRY_TEXT


def test_admin_accounts_table_move_buttons_and_presence_indicator():
    # Phasing out the accounts table: "Move to users/nodes" (re)mirror the record
    # into the authoritative table and drain the legacy accounts row. Both buttons
    # stay active and an indicator shows when the record already lives there.
    assert "Move to users" in ENTRY_TEXT
    assert "Move to nodes" in ENTRY_TEXT
    # Presence indicators query the authoritative tables.
    assert "SELECT 1 FROM users WHERE user_bi=?" in ENTRY_TEXT
    assert "SELECT 1 FROM nodes WHERE node_bi=?" in ENTRY_TEXT
    assert 'class="inpill"' in ENTRY_TEXT
    # Buttons are always active — no disabled state on the migration buttons.
    cell = ENTRY_TEXT.split("async def _admin_account_migration_cell", 1)[1]
    cell = cell.split("\nasync def ", 1)[0]
    assert "disabled" not in cell


def test_admin_accounts_table_has_migrate_verified_users_button():
    # One-click bulk migration of verified-email accounts into the users table.
    assert "def _admin_migrate_verified_users" in ENTRY_TEXT
    assert 'action="migrate_verified_users"' in ENTRY_TEXT
    assert "elif action == \"migrate_verified_users\":" in ENTRY_TEXT
    assert "Migrate all verified-email users into users" in ENTRY_TEXT
    # Making a user/node drains the legacy accounts row.
    assert "DELETE FROM accounts WHERE name_bi=?" in ENTRY_TEXT
