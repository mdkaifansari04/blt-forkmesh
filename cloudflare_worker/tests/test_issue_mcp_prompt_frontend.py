"""Web issue details offer a copyable MCP prompt that works the issue end to end."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = (ROOT / "public" / "dashboard.js").read_text(encoding="utf-8")


def test_issue_detail_renders_copy_mcp_prompt_action():
    for contract in (
        "data-repo-issue-mcp-prompt",
        "Copy MCP prompt",

        "isIssues && !options.pending",
    ):
        assert contract in DASHBOARD


def test_prompt_carries_the_full_mcp_server_configuration_and_token():


    for contract in (
        "mcpServers: {",
        '"<FORKMESH_CHECKOUT>/tools/forkmesh_mcp_server.py"',
        "FORKMESH_REPO: repoCheckout",
        "FORKMESH_MCP_TOKEN: connectorToken",
        "JSON.stringify(configuration, null, 2)",
        "claude mcp add forkmesh --scope user",
        "git clone ${forkmeshCloneUrl}",
        "never print it in logs, commits, pull requests, or chat",
    ):
        assert contract in DASHBOARD


def test_connector_token_is_generated_on_the_spot_and_remembered():


    for contract in (
        '"forkmesh.mcpConnectorToken"',
        "function loadMcpConnectorToken()",
        "function saveMcpConnectorToken(token)",
        "function generateMcpConnectorToken()",
        "window.crypto.getRandomValues(bytes)",
        '"fmcp_" +',
        "function ensureMcpConnectorToken(options)",
        "const token = ensureMcpConnectorToken(options);",
        "MCP_CONNECTOR_TOKEN_PLACEHOLDER",
    ):
        assert contract in DASHBOARD


def test_plain_copy_never_opens_a_dialog():


    start = DASHBOARD.index("function ensureMcpConnectorToken(options)")
    body = DASHBOARD[start : DASHBOARD.index("function issueMcpPrompt", start)]
    replace_branch = body.index("if (options?.replaceToken) {")
    prompt_call = body.index("window.prompt(")
    assert replace_branch < prompt_call

    assert body.index("if (!token) {") > prompt_call
    assert "replaceToken: event.shiftKey === true," in DASHBOARD


def test_prompt_installs_the_generated_token_without_clobbering_a_connector():



    for contract in (
        "mcp/connector.json",
        "Activate it before step 1",


        'create it (mode 0600) containing {"version": 1, "token"',
        '"created_ms": <epoch milliseconds>',
        "leave it exactly as it is and use its own",
        "overwriting it would revoke every other agent",
    ):
        assert contract in DASHBOARD


def test_prompt_covers_pull_work_submit_and_model_report():
    for contract in (
        "function issueMcpPrompt(repo, number, values, token)",


        'the "forkmesh" MCP server',
        "search_issues",
        "read_file",
        "open_pr_from_branch",

        "comment_on_issue",
        "which model you ran as (model name/id) and your thinking setting",
        "Do not skip this report.",
    ):
        assert contract in DASHBOARD


def test_signed_out_click_routes_through_login_and_back():



    assert "async function copyIssueMcpPrompt(button, options)" in DASHBOARD
    assert (
        '"/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`)'
        in DASHBOARD
    )
    assert "if (!state.session?.sessionToken) {" in DASHBOARD
