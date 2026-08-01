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
    # The registry is exposed to the embedding page.
    assert "listWorldElements,\n    setWorldElementEnabled," in SCENE
    # A broad sweep of named registrations: terrain, districts, boards,
    # recreation, avatars, systems, and the dynamic populations.
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
    # Scene graph and raycast list both empty out on detach…
    detach = SCENE.split("function detachElementRoot", 1)[1].split(
        "function attachElementRoot", 1
    )[0]
    assert "interactive.splice(index, 1);" in detach
    assert "parent?.remove?.(root);" in detach
    # …and a stale shadow does not linger after the caster leaves.
    assert "renderer.shadowMap.needsUpdate = true;" in SCENE.split(
        "function setWorldElementEnabled", 1
    )[1].split("function listWorldElements", 1)[0]
    # Per-frame work pauses with its element toggle.
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
    # A detached subtree cannot be clicked: visibility now requires a path to
    # the Scene, and despawned dynamic roots are never resurrected.
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
    # Every visitor can flip switches, and choices persist on this device only
    # — never into synced account preferences.
    toggle = APP.split("  setWorldElementEnabled(id, enabled) {", 1)[1].split(
        "\n  setAllWorldElementsEnabled(", 1
    )[0]
    assert "this.identity?.isAdmin !== true" not in toggle
    assert "writeJSON(localStorage, DISABLED_ELEMENTS_KEY" in toggle
    assert "disabledElements" not in APP.split("function defaultSettings()", 1)[
        1
    ].split("function mergeSettings", 1)[0]
    # The tab stays visible regardless of the current account.
    restore = APP.split("  applyAdminElementsAccess() {", 1)[1].split(
        "\n  adminErrorStorageKey", 1
    )[0]
    assert "elementsTab.hidden = false;" in restore


def test_elements_are_one_flat_table_sortable_by_any_column():
    # One-line rows in a single table: the group is a column, and clicking any
    # column header sorts by it (clicking again reverses the direction).
    for contract in (
        'data-world-element-sort="${column.key}"',
        '{ key: "label", heading: "Element" },',
        '{ key: "category", heading: "Group" },',
        '{ key: "drawables", heading: "Drawn" },',
        '{ key: "triangles", heading: "Tri" },',
        '{ key: "interactives", heading: "Click" },',
        'this.worldElementSort = "drawables";',
        "this.worldElementSortAscending = this.worldElementSortAscending !== true;",
    ):
        assert contract in APP
    render = APP.split("  renderWorldElementsPane(statusMessage = \"\") {", 1)[1].split(
        "\n  renderWorldSessions", 1
    )[0]
    assert 'Number(right[sort]) - Number(left[sort])' in render
    # The whole registry renders as one flat list — no per-category sections.
    assert "world-element-category" not in render
    # The Members Circle campfire sits in the table with everything else.
    assert '"Members Circle campfire"' in SCENE


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
    # The same one-second diagnostics tick feeds the tab, whether or not the
    # floating pill is visible, so the readings are genuinely real time.
    head = APP.split("  renderDiagnostics() {", 1)[1].split(
        "diagnosticsDetailReadouts(snapshot) {", 1
    )[0]
    assert "const debugPane = this.settingsDebugPaneElement();" in head
    assert "if (!floatingVisible && !debugPane) return;" in head
    assert "if (debugPane) this.renderDebugSettingsPane(debugPane, snapshot);" in head
    # Suggestions grade against the same thresholds as the readouts and name
    # the heaviest enabled elements an administrator can switch off to test.
    suggestions = APP.split("function worldDebugSuggestions(", 1)[1].split(
        "\nclass ", 1
    )[0]
    assert "Elements tab" in suggestions
    assert 'diagnosticLevel("calls"' in suggestions
    assert 'diagnosticLevel("triangles"' in suggestions
    assert "pixelRatio" in suggestions
    # Clicking back into the world must not dismiss the Debug or Elements
    # tabs — that is the whole point of watching them while playing.
    assert '!["debug", "elements"].includes(this.settingsTab || "") &&' in APP
    # The floating pill checkbox moved out of the View pane into Debug.
    view_pane = APP.split('data-world-settings-pane="view"', 1)[1].split(
        "</section>", 1
    )[0]
    assert "data-world-debug-panel" not in view_pane
    debug_pane = APP.split('data-world-settings-pane="debug"', 1)[1].split(
        'data-world-settings-pane="elements"', 1
    )[0]
    assert "data-world-debug-panel" in debug_pane


def test_debug_tab_lists_every_individual_triangle_drawing_object_sortably():
    # The scene exposes one row per mesh that actually draws triangles, tagged
    # with the world element that owns it.
    for contract in (
        "function listSceneObjects()",
        "function sceneObjectLabel(object)",
        "const SCENE_OBJECT_WALK_LIMIT =",
        "setWorldElementEnabled,\n    listSceneObjects,",
    ):
        assert contract in SCENE
    walk = SCENE.split("function listSceneObjects() {", 1)[1].split(
        "\n  // Whole-scene systems", 1
    )[0]
    assert (
        "if (!child.isMesh || objects.length >= SCENE_OBJECT_WALK_LIMIT) return;"
        in walk
    )
    assert "if (child.userData?.raycastProxy === true) return;" in walk
    assert "if (triangles <= 0) return;" in walk
    # Instanced meshes report the whole batch, and each row names its element.
    assert "triangles: triangles * instances," in walk
    assert 'element: owner ? owner.label : "Unregistered",' in walk
    # The table lives in the Debug tab, is walked on demand, and every column
    # sorts (clicking the active column reverses it).
    debug_pane = APP.split('data-world-settings-pane="debug"', 1)[1].split(
        'data-world-settings-pane="elements"', 1
    )[0]
    for contract in (
        "data-world-object-list",
        "data-world-object-refresh",
        "data-world-object-status",
    ):
        assert contract in debug_pane
    for contract in (
        'data-world-object-sort="${column.key}"',
        '{ key: "label", heading: "Object" },',
        '{ key: "element", heading: "Element" },',
        '{ key: "type", heading: "Type" },',
        '{ key: "triangles", heading: "Tri" },',
        '{ key: "instances", heading: "Inst" },',
        'this.worldObjectSort = "triangles";',
        "this.worldObjectSortAscending = this.worldObjectSortAscending !== true;",
        "this.renderWorldObjectPane({ walk: true });",
        'const WORLD_OBJECT_NUMERIC_KEYS = new Set(["triangles", "instances"]);',
    ):
        assert contract in APP
    # Re-sorting reuses the last walk; only the refresh button re-traverses.
    render = APP.split("  renderWorldObjectPane({ walk = false } = {}) {", 1)[1].split(
        "\n  renderWorldSessions", 1
    )[0]
    assert "this.worldObjects = this.world?.listSceneObjects?.() || [];" in render
    assert "WORLD_OBJECT_ROW_LIMIT" in render


def test_element_and_debug_panels_have_styles():
    for selector in (
        ".world-debug-live",
        ".world-debug-suggestions",
        '.world-debug-suggestions li[data-level="high"]',
        ".world-element-master",
        ".world-element-head",
        ".world-element-row",
        '.world-element-row[data-enabled="false"]',
        ".world-object-table",
        ".world-object-head",
        ".world-object-row",
        '.world-object-row[data-visible="false"]',
    ):
        assert selector in CSS
