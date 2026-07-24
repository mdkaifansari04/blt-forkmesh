#!/usr/bin/env python3
"""Regressions for truthful, low-impact World infrastructure displays."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")


def _between(start, end):
    return APP[APP.index(start):APP.index(end, APP.index(start))]


def test_durable_object_models_use_existing_current_counts_and_server_limits():
    assert "worldConnections = Number(context?.worldConnections)" in APP
    assert (
        "worldMessagesPerSecond = Number(context?.worldMessagesPerSecond)"
        in APP
    )
    assert "chatConnections = Number(context?.chatConnections)" in APP
    metrics = _between(
        "  updateDurableObjectMetrics() {",
        "  async moderateWorldPeer(action) {",
    )
    assert "this.worldLimits" in metrics
    assert "this.network?.stats?.clients" in metrics
    assert "Boolean(this.serverPeerId)" in metrics
    assert "1 + this.remotePlayers.size" in metrics
    assert "limits.worldConnections" in metrics
    assert "limits.chatConnections" in metrics
    assert "this.world.updateDurableObjects({ objects })" in metrics
    assert "Math.random" not in metrics
    assert "fetch(" not in metrics
    assert "fetchJSON" not in metrics


def test_infrastructure_display_adds_no_durable_object_polling():
    # Context and the cached network overview are each part of the existing
    # one-shot bootstrap. Rendering their values must not create another poll.
    assert APP.count('this.fetchJSON("/api/world/context"') == 1
    assert APP.count('this.fetchJSON("/api/network/overview"') == 1
    for method in (
        "startRewardPolling",
        "startEventPolling",
        "startNotificationPolling",
        "startMediaPlaybackPolling",
    ):
        section = _between(f"  {method}(", "\n  }")
        assert "/api/world/context" not in section
        assert "/api/network/overview" not in section
