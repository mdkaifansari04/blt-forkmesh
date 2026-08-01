#!/usr/bin/env python3
"""Focused contracts for the device-local World diagnostics bar."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
CSS = (WORLD / "world.css").read_text(encoding="utf-8")


def _section(source: str, start: str, end: str) -> str:
    return source[source.index(start) : source.index(end, source.index(start))]


def test_diagnostics_bar_is_compact_expandable_and_device_local():
    assert 'class="world-diagnostics"' in APP
    assert "<details" in APP
    assert (
        'aria-label="Open local World performance and connection details"'
        in APP
    )
    assert "Local one-second samples." in APP
    assert "Nothing here is transmitted while you are in the World" in APP


    assert (
        "if the tab crashes, a summary of these readings and your device "
        "class is reported"
    ) in APP
    for sensitive in ("URLs", "locations", "form contents", "activity history"):
        assert sensitive in APP
    assert ".world-diagnostics summary" in CSS
    assert ".world-diagnostics[open]" in CSS
    assert 'data-world-diagnostics-music-progress' in APP
    assert 'data-world-diagnostics-music-position' in APP
    assert ".world-diagnostics-compact" in CSS
    assert ".world-diagnostics-music-compact progress" in CSS
    assert "@media (max-width: 480px)" in CSS


def test_renderer_exposes_bounded_on_demand_frame_and_draw_diagnostics():
    diagnostics = _section(
        SCENE,
        "  function getDiagnostics(",
        "\n  function dispose(",
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


def test_music_playbar_reuses_the_local_one_hertz_diagnostics_sample():
    diagnostics = _section(
        APP,
        "  collectDiagnostics(",
        "\n  startActivityTicker()",
    )
    assert "playback.element?.currentTime" in diagnostics
    assert "playback.element?.duration" in diagnostics
    assert 'this.$("[data-world-diagnostics-music-progress]")' in diagnostics
    assert "musicProgress.max = duration || 1;" in diagnostics
    assert "musicProgress.value = position;" in diagnostics
    assert "setInterval(" not in diagnostics
    assert "fetch(" not in diagnostics


def test_mobile_renderer_has_low_memory_and_page_lifecycle_recovery():
    assert 'window.matchMedia?.("(pointer: coarse)")?.matches' in SCENE
    assert "antialias: !compactRenderer" in SCENE
    assert "stencil: false" in SCENE
    assert 'powerPreference: compactRenderer ? "default" : "high-performance"' in SCENE
    assert "compactRenderer ? 1" in SCENE
    assert '"webglcontextlost"' in SCENE
    assert '"webglcontextrestored"' in SCENE
    assert "event.preventDefault();" in SCENE
    assert "renderer.resetState();" in SCENE
    assert "renderer.shadowMap.needsUpdate = renderer.shadowMap.enabled;" in SCENE
    assert 'onRendererStateChange("lost")' in SCENE
    assert 'onRendererStateChange("restored")' in SCENE
    assert 'window.addEventListener("pagehide", this.handlePageHide);' in APP
    assert 'window.addEventListener("pageshow", this.handlePageShow);' in APP
    assert "event?.persisted === true" in APP
    assert "RENDERER_RECOVERY_DELAY_MS" in APP
    assert "data-world-renderer-recovery" in APP


def test_local_point_lights_are_budgeted_and_reported_separately():
    assert "LOCAL_POINT_LIGHT_BUDGET_DESKTOP = 4" in SCENE
    assert "LOCAL_POINT_LIGHT_BUDGET_COMPACT = 2" in SCENE
    assert "function updateLocalPointLightBudget" in SCENE
    assert "localPointLightCandidates.slice(0, budget)" in SCENE
    assert "if (parent === scene) return true" in SCENE
    assert "distanceToSquared(camera.position)" in SCENE
    assert "const localPointLights = new Set()" in SCENE
    assert "activePointLights" in SCENE
    assert "activeLights" in APP
    assert "activePointLights" in APP


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


def test_readings_are_graded_green_orange_red_from_local_thresholds_only():
    grading = _section(
        APP,
        "const WORLD_DIAGNOSTICS_THRESHOLDS = {",
        "\nfunction diagnosticStateLevel(",
    )
    for metric in (
        "fps",
        "frameTimeMs",
        "calls",
        "triangles",
        "longFrames",
        "longestFrameMs",
        "pointerGapMs",
        "reconnects",
        "bufferedBytes",
        "frameRate",
        "coalesced",
        "backpressure",
    ):
        assert f"{metric}:" in grading

    assert "lowerIsWorse: true" in grading
    assert 'return "high"' in grading
    assert '? "caution" : "good"' in grading

    for forbidden in ("fetch(", "setInterval(", "localStorage", "sessionStorage"):
        assert forbidden not in grading

    render = _section(APP, "  renderDiagnostics()", "\n  startActivityTicker()")
    assert "diagnosticMetric(" in render
    assert "diagnosticStateLevel(connection.state)" in render


    assert "escapeHTML(renderer.space)" in render
    assert "escapeHTML(queues.movement)" in render
    assert "escapeHTML(version)" in render

    assert '.world-diagnostics-value[data-level="good"]' in CSS
    assert '.world-diagnostics-value[data-level="caution"]' in CSS
    assert '.world-diagnostics-value[data-level="high"]' in CSS
    assert "var(--world-mint)" in CSS
    assert "var(--world-sun)" in CSS
    assert "var(--world-danger)" in CSS


def test_world_socket_recovery_reports_current_retries_not_lifetime_attempts():
    assert 'from "./world-socket-recovery.js"' in APP
    assert "createWorldSocketRecoveryTimers" in APP
    assert "this.socketRecovery = createWorldSocketRecoveryTimers" in APP
    diagnostics = _section(
        APP,
        "  collectDiagnostics(",
        "\n  renderDiagnostics()",
    )
    assert "this.socketRecoveryAttempts" in diagnostics
    assert "this.socketConnectionAttempts - 1" not in diagnostics
