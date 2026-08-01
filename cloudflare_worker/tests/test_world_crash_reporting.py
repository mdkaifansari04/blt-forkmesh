"""World crash detection, crash-report, and safe-mode reboot contracts."""

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")


def _client_error_fields():
    tree = ast.parse(ENTRY)
    names = {
        "CLIENT_ERROR_SURFACES",
        "CLIENT_ERROR_KINDS",
    }
    body = []
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id in names
            for target in node.targets
        ):
            body.append(node)
        elif (
            isinstance(node, ast.FunctionDef)
            and node.name in {
                "_sanitize_client_error_text",
                "_client_error_fields",
            }
        ):
            body.append(node)
    namespace = {"re": re}
    exec(compile(ast.Module(body=body, type_ignores=[]), "<entry>", "exec"),
         namespace)
    return namespace["_client_error_fields"]


def test_crash_kind_is_accepted_by_the_client_error_collector():
    assert (
        'CLIENT_ERROR_KINDS = frozenset({"error", "unhandledrejection", '
        '"crash"})'
    ) in ENTRY


def test_world_arms_a_pagehide_cleared_crash_guard_with_heartbeat():
    assert 'const CRASH_GUARD_KEY = "forkmesh.world.crash-guard.v1"' in WORLD
    assert 'const CRASH_COUNT_KEY = "forkmesh.world.crash-count.v1"' in WORLD


    assert "this.armCrashGuard();" in WORLD
    assert "this.beatCrashGuard();" in WORLD
    assert (
        "this.disarmCrashGuard();\n"
        "    this.pauseWorldActivity();"
    ) in WORLD

    assert "heapUsedMb" in WORLD
    assert "contextLosses" in WORLD
    assert "UNMASKED_RENDERER_WEBGL" in WORLD
    assert "document.visibilityState" in WORLD


def test_world_reports_the_previous_crashed_session_on_reload():
    assert "reportPreviousWorldCrash()" in WORLD
    assert "document.wasDiscarded === true" in WORLD
    assert "World crashed and reloaded" in WORLD
    assert "World reloaded after the browser discarded the tab" in WORLD
    assert '"/api/client-errors"' in WORLD
    assert 'kind: "crash"' in WORLD
    assert 'surface: "world"' in WORLD
    assert "keepalive: true" in WORLD


def test_webgl_context_losses_are_reported_with_diagnostics():
    assert "World renderer crashed; WebGL context lost after" in WORLD
    assert "this.reportedRendererContextLoss = true;" in WORLD
    assert "this.rendererContextLosses" in WORLD


def test_crashed_sessions_reboot_into_the_compact_renderer():



    assert "this.rendererSafeMode = this.worldCrashCount() > 0;" in WORLD
    assert "forceCompactRenderer: this.rendererSafeMode === true," in WORLD
    assert "forceCompactRenderer = false," in SCENE
    assert (
        "forceCompactRenderer || window.matchMedia?."
        '("(pointer: coarse)")?.matches,'
    ) in SCENE

    assert (
        "this.recordWorldCrash();\n"
        "    this.preserveWorldPositionForRefresh();"
    ) in WORLD


def test_compact_renderer_paints_large_plates_at_half_resolution():


    assert "const CANVAS_TEXTURE_SCALE_MIN_PIXELS = 512 * 512;" in SCENE
    assert "let canvasTextureScale = 1;" in SCENE
    assert "canvasTextureScale = compactRenderer ? 0.5 : 1;" in SCENE


    build = SCENE[SCENE.index("export function createWorldScene("):]
    assert build.index("canvasTextureScale = compactRenderer") < build.index(
        "createWorldSky({")

    assert "canvas.width = Math.max(1, Math.round(width * scale));" in SCENE
    assert "context.setTransform(scale, 0, 0, scale, 0, 0);" in SCENE

    assert (
        "if (canvasTextureScale >= 1 || draw.length >= 2) return 1;"
    ) in SCENE

    repaint = SCENE[SCENE.index("function repaintCanvasTexture("):]
    repaint = repaint[:repaint.index("\nfunction ")]
    assert "Number(canvas.dataset?.textureScale) || 1" in repaint
    assert (
        "context.clearRect(0, 0, canvas.width / scale, canvas.height / scale)"
    ) in repaint


def test_crash_reports_name_the_device_renderer_and_resident_scene():


    report = WORLD[WORLD.index("  reportPreviousWorldCrash()"):]
    report = report[:report.index("  reportWorldClientError(")]
    for reading in (
        "device ${device.touch",
        "renderer ${record.compact === true",
        "webgl${record.webgl2 === true",
        "cores ${describe(device.cores)}",
        "device memory ",
        "textures ${coarseCrashLabel(record.textures)}",
        "geometries ${coarseCrashLabel(record.geometries)}",
        "programs ${coarseCrashLabel(record.programs)}",
        "draws ${coarseCrashLabel(record.calls)}",
        "buffer ${buffer} at dpr ",
        "space ${String(record.space",
        "avatars ${describe(record.avatars)}",
    ):
        assert reading in report, reading

    assert "deviceProfile()" in WORLD
    assert "navigator.hardwareConcurrency" in WORLD
    assert "navigator.deviceMemory" in WORLD


    guard = WORLD[WORLD.index("  beatCrashGuard()"):]
    guard = guard[:guard.index("  reportPreviousWorldCrash()")]
    assert "const output = snapshot?.output;" in guard
    assert (
        "bufferWidth: output ? Math.round(output.drawingBufferWidth) : -1,"
    ) in guard
    assert (
        "compact: output ? output.compactRenderer === true : false,"
    ) in guard


def test_volatile_crash_readings_are_bucketed_so_equivalent_crashes_group():



    assert "function coarseCrashReading(value)" in WORLD
    assert "function coarseCrashLabel(value, unit = \"\")" in WORLD
    assert "const magnitude = 10 ** Math.floor(Math.log10(number));" in WORLD
    assert "return Math.round(number / magnitude) * magnitude;" in WORLD


def test_crash_reports_reach_the_admin_ping_without_losing_the_tail():


    assert '_sanitize_client_error_text(payload.get("message"), 900)' in ENTRY
    assert 'String(message || "").slice(0, 900)' in WORLD
    notify = ENTRY[ENTRY.index("async def _notify_new_error_group("):]
    notify = notify[:notify.index("async def _write_error_log(")]
    assert 'str(message or "")[:460]' in notify


    assert 'clean_string(body, 500)' in ENTRY

    assert "white-space:pre-wrap;overflow-wrap:anywhere" in ENTRY
    assert "text-overflow:ellipsis;white-space:nowrap;vertical-align:middle" \
        not in ENTRY


def test_crash_reports_survive_server_side_validation_and_redaction():
    fields = _client_error_fields()({
        "kind": "crash",
        "surface": "world",
        "message": (
            "World crashed and reloaded; previous session ended without "
            "pagehide; uptime 342s; heartbeat gap 2s; navigation reload; "
            "visibility visible; tab crashes 1; socket online with 3 peers; "
            "fps 11; frame 84ms worst 1204ms; triangles 1204000; dpr 2; "
            "heap 1900MB of 2048MB; context losses 1; safe mode off; "
            "gpu ANGLE (NVIDIA, GeForce RTX 3060 Direct3D11)"),
        "stack": "",
        "source": "world.js",
        "line": 0,
        "column": 0,
    })
    assert fields is not None
    surface, detail = fields
    assert surface == "world"
    assert detail.startswith("client crash [world]")
    assert "uptime 342s" in detail
    assert "heap 1900MB of 2048MB" in detail
    assert "GeForce RTX 3060" in detail
