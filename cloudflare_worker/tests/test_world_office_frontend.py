#!/usr/bin/env python3
"""Contracts for the unified ForkMesh Office campus."""

import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OFFICE_PATH = ROOT / "public" / "world" / "world-office.js"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
TOWER_PATH = ROOT / "public" / "world" / "world-office-tower.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"


def source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def function_body(script, name):
    """Return one named JavaScript function body without snapshotting formatting."""

    match = re.search(rf"\bfunction\s+{re.escape(name)}\s*\(", script)
    assert match, f"missing JavaScript helper: {name}"
    parameter_opening = script.find("(", match.start())
    parameter_depth = 0
    parameter_closing = -1
    for index in range(parameter_opening, len(script)):
        if script[index] == "(":
            parameter_depth += 1
        elif script[index] == ")":
            parameter_depth -= 1
            if parameter_depth == 0:
                parameter_closing = index
                break
    assert parameter_closing >= 0, f"unterminated parameters: {name}"
    opening = script.find("{", parameter_closing)
    assert opening >= 0, f"missing JavaScript helper body: {name}"
    depth = 0
    for index in range(opening, len(script)):
        if script[index] == "{":
            depth += 1
        elif script[index] == "}":
            depth -= 1
            if depth == 0:
                return script[opening + 1:index]
    raise AssertionError(f"unterminated JavaScript helper: {name}")


