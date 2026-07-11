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


def test_admin_accounts_table_can_migrate_account_kind():
    assert "def _admin_account_migration_cell" in ENTRY_TEXT
    assert 'name="account_migration"' in ENTRY_TEXT
    assert 'action="migrate_account"' in ENTRY_TEXT
    assert "elif action == \"migrate_account\":" in ENTRY_TEXT
    assert "_admin_migrate_account_kind(" in ENTRY_TEXT
    assert "event.submitter" in ENTRY_TEXT


def test_admin_accounts_table_has_migrate_verified_users_button():
    # One-click bulk migration of verified-email accounts into the users table.
    assert "def _admin_migrate_verified_users" in ENTRY_TEXT
    assert 'action="migrate_verified_users"' in ENTRY_TEXT
    assert "elif action == \"migrate_verified_users\":" in ENTRY_TEXT
    assert "Migrate all verified-email users into users" in ENTRY_TEXT
    # Making a user/node drains the legacy accounts row.
    assert "DELETE FROM accounts WHERE name_bi=?" in ENTRY_TEXT
