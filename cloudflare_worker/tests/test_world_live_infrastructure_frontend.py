#!/usr/bin/env python3
"""Regressions for truthful, low-impact World infrastructure displays."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)


def _between(start, end):
    return APP[APP.index(start):APP.index(end, APP.index(start))]


def test_system_capacity_uses_current_service_limits_and_admin_table_counts():
    assert "worldConnections = Number(context?.worldConnections)" in APP
    assert (
        "worldMessagesPerSecond = Number(context?.worldMessagesPerSecond)"
        in APP
    )
    assert "chatConnections = Number(context?.chatConnections)" in APP
    metrics = _between(
        "  updateSystemCapacityMetrics() {",
        "  async moderateWorldPeer(action) {",
    )
    assert "this.worldLimits" in metrics
    assert "this.network?.stats?.clients" in metrics
    assert "Boolean(this.serverPeerId)" in metrics
    assert "1 + this.remotePlayers.size" in metrics
    assert "limits.worldConnections" in metrics
    assert "limits.chatConnections" in metrics
    assert "this.systemCapacityTables" in metrics
    assert "this.world.updateSystemCapacity({" in metrics
    assert "tables: this.systemCapacityTables" in metrics
    assert "Math.random" not in metrics
    assert "fetch(" not in metrics
    assert "fetchJSON" not in metrics


def test_infrastructure_display_adds_no_durable_object_polling():
    # Context and the cached network overview are each part of the existing
    # one-shot bootstrap. Rendering their values must not create another poll.
    assert APP.count('this.fetchJSON("/api/world/context"') == 1
    assert APP.count('this.fetchJSON("/api/network/overview"') == 1
    for method in (
        "startStatusBoardPolling",
        "startEventPolling",
        "startNotificationPolling",
        "startMediaPlaybackPolling",
    ):
        section = _between(f"  {method}(", "\n  }")
        assert "/api/world/context" not in section
        assert "/api/network/overview" not in section


def test_treasury_balance_refreshes_on_hover_instead_of_on_a_timer():
    # Every /api/accounts/central-fund view costs a public Solana RPC round
    # trip, so the SOL board keeps the balance it is already painted with
    # until somebody points at it. No timer may fetch it.
    assert "WORLD_REWARD_POLL_MS" not in APP
    board = _between("  startStatusBoardPolling() {", "\n  }")
    assert "this.refreshSystemStatusBoard()" in board
    assert "refreshRewardState" not in board
    hover = _between("  async refreshRewardStateOnHover() {", "\n  }")
    assert "now - this.rewardHoverRefreshedAt < WORLD_REWARD_HOVER_MS" in hover
    assert "this.refreshRewardState({ force: true })" in hover
    assert "onRewardBoardHover: () =>" in APP
    # The hover itself is reported by the scene, from the treasury sign only.
    assert "onRewardBoardHover = () => {}," in SCENE
    assert "function updateRewardBoardHover(event, now) {" in SCENE
    assert 'landmarkObjects.get("fountain")?.userData?.treasurySign' in SCENE
    assert "if (hovered) onRewardBoardHover();" in SCENE