def test_office_controller_module_exists_and_proximity_is_hysteretic():
    assert OFFICE_PATH.exists()
    script = f"""
      import {{ nextOfficeZoneState }} from {json.dumps(OFFICE_PATH.as_uri())};
      process.stdout.write(JSON.stringify([
        nextOfficeZoneState("distant", 6.6),
        nextOfficeZoneState("distant", 6.5),
        nextOfficeZoneState("nearby", 7.4),
        nextOfficeZoneState("nearby", 7.6),
      ]));
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    assert json.loads(result.stdout) == [
        "distant",
        "nearby",
        "nearby",
        "distant",
    ]


def test_office_controller_validates_fallback_bridge_and_unloads_hidden_chat():
    office = source(OFFICE_PATH)
    assert 'const OFFICE_CHAT_PATH = "/chat?embed=office"' in office
    assert "event.origin !== window.location.origin" in office
    assert "event.source !== frame.contentWindow" in office
    assert 'type: "office-chat-suspend"' in office
    assert "const OFFICE_UNLOAD_DELAY_MS = 2000" in office
    for forbidden in (
        "plaintext",
        "roomKey",
        "attachment",
        "privateChannelId",
    ):
        assert forbidden not in office[
            office.index("function closeFallback"):
            office.index("function setProximity")
        ]


def test_office_controller_exposes_deliberate_entry_and_exit_lifecycle():
    office = source(OFFICE_PATH)
    for contract in (
        "export function createWorldOfficeController",
        "function setProximity",
        "function focusOffice",
        "async function enterOffice",
        "function collapse",
        "function completeOfficeExit",
        "function destroy",
        'world.focusLandmark("office")',
        "world.enterOffice()",
        "world.beginOfficeExit?.()",
        "world.setOfficeExitHandler?.(completeOfficeExit)",
    ):
        assert contract in office

    collapse = office[
        office.index("function collapse()"):
        office.index("function onClick")
    ]
    assert "meeting.leaveOffice()" not in collapse
    complete = office[
        office.index("function completeOfficeExit()"):
        office.index("function collapse()")
    ]
    assert "meeting.leaveOffice()" in complete


def test_reentry_reloads_a_fallback_frame_suspended_during_the_grace_period():
    office = source(OFFICE_PATH)
    assert "let frameSuspended = false" in office
    assert "if (frameSuspended)" in office
    assert "frameSuspended = true" in office


def test_entry_is_walk_through_for_everyone_and_the_keypad_protocol_is_absent():
    office = source(OFFICE_PATH)
    world = source(WORLD_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        'const OFFICE_ENTRY_PATH = "/api/world/office/general/entry"',
        'const OFFICE_FLOORS_PATH = "/api/world/office/floors"',
        "function authenticatedSession()",
        "const entered = completeOfficeEntry()",
        "void refreshOfficeAuthorization(",
        "async function authorizeMeeting()",
        "meeting.openLobby()",
    ):
        assert contract in office
    assert "data-world-office-prompt" not in world
    assert "data-world-office-enter" not in world
    assert "greetGuest" not in office
    assert "LOGIN_REQUIRED_MESSAGE" not in office
    for body in (office, world, scene):
        lowered = body.lower()
        assert "office/general/code" not in lowered
        assert "office_code_key" not in lowered
        assert "officekeypad" not in lowered
        assert "world-office-keypad" not in lowered
    assert "window.prompt" not in office
    assert "localStorage" not in office
    assert "sessionStorage" not in office


def test_entrance_uses_two_proximity_sliding_panels_without_a_hinged_door():
    scene = source(SCENE_PATH)
    for contract in (
        'slidingDoors.name = "forkmesh-office-sliding-doors"',
        "const doorPanels = [-1, 1].map((side) => {",
        "officeSlidingDoorPanels.forEach((panel, index) => {",
        "Math.abs(localPosition.z - OFFICE_FRONT_Z) <= 7.5",
        "officeDoorwayEntryPending || officeExitPending",
        "THREE.MathUtils.lerp(",
    ):
        assert contract in scene
    assert "forkmesh-office-door-pivot" not in scene
    assert "officeInteriorDoorPivot" not in scene


def test_attendance_is_one_shared_last_twenty_row_ledger():
    office = source(OFFICE_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        'const OFFICE_ATTENDANCE_PATH = "/api/world/office/attendance"',
        "async function loadAttendance()",
        "function recordAttendance(direction)",
        "visits.slice(0, 20)",
        "void loadAttendance()",
        'const action = direction === "out" ? "out" : "in"',
        "attendanceWrite = attendanceWrite",
    ):
        assert contract in office
    for contract in (
        "OFFICE · LAST 20 VISITS",
        'context.fillText("USER"',
        'context.fillText("IN"',
        'context.fillText("OUT"',
        "visit?.inAt",
        "visit?.outAt",
        "IN BUILDING",
    ):
        assert contract in scene


def test_floor_access_is_loaded_once_and_only_server_grants_unlock_buttons():
    office = source(OFFICE_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        "async function loadFloorAccess(activeSession)",
        "const payload = await root.fetchJSON(OFFICE_FLOORS_PATH",
        "officeAccess = floorAccess",
        "world.setOfficeAccess?.(officeAccess)",
        "generation !== authorizationGeneration",
        "canAccessOfficeFloor(officeAccess, floorId)",
        "world.setOfficeFloorHandler?.(travelToOfficeFloor)",
    ):
        assert contract in office
    for contract in (
        "function setOfficeAccess(payload = {})",
        "officeFloorAccess = normalizeOfficeFloorAccess(payload)",
        "button.userData.officeFloorAllowed = allowed",
        'button.userData.interactive = "office-elevator-floor"',
        "officeFloorHandler?.({",
        "floorId: hit.object.userData.officeFloorId",
    ):
        assert contract in scene


def test_office_is_a_remote_island_reached_by_a_glass_bridge():
    scene = source(SCENE_PATH)
    tower = source(TOWER_PATH)
    for contract in (
        'officeIsland.name = "forkmesh-office-island"',
        "new THREE.CircleGeometry(OFFICE_ISLAND_RADIUS, 128)",
        'officeBridge.name = "forkmesh-office-bridge"',
        "new THREE.BoxGeometry(OFFICE_BRIDGE_WIDTH, 0.3, officeBridgeLength)",
        "const bridgeGlass = makeMaterial",
        "world.add(officeIsland)",
        "world.add(officeBridge)",
    ):
        assert contract in scene
    for contract in (
        "OFFICE_ISLAND_CENTER = Object.freeze([0, 0, -215])",
        "OFFICE_ISLAND_RADIUS = 110",
        "OFFICE_BRIDGE_WIDTH = 12",
    ):
        assert contract in tower


def test_office_glass_uses_one_stable_non_depth_writing_envelope():
    scene = source(SCENE_PATH)
    material_helper = scene[
        scene.index("function makeMaterial("):
        scene.index("function setShadows(")
    ]
    exterior = scene[
        scene.index('const glass = makeMaterial(THREE, "#9ef7c6"'):
        scene.index("const doorMaterial = makeMaterial", scene.index(
            'const glass = makeMaterial(THREE, "#9ef7c6"'
        ))
    ]
    assert "parameters.depthWrite = options.depthWrite" in material_helper
    for contract in (
        "opacity: 0.24",
        "metalness: 0",
        "roughness: 0.62",
        "depthWrite: false",
    ):
        assert contract in exterior
    assert "officeFloorGlassMaterial" not in scene
    assert (
        "new THREE.BoxGeometry(0.18, OFFICE_FLOOR_HEIGHT - 0.7, OFFICE_DEPTH)"
        not in scene
    )
    assert "child.material?.transparent" in scene
    assert "child.castShadow = false" in scene
    assert "child.receiveShadow = false" in scene


def test_tower_is_ten_stories_and_about_ten_times_the_old_width():
    scene = source(SCENE_PATH)
    tower = source(TOWER_PATH)
    assert "export const OFFICE_WIDTH = 170" in tower
    assert "export const OFFICE_FLOOR_COUNT = 10" in tower
    assert "export const OFFICE_FLOOR_HEIGHT = 16" in tower
    assert tower.count("level: ") == 10
    for floor_id in (
        "lobby",
        "marketing",
        "engineering",
        "product-design",
        "security",
        "infrastructure",
        "community",
        "partnerships",
        "operations",
        "rooftop",
    ):
        assert f'id: "{floor_id}"' in tower
    assert "for (let level = 1; level < OFFICE_FLOOR_COUNT; level += 1)" in scene
    assert "OFFICE_FLOORS.slice(1).forEach((floor) => {" in scene
    assert "function addOfficeFunFloorProps()" in scene


def test_tall_floor_exhibits_and_elevator_openings_stay_between_slabs():
    scene = source(SCENE_PATH)
    for contract in (
        "function addOfficeFloorSurface(parent, material)",
        "elevatorCutMinX",
        "elevatorCutMaxX",
        "elevatorCutMinZ",
        "forkmesh-office-floor-slab-",
        "new THREE.TorusKnotGeometry(3.8, 0.6",
        'shield.name = "forkmesh-office-feature-security-shield"',
        "OFFICE_FLOOR_HEIGHT / 2",
        "orbitPivot.rotation.y",
        "forkmesh-office-feature-partnerships-orbit-",
    ):
        assert contract in scene
    # These rotations were the source of the giant pink/cyan shapes visibly
    # slicing through adjacent floors.
    assert "new THREE.TorusKnotGeometry(8, 1.35" not in scene
    assert "orbit.rotation.z" not in scene


def test_office_remains_in_the_world_instead_of_swapping_to_another_scene():
    scene = source(SCENE_PATH)
    for contract in (
        'officeInterior.name = "forkmesh-office-interior"',
        "officeInterior.visible = true",
        "world.add(officeInterior)",
        "The tower is physically part of the World.",
        "the campus, Town Square, sky, and other",
        'currentSpace = `office-${destinationFloor.id}`',
    ):
        assert contract in scene
    enter = scene[
        scene.index("function enterOfficeLobby("):
        scene.index("function officeChairTransform")
    ]
    assert "world.visible = false" not in enter
    assert "scene.remove(world)" not in enter


def test_office_entry_preserves_the_live_player_pose_and_camera_controls():
    scene = source(SCENE_PATH)
    prepare = function_body(scene, "enterOffice")
    enter = function_body(scene, "enterOfficeLobby")
    local_position = function_body(scene, "officeAvatarLocalPosition")

    # Admission must not stage a second teleport before the lobby handoff.
    for hard_snap in (
        "player.position.set",
        "camera.position.copy",
        "camera.lookAt",
    ):
        assert hard_snap not in prepare

    # The threshold helper converts the live avatar's world pose into Office
    # coordinates. It keeps the crossing beside the physical entrance instead
    # of spawning the visitor deep inside the lobby.
    assert "worldToLocal" in local_position
    assert "OFFICE_FRONT_Z" in local_position
    assert re.search(
        r"OFFICE_(?:AVATAR_RADIUS|INTERIOR_(?:EXIT_Z|WALL_LIMIT))",
        local_position,
    )
    assert "officeAvatarLocalPosition(" in enter

    # Entry continues with the same player object. A second local avatar, a
    # forced camera mode, or rewritten orbit state would read as a scene cut.
    assert "player" in enter
    for swap_or_snap in (
        "officeLobbyPlayer.position.set",
        "player.visible = false",
        "setCameraMode(",
        "cameraYaw =",
        "cameraPitch =",
        "cameraZoom =",
        "camera.position.copy",
        "camera.lookAt",
    ):
        assert swap_or_snap not in enter


def test_office_walkers_share_world_movement_tuning_and_heading():
    scene = source(SCENE_PATH)
    speed = function_body(scene, "movementSpeedForInput")
    walkers = {
        name: function_body(scene, name)
        for name in (
            "walkPlayer",
            "walkOfficeLobbyPlayer",
            "walkOfficeParticipant",
        )
    }

    # One cadence owns keyboard acceleration, analog strength, and the user's
    # per-device tuning in every part of the continuous World.
    for contract in (
        "PLAYER_MAX_SPEED",
        "PLAYER_ACCELERATION",
        "moveSpeedScale",
        "moveAccelScale",
        "keyboardMovementSpeed",
        "inputStrength",
    ):
        assert contract in speed
    assert "movementSpeedForInput(" in walkers["walkPlayer"]
    assert "movementSpeedForInput(" in walkers["walkOfficeLobbyPlayer"]
    assert "movementSpeedForInput(" in walkers["walkOfficeParticipant"]

    heading = re.compile(
        r"Math\.atan2\(\s*-\s*movement\.x\s*,\s*-\s*movement\.z\s*\)"
    )
    for name, body in walkers.items():
        assert "PLAYER_SPEED * 0.72" not in body, (
            f"{name} bypasses the shared movement tuning"
        )
        assert heading.search(body), f"{name} uses a different avatar heading"


def test_meeting_handoff_projects_the_live_player_out_of_collidable_furniture():
    scene = source(SCENE_PATH)
    enter = function_body(scene, "enterOfficeLobby")
    nearest = function_body(scene, "nearestOfficeWalkablePosition")
    participants = function_body(scene, "setOfficeParticipants")

    assert "nearestOfficeWalkablePosition(" in enter
    assert "setOfficeParticipants([])" in enter
    assert "officeInteriorPointIsWalkable(" in nearest
    assert "OFFICE_AVATAR_RADIUS" in nearest
    assert "cameraMode !== \"first-person\"" in participants


def test_office_third_person_camera_distance_is_bounded_by_local_geometry():
    scene = source(SCENE_PATH)
    limiter = function_body(scene, "officeCameraDistanceLimit")
    update = function_body(scene, "updateCamera")

    # The limiter must be part of the live third-person camera calculation,
    # rather than a constant Office zoom step applied only during entry.
    assert "officeCameraDistanceLimit(" in update
    assert "officeSceneMode" in update

    # Account for both the tower envelope and the semi-exterior elevator. The
    # exact ray/AABB implementation may change; these semantic dependencies and
    # a real upper-bound operation are the stable behavior contract.
    assert "OFFICE_WIDTH" in limiter
    assert "OFFICE_DEPTH" in limiter or "OFFICE_FRONT_Z" in limiter
    assert re.search(r"elevator", limiter, re.IGNORECASE)
    assert "Math.min" in limiter or "clamp(" in limiter
    cabin_detection = limiter.index("const ridingElevator")
    rooftop_bypass = limiter.index(
        'officeCurrentFloorId === "rooftop" && !ridingElevator'
    )
    assert cabin_detection < rooftop_bypass
    assert "return requested;" in limiter[rooftop_bypass:]


def test_lobby_has_two_greeters_attendance_and_the_reflective_logo_fountain():
    scene = source(SCENE_PATH)
    for contract in (
        'officeReception.name = "forkmesh-office-reception"',
        'id: "office-greeter-maya"',
        'id: "office-greeter-noah"',
        'receptionDesk.name = "forkmesh-office-reception-desk"',
        "receptionDesk.position.set(0, 1.05, -36.5)",
        "avatar.position.set(staff.x, 0.38, -40)",
        "avatar.rotation.y = Math.PI",
        "WELCOME · WALK RIGHT IN",
        "officeGreetingBoard.position.set(0, 6.25, -OFFICE_FRONT_Z + 0.5)",
        'officeAttendanceBoard.name = "forkmesh-office-attendance"',
        "OFFICE · LAST 20 VISITS",
        "visits.slice(0, 20)",
        "IN BUILDING",
        "function setOfficeAttendance(event = {})",
        'logoFountain.name = "forkmesh-office-logo-fountain"',
        'chromeCube.name = "forkmesh-reflective-fm-cube"',
        'chromeMark.name = "forkmesh-reflective-fm-cube-fixed-tilt"',
        "chromeMark.quaternion.setFromUnitVectors(",
        "addFLogoFace(2.68, 0)",
        "addFLogoFace(-2.68, Math.PI)",
        "addMLogoFace(2.68, Math.PI / 2)",
        "addMLogoFace(-2.68, -Math.PI / 2)",
        "forkmesh-reflective-fm-panel-",
        'logoContactPoint.name = "forkmesh-reflective-fm-cube-contact-point"',
        'logoSupport.name = "forkmesh-reflective-fm-cube-support"',
        "new THREE.WebGLCubeRenderTarget(",
        "new THREE.CubeCamera(",
        "metalness: 1",
        "chromeCube.rotation.y = time * 0.00022",
        "reflectionCamera.update(renderer, scene)",
    ):
        assert contract in scene
    logo = scene[
        scene.index('chromeCube.name = "forkmesh-reflective-fm-cube"'):
        scene.index("let lastLogoReflectionAt")
    ]
    assert "const cubeBody" not in logo
    assert "new THREE.BoxGeometry(5.6, 5.6, 5.6" not in logo
    animation = scene[
        scene.index("const logoReflectionIntervalMs"):
        scene.index("// One physical selector rides inside")
    ]
    assert "chromeMark.rotation" not in animation
    assert "chromeMark.quaternion" not in animation


def test_elevator_animates_between_floors_and_emits_departure_and_arrival_audio():
    scene = source(SCENE_PATH)
    for contract in (
        "function travelToOfficeFloor(floorId)",
        "!canAccessOfficeFloor(officeFloorAccess, floor.id)",
        "officeElevatorRide = {",
        "toY: officeFloorY(floor.id) + 0.38",
        'onOfficeElevatorSound("depart"',
        "function updateOfficeElevator(time)",
        "officeCurrentFloorId = ride.floorId",
        'currentSpace = `office-${ride.floorId}`',
        'onOfficeElevatorSound("arrive"',
    ):
        assert contract in scene


def test_elevator_has_one_stable_car_glass_layer_and_idle_lobby_reflections():
    scene = source(SCENE_PATH)
    elevator = scene[
        scene.index('officeElevatorShaft.name = "forkmesh-office-glass-elevator-shaft"'):
        scene.index("const elevatorPanelGeometry")
    ]
    reflection = scene[
        scene.index("const logoReflectionIntervalMs"):
        scene.index("// One physical selector rides inside")
    ]
    assert "const elevatorCarGlass = makeMaterial" in elevator
    assert "elevatorShaftGlass" not in elevator
    assert "new THREE.BoxGeometry(0.12, OFFICE_TOWER_HEIGHT" not in elevator
    car_glass = elevator[
        elevator.index("const elevatorCarGlass = makeMaterial"):
        elevator.index("for (const [x, z] of")
    ]
    door_glass = elevator[
        elevator.index('makeMaterial(THREE, "#d8fff6"'):
        elevator.index("door.position.set")
    ]
    for material in (car_glass, door_glass):
        assert "depthWrite: false" in material
        assert "metalness: 0," in material
    for contract in (
        'officeSceneMode === "lobby"',
        'officeCurrentFloorId === "lobby"',
        "!officeElevatorRide",
        "!officeLobbyPlayerMoving",
        "primaryPointerId === null",
        "!pinchActive",
    ):
        assert contract in reflection


def test_rooftop_has_glass_safety_barriers_and_office_jumping_is_disabled():
    scene = source(SCENE_PATH)
    assert 'const rooftop = officeFloorGroups.get("rooftop")' in scene
    assert 'const roofGlass = makeMaterial(THREE, "#d8ffff"' in scene
    assert "transparent: true" in scene
    roof_glass = scene[
        scene.index('const roofGlass = makeMaterial(THREE, "#d8ffff"'):
        scene.index("for (const [width, depth, x, z]", scene.index(
            'const roofGlass = makeMaterial(THREE, "#d8ffff"'
        ))
    ]
    for contract in (
        "opacity: 0.24",
        "metalness: 0",
        "roughness: 0.54",
        "depthWrite: false",
    ):
        assert contract in roof_glass
    assert "rooftop.add(barrier)" in scene
    jump = scene[
        scene.index('if (event.code === "Space")'):
        scene.index('if (event.code === "KeyR"')
    ]
    assert 'officeSceneMode === "town"' in jump
    assert "!officeCampusSurfaceContains(player.position.x, player.position.z)" in jump


def test_walk_surfaces_and_every_floor_use_real_collision_constraints():
    scene = source(SCENE_PATH)
    for contract in (
        "function worldWalkSurfaceContains(x, z, radius = OFFICE_AVATAR_RADIUS)",
        "return officeCampusSurfaceContains(px, pz, margin)",
        "function constrainTownOfficeWalls(previousPosition)",
        "function constrainOfficeInteriorWalls(avatar, previousPosition)",
        "officeInteriorPointIsWalkable(",
        "position.z = previous.z",
        "position.x = previous.x",
        "player.position.x = previousHorizontalPosition.x",
        "player.position.z = previousHorizontalPosition.z",
    ):
        assert contract in scene


def test_walking_through_the_doorway_enters_immediately_and_hydrates_access():
    office = source(OFFICE_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        "let officeDoorwayEntryPending = false;",
        "let officeDoorwayEntryArmed = true;",
        "const crossedDoorway =",
        "officeDoorwayEntryArmed &&",
        "!officeDoorwayEntryPending",
        "officeDoorwayEntryArmed = false;",
        "officeDoorwayEntryPending = true;",
        'source: "doorway"',
        "function setOfficeDoorwayEntryPending(pending = false)",
        "setOfficeDoorwayEntryPending,",
    ):
        assert contract in scene
    entry = office[
        office.index("async function enterOffice(entry = {})"):
        office.index("function completeOfficeExit()")
    ]
    assert 'entry?.source === "doorway"' in entry
    assert "const entered = completeOfficeEntry()" in entry
    assert "void refreshOfficeAuthorization(" in entry
    assert "requestOfficeEntry" not in entry
    assert "world.setOfficeDoorwayEntryPending?.(false)" in entry


def test_scene_returns_the_new_office_control_surface():
    scene = source(SCENE_PATH)
    returned = scene[scene.rindex("return {"):]
    for contract in (
        "setOfficeExitHandler",
        "setOfficeDoorwayEntryPending",
        "setOfficeFloorHandler",
        "setOfficeAccess",
        "setOfficeAttendance",
        "travelToOfficeFloor",
        "enterOfficeLobby",
        "leaveOfficeInterior",
    ):
        assert contract in returned
