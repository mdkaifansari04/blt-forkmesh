"""Static safety checks for the on-demand World Discord bridge."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"


def test_world_discord_bridge_is_manual_and_never_handles_provider_secrets():
    source = (WORLD / "world-discord-panel.js").read_text(encoding="utf-8")
    loader = (WORLD / "world-discord.js").read_text(encoding="utf-8")
    index = (WORLD / "index.html").read_text(encoding="utf-8")
    styles = (WORLD / "world.css").read_text(encoding="utf-8")
    assert 'src="/world/world-discord.js"' in index
    assert "setInterval" not in loader
    assert "innerHTML" not in loader
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
    assert "stage: record decryption" in source
    assert "stage: record state binding" in source
    assert "stage: session context" in source
    assert "stage: authorization code" in source
    assert "Disconnect Discord" in source
    assert "connector.state === \"configured\"" in source
    assert "world-discord-panel" in styles


def test_world_discord_panel_loads_only_when_the_bridge_is_asked_for():
    """The eager entry point must stay a loader, not the bridge itself."""
    loader = (WORLD / "world-discord.js").read_text(encoding="utf-8")
    panel = (WORLD / "world-discord-panel.js").read_text(encoding="utf-8")
    assert 'import("./world-discord-panel.js")' in loader
    assert 'from "./world-discord-panel.js"' not in loader
    # Both entry points the panel used to bind for itself.
    assert "[data-world-discord-open]" in loader
    assert "forkmesh:open-discord" in loader
    # The panel no longer boots itself; the loader owns that call.
    assert "export function bootWorldDiscord()" in panel
    assert "export function showWorldDiscordPanel()" in panel
    assert "DOMContentLoaded" not in panel
    # An authorization return still opens the panel without a second click.
    assert 'searchParams.has("discord")' in loader
    # One binding per entry point: the loader releases its stand-ins as soon as
    # the panel binds its own, so a single click cannot toggle the panel twice.
    assert "releaseLoaderBindings()" in loader
    assert "removeEventListener" in loader
