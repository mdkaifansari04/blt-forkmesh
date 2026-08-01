#!/usr/bin/env python3
"""Contracts for the Elements tab and the settings Debug tab.

The issue asks for three things: a settings tab that can
turn every single world element off (completely removing it from the game)
and back on to isolate what is slowing rendering down; the debug tool moved
into its own settings tab; and that panel staying open while playing, with
all debug information updating in real time plus concrete improvement
suggestions.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
CSS = (WORLD / "world.css").read_text(encoding="utf-8")


def test_scene_keeps_an_element_registry_every_piece_registers_into():
    for contract in (
        "const worldElements = new Map();",
        "const disabledWorldElements = new Set(",
        "function registerWorldElement(id, label, category, roots = [], live = null)",
        "function registerWorldElementSystem(id, label, category, onToggle = null)",
        "function setWorldElementEnabled(id, enabled)",
        "function listWorldElements()",
        "function worldElementEnabled(id)",
        "initialDisabledElements = [],",
    ):
        assert contract in SCENE

    assert "listWorldElements,\n    setWorldElementEnabled," in SCENE


    for element_id in (
        '"city-terrain"',
        '"town-landscape"',
        '"beach"',
        '"causeways"',
        '"campfire"',
        '"swing-set"',
        '"gym"',
        '"player-avatar"',
        '"forkbot"',
        '"agent-npcs"',
        '"remote-avatars"',
        '"node-cabinets"',
        '"directory-bots"',
        '"repository-portals"',
        '"office-interior"',
        '"social-banners"',
        '"lighting"',
        '"sky"',
        '"shadows"',
        '"screen-labels"',
        '"animations"',
        '"weather"',
        '"effects"',
        '"chat-bubbles"',
        "`landmark-${landmark.id}`",
    ):
        assert element_id in SCENE, f"element {element_id} is not registered"


def test_disabling_an_element_removes_it_from_scene_raycast_and_frame_work():

    detach = SCENE.split("function detachElementRoot", 1)[1].split(
        "function attachElementRoot", 1
    )[0]
    assert "interactive.splice(index, 1);" in detach
    assert "parent?.remove?.(root);" in detach

    assert "renderer.shadowMap.needsUpdate = true;" in SCENE.split(
        "function setWorldElementEnabled", 1
    )[1].split("function listWorldElements", 1)[0]

    for gate in (
        'if (worldElementEnabled("remote-avatars")) updateRemotePlayers(delta, time);',
        'if (worldElementEnabled("forkbot")) updateForkbot(delta, time);',
        'if (worldElementEnabled("agent-npcs")) updateAgentBots(delta, time);',
        'if (worldElementEnabled("animations")) {',
        'if (worldElementEnabled("weather")) {',
        'if (worldElementEnabled("sky")) worldSky.tick(Date.now(), camera.position);',
        'if (worldElementEnabled("node-cabinets")) {',
        'if (worldElementEnabled("directory-bots")) {',
        'if (worldElementEnabled("repository-portals")) {',
        'if (worldElementEnabled("screen-labels") && time >= nextScreenLabelUpdateAt)',
        'if (!worldElementEnabled("effects")) return',
        'if (!worldElementEnabled("chat-bubbles")) return',
    ):
        assert gate in SCENE, f"missing per-frame gate: {gate}"


    assert "if (!current.parent && !current.isScene) return false;" in SCENE
    assert "() => remotePlayers.get(remoteId) === avatar," in SCENE
    assert "() => nodeInfrastructure.get(id) === registeredCabinet," in SCENE


def test_elements_tab_is_available_to_everyone_device_local_and_wired_to_the_scene():
    for contract in (
        'data-world-settings-tab="elements"',
        'data-world-settings-pane="elements"',
        "data-world-elements-tab",
        "data-world-element-list",
        'data-world-element-master="on"',
        'data-world-element-master="off"',
        "data-world-element-toggle",
        'const DISABLED_ELEMENTS_KEY = "forkmesh.world.disabledElements.v1";',
        "function storedDisabledWorldElements()",
        "initialDisabledElements: this.disabledWorldElements,",
    ):
        assert contract in APP


    toggle = APP.split("  setWorldElementEnabled(id, enabled) {", 1)[1].split(
        "\n  setAllWorldElementsEnabled(", 1
    )[0]
    assert "this.identity?.isAdmin !== true" not in toggle
    assert "writeJSON(localStorage, DISABLED_ELEMENTS_KEY" in toggle
    assert "disabledElements" not in APP.split("function defaultSettings()", 1)[
        1
    ].split("function mergeSettings", 1)[0]

    restore = APP.split("  applyAdminElementsAccess() {", 1)[1].split(
        "\n  adminErrorStorageKey", 1
    )[0]
    assert "elementsTab.hidden = false;" in restore


def test_elements_can_sort_by_drawn_triangles_or_clicks():
    for contract in (
        'data-world-element-sort',
        '<option value="drawables">Drawn</option>',
        '<option value="triangles">Triangles</option>',
        '<option value="interactives">Clicks</option>',
        'this.worldElementSort = "drawables";',
    ):
        assert contract in APP
    render = APP.split("  renderWorldElementsPane(statusMessage = \"\") {", 1)[1].split(
        "\n  renderWorldSessions", 1
    )[0]
    assert 'Number(right[sort]) - Number(left[sort])' in render


def test_debug_tab_shows_live_readings_with_suggestions_and_stays_open():
    for contract in (
        'data-world-settings-tab="debug"',
        'data-world-settings-pane="debug"',
        "data-world-debug-live",
        "data-world-debug-suggestions",
        "data-world-debug-renderer",
        "data-world-debug-frame-health",
        "data-world-debug-memory",
        "data-world-debug-connection",
        "function worldDebugSuggestions(",
        "diagnosticsDetailReadouts(snapshot)",
        "renderDebugSettingsPane(",
        "settingsDebugPaneElement()",
    ):
        assert contract in APP


    head = APP.split("  renderDiagnostics() {", 1)[1].split(
        "diagnosticsDetailReadouts(snapshot) {", 1
    )[0]
    assert "const debugPane = this.settingsDebugPaneElement();" in head
    assert "if (!floatingVisible && !debugPane) return;" in head
    assert "if (debugPane) this.renderDebugSettingsPane(debugPane, snapshot);" in head


    suggestions = APP.split("function worldDebugSuggestions(", 1)[1].split(
        "\nclass ", 1
    )[0]
    assert "Elements tab" in suggestions
    assert 'diagnosticLevel("calls"' in suggestions
    assert 'diagnosticLevel("triangles"' in suggestions
    assert "pixelRatio" in suggestions


    assert '!["debug", "elements"].includes(this.settingsTab || "") &&' in APP

    view_pane = APP.split('data-world-settings-pane="view"', 1)[1].split(
        "</section>", 1
    )[0]
    assert "data-world-debug-panel" not in view_pane
    debug_pane = APP.split('data-world-settings-pane="debug"', 1)[1].split(
        'data-world-settings-pane="elements"', 1
    )[0]
    assert "data-world-debug-panel" in debug_pane


def test_element_and_debug_panels_have_styles():
    for selector in (
        ".world-debug-live",
        ".world-debug-suggestions",
        '.world-debug-suggestions li[data-level="high"]',
        ".world-element-master",
        ".world-element-category",
        ".world-element-row",
        '.world-element-row[data-enabled="false"]',
    ):
        assert selector in CSS
