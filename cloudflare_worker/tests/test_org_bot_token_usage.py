from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
DASHBOARD = (
    ROOT / "public" / "dashboard" / "js" / "04-account.js"
).read_text(encoding="utf-8")
MIGRATION = (
    ROOT / "migrations" / "0107_org_bot_token_usage.sql"
).read_text(encoding="utf-8")


def test_bot_usage_log_is_metadata_only_and_retained_with_a_bound():
    for source in (MIGRATION, SCHEMA):
        assert "CREATE TABLE IF NOT EXISTS org_bot_token_usage" in source
        for column in ("token_id", "org_bi", "provider", "action", "method", "used_at"):
            assert column in source
    columns = MIGRATION[
        MIGRATION.index("CREATE TABLE IF NOT EXISTS"):
        MIGRATION.index(");", MIGRATION.index("CREATE TABLE IF NOT EXISTS"))
    ].lower()
    for forbidden in ("secret", "request_body", "query_string", "ip_address", "user_agent"):
        assert forbidden not in columns
    assert "DELETE FROM org_bot_token_usage WHERE used_at<?" in ENTRY


def test_successful_bot_authentication_appends_usage_without_request_content():
    helper = ENTRY[
        ENTRY.index("async def _org_bot_token_context("):
        ENTRY.index("\n\nasync def org_bot_tokens_handler", ENTRY.index(
            "async def _org_bot_token_context("))
    ]
    assert "INSERT INTO org_bot_token_usage" in helper
    assert "urlparse(request.url).path" in helper
    assert "searchParams" not in helper
    assert "request.text" not in helper
    assert "user-agent" not in helper.lower()


def test_org_admin_token_list_and_page_include_recent_usage_preview():
    assert "ORDER BY used_at DESC,id DESC LIMIT 5" in ENTRY
    assert '"usagePreview": usage_preview' in ENTRY
    assert "Recent token use" in DASHBOARD
    assert "secrets, bodies, network addresses, and user agents are never logged" in DASHBOARD


def test_scoped_bot_tokens_can_read_and_write_only_their_organization_tasks():
    handler = ENTRY[
        ENTRY.index("async def organization_tasks_handler("):
        ENTRY.index("\n\nworld_office_marketing_tasks_handler", ENTRY.index(
            "async def organization_tasks_handler("))
    ]
    runtime = ENTRY[
        ENTRY.index("class _OfficeMarketingTasksRuntime:"):
        ENTRY.index("\n\nclass _ChatChannelsRuntime", ENTRY.index(
            "class _OfficeMarketingTasksRuntime:"))
    ]
    assert "invalid_bot_token" in handler
    assert 'authorization.lower().startswith("bearer fmbot_")' in handler
    assert '"organization.tasks.read"' in handler
    assert '"organization.tasks.write"' in handler
    assert '"requiredScope": required_scope' in handler
    assert "str(org_bi or \"\") != str(self.bot_context.get(\"orgBi\") or \"\")" in runtime
    assert 'return "admin", "maintain"' in runtime
