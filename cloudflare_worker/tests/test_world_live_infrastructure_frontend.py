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
    assert "this.syncSystemStatusBoardTimer()" in board
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


def test_status_board_counts_down_each_second_and_fetches_once_per_minute():
    assert "const WORLD_STATUS_POLL_MS = 60 * 1000;" in APP
    start = _between("  startStatusBoardPolling() {", "\n  }")
    assert "}, 1000);" in start
    timer = _between("  syncSystemStatusBoardTimer() {", "\n  }")
    assert "this.statusBoardRefreshRemaining()" in timer
    assert "this.world?.updateSocialBannerTimers?.({" in timer
    assert "status:" in timer
    assert "if (!loading && remaining <= 0)" in timer
    assert (
        "const socialBannerTimerRecords = [...socialBanners, statusBannerRecord];"
        in SCENE
    )
    assert "for (const record of socialBannerTimerRecords)" in SCENE
    refresh = _between("  async refreshSystemStatusBoard() {", "\n  }")
    assert "if (this.statusBoardLoad) return this.statusBoardLoad;" in refresh
    assert 'this.fetchJSON("/api/status?view=world"' in refresh
    assert "maxAge: WORLD_STATUS_POLL_MS" in refresh
    assert "this.applySystemStatusBoard(payload);" in refresh
    assert "dial.userData.countdownSeconds = Math.ceil(remaining / 1000);" in SCENE
    snapshot = SCENE.split("function updateSystemStatusBoard(payload)", 1)[1]
    snapshot = snapshot.split("function updateSocialBannerTimers", 1)[0]
    assert "Date.now() % 60_000" not in snapshot
