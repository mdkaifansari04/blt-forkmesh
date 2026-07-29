"""First-input responsiveness contracts for the browser World."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text()
APP = (ROOT / "public" / "world" / "world.js").read_text()


def _section(source: str, start: str, end: str) -> str:
    return source[source.index(start) : source.index(end, source.index(start))]


def test_initial_and_restored_spawns_do_not_ease_the_camera_across_the_map():
    player_setup = _section(
        SCENE,
        "  const player = createAvatar",
        "  const playerLabel =",
    )
    assert "player.position.x + CAMERA_OFFSET[0]" in player_setup
    assert "player.position.y + FIRST_PERSON_EYE_HEIGHT + CAMERA_OFFSET[1]" in player_setup
    assert "player.position.z + CAMERA_OFFSET[2]" in player_setup

    spawn = _section(
        SCENE,
        "  function setSpawn(",
        "\n  // One unified chest card",
    )
    camera = _section(SCENE, "  function updateCamera(", "\n  function setSpawn")
    assert "cameraSnapPending = true" in spawn
    assert "else if (cameraSnapPending)" in camera
    assert "cameraSnapPending = false" in camera


def test_animation_loop_uses_cached_viewport_without_forcing_layout():
    resize = _section(SCENE, "  const resize = () =>", "\n  function animate(")
    animate = _section(SCENE, "  function animate(", "\n  function setPaused(")
    assert "container.getBoundingClientRect()" in resize
    assert "viewportRect.width = width" in resize
    assert "viewportRect.height = height" in resize
    assert "container.getBoundingClientRect()" not in animate
    assert "const rect = viewportRect" in animate


def test_first_input_toggles_activity_without_repainting_avatar_badges():
    handler = _section(
        APP,
        "  handlePublicInputActivity =",
        "\n  blockWorldPullToRefresh",
    )
    scene_toggle = _section(
        SCENE,
        "  function setInputActive(",
        "\n  function updateRepositoryCatalog",
    )
    assert (
        "this.world?.setInputActive?.(this.settings.privacy.activity === true)"
        in handler
    )
    assert "this.world?.setInputActive?.(false)" in handler
    assert "updateIdentity(" not in handler
    assert "this.scheduleActivityArrival()" in handler
    assert "this.scheduleInputActivityPublish()" in handler
    assert "this.sendPresence(" not in handler
    assert "this.broadcastLocalPresence()" not in handler
    assert "syncAvatarActivity(player, identity)" in scene_toggle
    assert "syncAvatarActivity(officeLobbyPlayer, identity)" in scene_toggle
    assert "updateAvatarBadge" not in scene_toggle


def test_movement_hot_path_reuses_vectors_and_reports_input_delay():
    movement = _section(SCENE, "  function movementInput(", "\n  function walkOfficeParticipant")
    town_walk = _section(SCENE, "  function walkPlayer(", "\n  function updateRemotePlayers")
    diagnostics = _section(SCENE, "  function getDiagnostics(", "\n  function dispose(")
    assert "movementVector.set(0, 0, 0)" in movement
    assert "movementForward.set(" in movement
    assert "movementRight.set(" in movement
    assert "new THREE.Vector3()" not in movement
    assert "return movementInputState" in movement
    assert "? topSpeed" in SCENE
    assert town_walk.count("queueMovementEvent({") == 2
    assert "onMovement({" not in town_walk
    assert "window.setTimeout(" in _section(
        SCENE,
        "  function queueMovementEvent(",
        "\n  function walkOfficeParticipant",
    )
    assert 'console.warn("[ForkMesh World] Movement input delayed"' in SCENE
    assert "inputResponseMs" in diagnostics
    assert "worstInputResponseMs" in diagnostics
    assert "movementInputMs: { caution: 34, high: 80 }" in APP


def test_camera_and_non_motion_frame_work_are_allocation_bounded():
    camera = _section(SCENE, "  function updateCamera(", "\n  function setSpawn")
    animate = _section(SCENE, "  function animate(", "\n  function setPaused(")
    assert "new THREE.Vector3()" not in camera
    assert "cameraEye.set(" in camera
    assert "cameraTarget" in camera
    assert "time >= nextProximityUpdateAt" in animate
    assert "time >= nextScreenLabelUpdateAt" in animate
    # Drawing-buffer inspection and console I/O are deferred until after the
    # already-late animation frame has yielded.
    warning = _section(
        SCENE,
        "  function scheduleRenderStallWarning(",
        "\n  function animate(",
    )
    assert "diagnosticsDrawingBuffer" in warning
    assert "window.setTimeout(" in warning
    assert "new Error(" not in warning


def test_zoom_and_selection_work_are_bounded_to_display_cadences():
    zoom = _section(SCENE, "  function setCameraZoom(", "\n  function touchDistance")
    lod = _section(
        SCENE,
        "  function updateSceneLevelOfDetail(",
        "\n  function updateWorldEnvironment",
    )
    animate = _section(SCENE, "  function animate(", "\n  function setPaused(")
    assert "updateSceneLevelOfDetail();" in zoom
    assert "updateSceneLevelOfDetail(true);" not in zoom
    assert "if (!force && farSceneDetail === far) return;" in lod
    assert "nextSceneLodAt = time + SCENE_LOD_SAMPLE_MS" in animate
    assert "time >= nextAvatarHighlightAt" in animate
    assert "nextAvatarHighlightAt = time + AVATAR_HIGHLIGHT_SAMPLE_MS" in animate


def test_stall_logs_are_aggregated_deferred_and_never_capture_stacks():
    animate = _section(SCENE, "  function animate(", "\n  function setPaused(")
    warning = _section(
        SCENE,
        "  function scheduleRenderStallWarning(",
        "\n  function animate(",
    )
    assert "const RENDER_STALL_LOG_COOLDOWN_MS = 30_000;" in SCENE
    assert "suppressedRenderStalls += 1" in animate
    assert "window.setTimeout(" in warning
    assert "console.warn(" in warning
    assert "new Error(" not in animate
    assert ".stack" not in animate


def test_repository_collision_basic_material_has_no_unsupported_emissive_field():
    collision = _section(
        SCENE,
        "    const collisionRing = new THREE.Mesh(",
        "\n    const sparkleCount",
    )
    assert "new THREE.MeshBasicMaterial({" in collision
    assert "emissive:" not in collision
