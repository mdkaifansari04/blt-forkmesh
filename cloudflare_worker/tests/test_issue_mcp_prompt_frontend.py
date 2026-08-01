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


def test_prompt_covers_pull_work_submit_and_model_report():
    for contract in (
        "function issueMcpPrompt(repo, number, values)",
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
    assert "async function copyIssueMcpPrompt(button)" in DASHBOARD
    assert (
        '"/login?next=" + encodeURIComponent(`${location.pathname}${location.search}`)'
        in DASHBOARD
    )
    assert "if (!state.session?.sessionToken) {" in DASHBOARD
