#!/usr/bin/env python3
"""Contracts for the unified, authenticated ForkMesh Office campus."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OFFICE_PATH = ROOT / "public" / "world" / "world-office.js"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
TOWER_PATH = ROOT / "public" / "world" / "world-office-tower.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"


def source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


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


def test_entry_is_login_only_and_the_retired_keypad_protocol_is_absent():
    office = source(OFFICE_PATH)
    world = source(WORLD_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        'const OFFICE_ENTRY_PATH = "/api/world/office/general/entry"',
        'const OFFICE_FLOORS_PATH = "/api/world/office/floors"',
        "function authenticatedSession()",
        "if (!activeSession) return greetGuest()",
        "root.postJSON(",
        "OFFICE_ENTRY_PATH,\n        {},",
        "await loadFloorAccess(activeSession)",
        "world.greetOfficeGuest?.(LOGIN_REQUIRED_MESSAGE)",
    ):
        assert contract in office
    for body in (office, world, scene):
        lowered = body.lower()
        assert "office/general/code" not in lowered
        assert "office_code_key" not in lowered
        assert "officekeypad" not in lowered
        assert "world-office-keypad" not in lowered
    assert "window.prompt" not in office
    assert "localStorage" not in office
    assert "sessionStorage" not in office


def test_floor_access_is_loaded_once_and_only_server_grants_unlock_buttons():
    office = source(OFFICE_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        "async function loadFloorAccess(activeSession)",
        "payload = await root.fetchJSON(OFFICE_FLOORS_PATH",
        'allowedFloorIds: ["lobby", "marketing", "rooftop"]',
        "world.setOfficeAccess?.(officeAccess)",
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


def test_tower_is_ten_stories_and_about_ten_times_the_old_width():
    scene = source(SCENE_PATH)
    tower = source(TOWER_PATH)
    assert "export const OFFICE_WIDTH = 170" in tower
    assert "export const OFFICE_FLOOR_COUNT = 10" in tower
    assert "export const OFFICE_FLOOR_HEIGHT = 8" in tower
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


def test_lobby_has_two_greeters_attendance_and_the_reflective_logo_fountain():
    scene = source(SCENE_PATH)
    for contract in (
        'officeReception.name = "forkmesh-office-reception"',
        'id: "office-greeter-maya"',
        'id: "office-greeter-noah"',
        "WELCOME · SIGN IN TO VISIT THE OFFICES",
        'officeAttendanceBoard.name = "forkmesh-office-attendance"',
        "function setOfficeAttendance(event = {})",
        'logoFountain.name = "forkmesh-office-logo-fountain"',
        'chromeCube.name = "forkmesh-reflective-fm-cube"',
        "new THREE.WebGLCubeRenderTarget(",
        "new THREE.CubeCamera(",
        "metalness: 1",
        "chromeCube.rotation.y = time * 0.00022",
        "reflectionCamera.update(renderer, scene)",
    ):
        assert contract in scene


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


def test_rooftop_has_glass_safety_barriers_and_office_jumping_is_disabled():
    scene = source(SCENE_PATH)
    assert 'const rooftop = officeFloorGroups.get("rooftop")' in scene
    assert 'const roofGlass = makeMaterial(THREE, "#d8ffff"' in scene
    assert "transparent: true" in scene
    assert "opacity: 0.36" in scene
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
        "avatar.position.z = previous.z",
        "avatar.position.x = previous.x",
        "player.position.x = previousHorizontalPosition.x",
        "player.position.z = previousHorizontalPosition.z",
    ):
        assert contract in scene


def test_walking_through_the_doorway_requests_authenticated_admission_once():
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
    assert "return await requestOfficeEntry()" in entry
    assert "world.setOfficeDoorwayEntryPending?.(false)" in entry


def test_scene_returns_the_new_office_control_surface():
    scene = source(SCENE_PATH)
    returned = scene[scene.rindex("return {"):]
    for contract in (
        "setOfficeExitHandler",
        "setOfficeDoorwayEntryPending",
        "setOfficeFloorHandler",
        "setOfficeAccess",
        "greetOfficeGuest",
        "setOfficeAttendance",
        "travelToOfficeFloor",
        "enterOfficeLobby",
        "leaveOfficeInterior",
    ):
        assert contract in returned
