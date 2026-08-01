"""Web issue details offer a copyable MCP prompt that works the issue end to end."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = (ROOT / "public" / "dashboard.js").read_text(encoding="utf-8")


def test_issue_detail_renders_copy_mcp_prompt_action():
    for contract in (
        "data-repo-issue-mcp-prompt",
        "Copy MCP prompt",
        # Issues only, and never for a pending record that has no number yet.
        "isIssues && !options.pending",
    ):
        assert contract in DASHBOARD


def test_prompt_carries_the_full_mcp_server_configuration_and_token():
    # A paste has to be enough: the block names the stdio server, the checkouts
    # it needs, and the connector token the signed write tools require.
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


def test_connector_token_is_asked_for_once_and_remembered():
    # The desktop mints the token locally and it never reaches the website, so
    # the browser remembers what the user pastes and reuses it on every copy;
    # a shift-click replaces a rotated one, and a skipped prompt still copies
    # with an obvious placeholder rather than a silently broken config.
    for contract in (
        '"forkmesh.mcpConnectorToken"',
        "function loadMcpConnectorToken()",
        "function saveMcpConnectorToken(token)",
        "if (!token || options?.replaceToken) {",
        "window.prompt(",
        "saveMcpConnectorToken(token);",
        "MCP_CONNECTOR_TOKEN_PLACEHOLDER",
        "replaceToken: event.shiftKey === true,",
    ):
        assert contract in DASHBOARD


def test_prompt_covers_pull_work_submit_and_model_report():
    for contract in (
        "function issueMcpPrompt(repo, number, values, token)",
        # The agent pulls the issue, works a branch, and submits the PR
        # through the forkmesh MCP server's own tools.
        'the "forkmesh" MCP server',
        "search_issues",
        "read_file",
        "open_pr_from_branch",
        # It must report back the model and thinking setting on the issue.
        "comment_on_issue",
        "which model you ran as (model name/id) and your thinking setting",
        "Do not skip this report.",
    ):
        assert contract in DASHBOARD


def test_signed_out_click_routes_through_login_and_back():
    # The button renders regardless of session, but copying requires an
    # authenticated account: the handler bounces to /login with a next hop
    # back to the issue instead of handing out the prompt.
    assert "async function copyIssueMcpPrompt(button, options)" in DASHBOARD
    assert (
        '"/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`)'
        in DASHBOARD
    )
    assert "if (!state.session?.sessionToken) {" in DASHBOARD
