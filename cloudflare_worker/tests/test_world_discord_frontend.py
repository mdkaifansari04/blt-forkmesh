"""Static safety checks for the on-demand World Discord bridge."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"


def test_world_discord_bridge_is_manual_and_never_handles_provider_secrets():
    source = (WORLD / "world-discord.js").read_text(encoding="utf-8")
    index = (WORLD / "index.html").read_text(encoding="utf-8")
    styles = (WORLD / "world.css").read_text(encoding="utf-8")
    assert 'src="/world/world-discord.js"' in index
    assert "/api/orgs/${encodeURIComponent(state.organization)}/discord" in source
    assert "/discord/oauth/start" in source
    assert "textContent" in source
    assert "innerHTML" not in source
    assert "setInterval" not in source
    assert "DISCORD_BOT_TOKEN" not in source
    assert "oauth/start" in source
    assert "state.reading" in source
    assert "state.writing" in source
    assert "window.location.assign" in source
    assert "window.open" not in source
    assert "consumeOAuthOutcome" in source
    assert "Discord returned an invalid or expired authorization" in source
    assert "stage: state lookup" in source
    assert "stage: session context" in source
    assert "Disconnect Discord" in source
    assert "connector.state === \"configured\"" in source
    assert "world-discord-panel" in styles
