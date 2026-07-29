"""Contracts for the authenticated ForkMesh Streamable HTTP MCP endpoint."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
DASHBOARD = (
    ROOT / "public" / "dashboard" / "js" / "04-account.js"
).read_text(encoding="utf-8")


def test_remote_mcp_is_routed_and_requires_org_bot_authentication():
    assert 'url.path in ("/mcp", "/mcp/")' in ENTRY
    assert "return await remote_mcp_handler(self.env, request)" in ENTRY
    handler = ENTRY[
        ENTRY.index("async def remote_mcp_handler("):
        ENTRY.index("async def world_build_board_handler(")
    ]
    assert "await _org_bot_token_context(env, request, touch=True)" in handler
    assert '"invalid_bot_token"' in handler
    assert "bounded_json_request(request, max_bytes=64 * 1024)" in handler
    assert '"cache-control": "no-store"' in handler


def test_remote_mcp_implements_lifecycle_and_task_tools():
    for method in (
        '"initialize"',
        '"ping"',
        '"tools/list"',
        '"tools/call"',
    ):
        assert method in ENTRY
    for tool in (
        '"list_org_tasks"',
        '"get_org_task"',
        '"complete_org_task"',
    ):
        assert tool in ENTRY
    assert '"organization.tasks.read"' in ENTRY
    assert '"organization.tasks.write"' in ENTRY
    assert "world_office_tasks.handle(" in ENTRY


def test_org_admin_generates_one_filled_remote_setup_block():
    assert "function orgRemoteMcpPrompt(name, mode, token)" in DASHBOARD
    assert "function orgRemoteMcpSetup(name)" in DASHBOARD
    assert 'window.location.origin + "/mcp"' in DASHBOARD
    assert 'type: "http"' in DASHBOARD
    assert 'Authorization: "Bearer " + token' in DASHBOARD
    assert "Generate complete setup" in DASHBOARD
    assert "Copy complete prompt" in DASHBOARD
    assert "PASTE_" not in DASHBOARD[
        DASHBOARD.index("function orgRemoteMcpPrompt("):
        DASHBOARD.index("function renderOrgDetail(")
    ]


def test_generated_remote_credential_is_task_scoped():
    wiring = DASHBOARD[
        DASHBOARD.index(
            'root.querySelectorAll("[data-org-remote-mcp-generate]"'
        ):
        DASHBOARD.index("function wireOrgMemberAutocomplete(")
    ]
    assert '"organization.tasks.read"' in wiring
    assert '"organization.tasks.write"' in wiring
    assert '"repository.issues.write"' not in wiring
    assert '"agents.sessions.run"' not in wiring
