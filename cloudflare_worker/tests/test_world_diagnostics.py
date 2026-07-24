#!/usr/bin/env python3
"""Focused contracts for the device-local World diagnostics bar."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
CSS = (WORLD / "world.css").read_text(encoding="utf-8")
SPEECH_CSS = (WORLD / "world-speech.css").read_text(encoding="utf-8")


def _section(source: str, start: str, end: str) -> str:
    return source[source.index(start) : source.index(end, source.index(start))]


def test_diagnostics_bar_is_compact_expandable_and_device_local():
    assert 'class="world-diagnostics"' in APP
    assert "<details" in APP
    assert (
        'aria-label="Open local World performance and connection details"'
        in APP
    )
    assert "Local one-second samples only." in APP
    assert "No diagnostics are transmitted" in APP
    for sensitive in ("URLs", "locations", "form contents", "activity history"):
        assert sensitive in APP
    assert ".world-diagnostics summary" in CSS
    assert ".world-diagnostics[open]" in CSS
    assert "@media (max-width: 480px)" in CSS
    assert SPEECH_CSS.count("var(--world-diagnostics-height, 0px)") >= 5


def test_renderer_exposes_bounded_on_demand_frame_and_draw_diagnostics():
    diagnostics = _section(
        SCENE,
        "  function getDiagnostics(",
        "\n  function updateLandmarkConstruction",
    )
    assert "diagnosticsFrameCount" in diagnostics
    assert "(frames * 1000) / elapsedMs" in diagnostics
    assert "elapsedMs / frames" in diagnostics
    assert "rendererCalls" in diagnostics
    assert "rendererTriangles" in diagnostics
    assert "paused: !running" in diagnostics
    assert "getDiagnostics," in SCENE
    assert "renderer.info?.render?.calls" in SCENE
    assert "renderer.info?.render?.triangles" in SCENE


def test_socket_diagnostics_use_existing_frames_and_one_hertz_ui_sampling():
    assert "const WORLD_DIAGNOSTICS_INTERVAL_MS = 1000;" in APP
    assert "this.startDiagnostics();" in APP
    assert "this.diagnosticsTimer = window.setInterval(" in APP
    assert "this.socketInboundFrames = incrementDiagnosticCounter(" in APP
    assert "this.socketOutboundFrames = incrementDiagnosticCounter(" in APP
    assert "this.socketConnectionAttempts = incrementDiagnosticCounter(" in APP
    assert "this.movementCoalescedFrames = incrementDiagnosticCounter(" in APP
    assert "this.profileCoalescedFrames = incrementDiagnosticCounter(" in APP
    assert "this.socketBackpressureEvents = incrementDiagnosticCounter(" in APP
    assert "window.clearInterval(this.diagnosticsTimer);" in APP

    diagnostics = _section(
        APP,
        "  collectDiagnostics(",
        "\n  renderDiagnostics()",
    )
    for field in (
        "inboundRate",
        "outboundRate",
        "bufferedBytes",
        "movementCoalesced",
        "profileCoalesced",
        "backpressureEvents",
    ):
        assert field in diagnostics
    assert "WORLD_DIAGNOSTICS_COUNTER_MAX" in diagnostics
    assert "new WebSocket" not in diagnostics
    assert "fetch(" not in diagnostics
    for sensitive in (
        "location.href",
        "location.pathname",
        "searchParams",
        "lastMovement",
        "identity",
        "repositories",
    ):
        assert sensitive not in diagnostics


def test_build_marker_is_strictly_reduced_to_version_and_git_revision():
    build = _section(
        APP,
        "function normalizeBuildDiagnostics(",
        "\nfunction ",
    )
    assert "payload?.version" in build
    assert "payload?.rev" in build
    assert ".slice(0, 32)" in build
    assert "/^[a-f0-9]{7,64}$/i" in build
    assert "return { version, revision };" in build
    assert "build.revision.slice(0, 12)" in APP
