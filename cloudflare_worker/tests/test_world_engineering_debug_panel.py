from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)


def test_engineering_floor_has_live_bounded_optimization_telemetry():
    for contract in (
        "function engineeringDebugTexture(THREE, snapshot = {})",
        "function createEngineeringDebugPanel(THREE)",
        '"forkmesh-engineering-live-debug-panel"',
        '"ENGINEERING · LIVE DEBUG CONTROL"',
        "GREEN HEALTHY · ORANGE OPTIMIZE · RED ACT",
        'officeFloorGroups.get("engineering").add(engineeringDebugPanel)',
        '"FRAMES / SECOND"',
        '"LONGEST FRAME"',
        '"DRAW CALLS"',
        '"TRIANGLES"',
        '"GEOMETRIES"',
        '"TEXTURES"',
        '"GPU PROGRAMS"',
        '"ANIMATION CALLBACKS"',
        '"INTERACTIVE TARGETS"',
        '"JS HEAP"',
        "renderer.info?.render?.calls",
        "renderer.info?.memory?.textures",
        "previous?.dispose?.()",
    ):
        assert contract in SCENE


def test_debug_panel_uses_green_orange_red_thresholds():
    assert 'status === "critical"' in SCENE
    assert 'status === "warning"' in SCENE
    assert 'statusHigh(engineeringDebugLongestFrameMs, 24, 40)' in SCENE
    assert "statusLow(fps, 50, 30)" in SCENE
