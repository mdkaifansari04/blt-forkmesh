from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def test_world_hud_has_compact_clipboard_prompt_button():
    button = WORLD[
        WORLD.index('class="world-prompt-button"'):
        WORLD.index("</button>", WORLD.index('class="world-prompt-button"'))
    ]
    assert "data-world-mcp-prompt" in button
    assert "Copy MCP task prompt" in button
    assert "<svg" in button
    assert ">Prompt<" in button
    assert ".world-status-actions .world-prompt-button" in CSS


def test_world_prompt_uses_task_only_one_day_credential_and_role_workflows():
    handler = WORLD[
        WORLD.index("async copyMcpTaskPrompt("):
        WORLD.index("\n  // Server-authoritative", WORLD.index(
            "async copyMcpTaskPrompt("))
    ]
    assert '"organization.tasks.read"' in handler
    assert '"organization.tasks.write"' in handler
    assert "expiresDays: 1" in handler
    assert 'role === "owner" ? "deploy" : "pr"' in handler
    assert "copyWorldText(worldRemoteMcpPrompt(name, mode, token))" in handler

    prompt = WORLD[
        WORLD.index("function worldRemoteMcpPrompt("):
        WORLD.index("\n\nfunction createPullMergeRequestId", WORLD.index(
            "function worldRemoteMcpPrompt("))
    ]
    assert "merge only when the merge and required checks are clean" in prompt
    assert "Do not merge or deploy" in prompt
    assert "Treat the bearer credential above as a secret" in prompt


def test_members_can_mint_only_task_scoped_tokens():
    handler = ENTRY[
        ENTRY.index("async def org_bot_tokens_handler("):
        ENTRY.index("\n\nasync def bot_session_handler", ENTRY.index(
            "async def org_bot_tokens_handler("))
    ]
    assert 'actor_role not in ("owner", "admin", "member")' in handler
    assert (
        'method in ("GET", "DELETE") and actor_role not in ("owner", "admin")'
        in handler
    )
    assert 'actor_role == "member"' in handler
    assert '"organization.tasks.read", "organization.tasks.write"' in handler
