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
DATA_PATH = ROOT / "public" / "world" / "world-data.js"


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
        "world.enterOffice({ source })",
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
    data = source(DATA_PATH)
    for contract in (
        'const OFFICE_ENTRY_PATH = "/api/world/office/general/entry"',
        'const OFFICE_FLOORS_PATH = "/api/world/office/floors"',
        "function authenticatedSession()",
        "const entered = completeOfficeEntry({",
        "loadPublicAttendance: !activeSession",
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
    assert "Building admission is open to every visitor." in data
    assert "Everyone, including guests, can cross the bridge and enter the lobby." \
        in data
    assert "Building admission requires an active account." not in data
    assert "must sign in before entering the tower" not in data


def test_office_doorway_has_no_concrete_sill_or_raised_door_panels():
    scene = source(SCENE_PATH)
    office_start = scene.index("function createForkMeshOffice(")
    office_end = scene.index("\nfunction ", office_start + 1)
    office = scene[office_start:office_end]

    assert "OFFICE_DOOR_SILL_Y" not in scene
    assert "const OFFICE_DOOR_HEIGHT = OFFICE_HEIGHT;" in scene
    assert "[-OFFICE_WIDTH / 2, -doorWidth / 2]" in office
    assert "[doorWidth / 2, elevatorFacadeMinX]" in office
    assert "doorHeight / 2," in office
    assert "portalY >= 0.08" in scene
    assert "portalY <= OFFICE_DOOR_HEIGHT - 0.08" in scene


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
    door_material = scene[
        scene.index('const doorMaterial = makeMaterial(THREE, "#c9fff3"'):
        scene.index("const elevatorFacadeMinX")
    ]
    for contract in (
        "transparent: true",
        "opacity: 0.3",
        "metalness: 0",
        "roughness: 0.38",
        "depthWrite: false",
    ):
        assert contract in door_material
    assert "forkmesh-office-door-pivot" not in scene
    assert "officeInteriorDoorPivot" not in scene


def test_attendance_is_one_shared_last_twenty_row_ledger():
    office = source(OFFICE_PATH)
    scene = source(SCENE_PATH)
    for contract in (
        'const OFFICE_ATTENDANCE_PATH = "/api/world/office/attendance"',
        "async function loadAttendance()",
        "function recordAttendance(",
        "visits.slice(0, 20)",
        "if (loadPublicAttendance) void loadAttendance()",
        "loadPublicAttendance: !activeSession",
        "loadPublicFallback = true",
        "generation === authorizationGeneration",
        'direction === "heartbeat"',
        "attendanceWrite = attendanceWrite",
    ):
        assert contract in office
    for contract in (
        "OFFICE · LAST 20 VISITS",
        'context.fillText("USER"',
        'context.fillText("IN"',
        'context.fillText("OUT"',
        'context.fillText("FLOOR"',
        'context.fillText("TOTAL"',
        "visit?.inAt",
        "visit?.outAt",
        "visit?.durationMs",
        "officeAttendanceDurationLabel",
        "updateOfficeAttendanceClock",
        "Math.floor(elapsedMs / 30_000)",
        "IN BUILDING",
        'String(visit?.floor || "Lobby")',
    ):
        assert contract in scene
    for contract in (
        "OFFICE_ATTENDANCE_HEARTBEAT_MS = 30_000",
        'direction === "heartbeat"',
        "{ action, floor: attendanceFloorId }",
        "startAttendanceHeartbeat()",
        "stopAttendanceHeartbeat()",
    ):
        assert contract in office


def test_attendance_duration_labels_are_compact_and_reject_bad_values():
    script = f"""
      import {{ officeAttendanceDurationLabel }} from {
          json.dumps(SCENE_PATH.as_uri())
      };
      process.stdout.write(JSON.stringify([
        officeAttendanceDurationLabel(null),
        officeAttendanceDurationLabel(-1),
        officeAttendanceDurationLabel(0),
        officeAttendanceDurationLabel(59_999),
        officeAttendanceDurationLabel(60_000),
        officeAttendanceDurationLabel(3_720_000),
        officeAttendanceDurationLabel(90_000_000),
      ]));
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    assert json.loads(result.stdout) == [
        "—", "—", "0m", "0m", "1m", "1h 02m", "1d 01h",
    ]


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


def test_explicit_floor_refresh_updates_access_without_attendance_or_polling():
    script = f"""
      globalThis.HTMLElement = class {{}};
      globalThis.window = {{
        location: {{ origin: "https://forkmesh.test" }},
        setTimeout,
        clearTimeout,
        requestAnimationFrame(callback) {{ callback(); }},
        addEventListener() {{}},
        removeEventListener() {{}},
      }};

      const {{ createWorldOfficeController }} = await import({
          json.dumps(OFFICE_PATH.as_uri())
      });
      let session = null;
      const gets = [];
      const posts = [];
      const applied = [];
      const root = {{
        querySelector() {{ return null; }},
        addEventListener() {{}},
        removeEventListener() {{}},
        async fetchJSON(path) {{
          gets.push(path);
          if (path === "/api/world/office/floors") {{
            return {{
              authenticated: true,
              account: "alice",
              allowedFloorIds: ["engineering"],
              teams: ["engineering"],
            }};
          }}
          if (path === "/api/world/office/attendance") return {{ visits: [] }};
          throw new Error(`unexpected GET ${{path}}`);
        }},
        async postJSON(path, payload, options) {{
          posts.push({{ path, payload, options }});
          return {{}};
        }},
        toast() {{}},
      }};
      const world = {{
        enterOffice() {{ return true; }},
        setOfficeAccess(access) {{
          applied.push({{
            authenticated: access.authenticated,
            account: access.account,
            allowedFloorIds: [...access.allowedFloorIds],
          }});
        }},
        setOfficeExitHandler() {{}},
        setOfficeMeetingHandler() {{}},
        setOfficeDoorwayEntryPending() {{}},
      }};
      const meeting = {{
        inRoom: false,
        openLobby() {{}},
        leaveOffice() {{}},
        setEntryTicket() {{}},
        destroy() {{}},
      }};
      const controller = createWorldOfficeController({{
        root,
        world,
        meeting,
        getSession: () => session,
      }});

      const entered = await controller.enterOffice({{ source: "doorway" }});
      session = {{ nodeName: "alice", sessionToken: "signed-token" }};
      const refreshed = await controller.refreshAuthorization();
      controller.destroy();
      process.stdout.write(JSON.stringify({{
        entered,
        refreshed,
        floorGets: gets.filter(
          (path) => path === "/api/world/office/floors",
        ).length,
        attendanceGets: gets.filter(
          (path) => path === "/api/world/office/attendance",
        ).length,
        postCount: posts.length,
        finalAccess: applied.at(-2),
      }}));
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    payload = json.loads(result.stdout)
    assert payload["entered"] is True
    assert payload["refreshed"] is True
    assert payload["floorGets"] == 1
    # The guest entry may read the public ledger once. The explicit
    # authorization refresh adds no attendance read or write.
    assert payload["attendanceGets"] == 1
    assert payload["postCount"] == 0
    assert payload["finalAccess"] == {
        "authenticated": True,
        "account": "alice",
            "allowedFloorIds": [
                "engineering",
                "lobby",
                "rooftop",
            ],
    }

    office = source(OFFICE_PATH)
    refresh = function_body(office, "refreshAuthorization")
    assert "setInterval" not in refresh
    assert "recordAttendance" not in refresh
def test_marketing_is_a_team_floor_reached_only_through_a_server_grant():
    tower = source(TOWER_PATH)
    scene = source(SCENE_PATH)
    access = function_body(tower, "normalizeOfficeFloorAccess")
    assert 'supplied.add("lobby")' in access
    assert 'supplied.add("rooftop")' in access
    # Signing in must not hand out the Marketing studio. Only the server's
    # team-derived allowlist may add that floor.
    assert 'supplied.add("marketing")' not in access
    marketing_floor = tower[tower.index('id: "marketing"'):]
    marketing_floor = marketing_floor[:marketing_floor.index("}),")]
    assert "publicForMembers" not in marketing_floor
    # The meeting table is the other door onto that storey.
    meeting = function_body(scene, "enterOfficeMeeting")
    assert 'canAccessOfficeFloor(officeFloorAccess, "marketing")' in meeting
    board = scene[scene.index('=== "office-meeting-board"'):]
    board = board[:board.index("onOfficeMeetingBoardSelect()")]
    assert 'officeCurrentFloorId === "marketing"' in board


def test_an_arrival_cell_never_drops_a_visitor_off_their_office_floor():
    scene = source(SCENE_PATH)
    app = source(WORLD_PATH)
    spawn = function_body(scene, "setSpawn")
    # Arrival cells are outdoor ground spots. Applying one to a visitor riding
    # the tower teleported them from their storey down onto the lobby slab.
    guard = 'if (officeSceneMode !== "town") return false;'
    assert guard in spawn
    assert spawn.index(guard) < spawn.index("player.position.set(")
    assert "return true;" in spawn
    welcome = app[app.index("const spawnBlocked ="):]
    welcome = welcome[:welcome.index("window.clearTimeout(this.peerGraceTimer)")]
    assert "this.initialPresenceWelcomePending &&" in welcome
    assert "const relocated = this.world?.setSpawn?.({" in welcome
    assert "if (relocated !== false) {" in welcome
    assert welcome.index("const relocated") < welcome.index("this.currentSpace =")


def test_saved_office_views_use_normal_attendance_entry_and_exit():
    office = source(OFFICE_PATH)
    restore = function_body(office, "restoreSavedView")
    assert 'source: "saved-view"' in restore
    assert "refreshOfficeAuthorization(" in restore
    assert 'recordEntry: true' in restore
    assert "world.leaveOfficeInterior?.();" in restore
    assert "completeOfficeExit();" in restore
    assert "world.restoreSavedViewState?.(view)" in restore
    scene = source(SCENE_PATH)
    enter = function_body(scene, "enterOffice")
    assert '["doorway", "saved-view"].includes(source)' in enter


def test_town_camera_cannot_orbit_through_the_ground():
    scene = source(SCENE_PATH)
    limiter = function_body(scene, "groundCameraDistanceLimit")
    assert 'officeSceneMode !== "town"' in limiter
    assert "headroom / -rise" in limiter
    update = function_body(scene, "updateCamera")
    assert "groundCameraDistanceLimit(" in update
    assert "WORLD_GROUND_Y + CAMERA_GROUND_CLEARANCE" in update


def test_marketing_task_board_is_blank_while_the_room_is_empty():
    scene = source(SCENE_PATH)
    blank = function_body(scene, "officeMarketingTasksBlankTexture")
    assert "fillRect(0, 0, 2048, 1024)" in blank
    occupancy = function_body(scene, "officeMarketingRoomOccupied")
    assert "officeParticipants.size > 0" in occupancy
    assert 'officeCurrentFloorId === "marketing"' in occupancy
    render = function_body(scene, "renderOfficeMarketingTasks")
    assert "officeMarketingTasksBlankTexture(THREE)" in render
    assert '? officeMarketingTaskSnapshot.state' in render
    assert ': "vacant"' in render
    update = function_body(scene, "updateOfficeMarketingTasks")
    assert "officeMarketingTaskSnapshot = normalizeOfficeMarketingTasks(payload)" in update


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


def test_office_walkway_meets_the_lobby_without_a_gap_or_step():
    scene = source(SCENE_PATH)
    assert "const OFFICE_LOBBY_SURFACE_Y = 0.38;" in scene
    assert "const officeEntranceZ =" in scene
    assert "Math.abs(officeEntranceZ - OFFICE_BRIDGE_END_Z)" in scene
    assert "(OFFICE_BRIDGE_END_Z + officeEntranceZ) / 2" in scene
    assert "officeBridgeDeck.position.y = OFFICE_LOBBY_SURFACE_Y - 0.15" in scene
    assert "approachDeck.position.y = OFFICE_LOBBY_SURFACE_Y - 0.09" in scene
    assert 'officeBridgeDeck.name = "forkmesh-office-bridge-deck"' in scene
    assert (
        'approachDeck.name = "forkmesh-office-island-approach-deck"' in scene
    )
    floor_surface = function_body(scene, "addOfficeFloorSurface")
    assert "OFFICE_LOBBY_SURFACE_Y / 2" in floor_surface


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
    town_collision = function_body(scene, "constrainTownOfficeWalls")

    # Admission must not stage a second teleport before the lobby handoff.
    for hard_snap in (
        "player.position.set",
        "camera.position.copy",
        "camera.lookAt",
    ):
        assert hard_snap not in prepare

    # The threshold helper is now a pure coordinate conversion. The mode
    # handoff may normalize floor height, but must preserve the doorway's exact
    # physical X/Z instead of staging a second inward teleport.
    assert "worldToLocal" in local_position
    assert "target.x =" not in local_position
    assert "target.z =" not in local_position
    assert "officeAvatarLocalPosition(" in enter
    threshold_handoff = enter[
        enter.index("if (enteringFromTown)"):
        enter.index("} else if (meetingAvatar)")
    ]
    assert "localPosition.y = currentFloorY" in threshold_handoff
    assert "localPosition.x =" not in threshold_handoff
    assert "localPosition.z =" not in threshold_handoff
    assert "OFFICE_INTERIOR_WALL_LIMIT - 0.72" not in enter
    assert "if (source !== \"doorway\") cancelDash();" in prepare
    assert "if (!enteringFromTown) keyboardMovementSpeed = baseMoveSpeed();" \
        in enter
    assert (
        "player.position.z = office.position.z + doorwayThreshold"
        not in town_collision
    )
    assert "const OFFICE_DOORWAY_PASSAGE_MIN_Z =" in scene
    assert "const OFFICE_DOORWAY_INTERIOR_JOIN_Z =" in scene
    assert "const inDoorwayPassage =" in town_collision
    assert "const readyToCommitEntry =" in town_collision
    assert "localZ <= OFFICE_DOORWAY_ENTRY_Z" in town_collision
    accepted_handoff = town_collision[
        town_collision.index('if (officeSceneMode !== "town")'):
        town_collision.index("const previousX")
    ]
    assert "player.position.z =" not in accepted_handoff
    assert "cancelDash()" not in accepted_handoff

    collision = function_body(scene, "constrainOfficeInteriorWalls")
    assert "const movingOutward =" in collision
    assert "movingOutward &&" in collision
    assert "z <= OFFICE_DOORWAY_APPROACH_Z + 0.08" in collision

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


def test_office_doorway_is_an_open_tunnel_with_full_body_entry_and_exit_planes():
    scene = source(SCENE_PATH)
    town_collision = function_body(scene, "constrainTownOfficeWalls")
    interior_collision = function_body(scene, "constrainOfficeInteriorWalls")
    leave = function_body(scene, "leaveOfficeInterior")

    assert "OFFICE_FRONT_Z - OFFICE_AVATAR_RADIUS - 0.06" in scene
    assert "OFFICE_FRONT_Z + OFFICE_AVATAR_RADIUS + 0.06" in scene
    assert "OFFICE_FRONT_Z - OFFICE_AVATAR_RADIUS - 0.7" in scene
    assert "z >= OFFICE_DOORWAY_INTERIOR_JOIN_Z" in interior_collision
    assert "if (inDoorwayPassage)" in town_collision
    assert "return false;" in town_collision[
        town_collision.index("if (inDoorwayPassage)"):
        town_collision.index("const candidates")
    ]

    exit_handoff = interior_collision[
        interior_collision.index(
            'if (officeSceneMode !== "lobby")',
            interior_collision.index("position.z >= OFFICE_INTERIOR_EXIT_Z"),
        ):
        interior_collision.index("// A missing/rejected controller")
    ]
    assert "position.z =" not in exit_handoff
    assert "commitPosition()" not in exit_handoff

    assert "const crossedLobbyDoorway =" in leave
    preserve = leave[
        leave.index("if (!crossedLobbyDoorway)"):
        leave.index("applyOfficeAvatarLocalPosition")
    ]
    assert "OFFICE_FRONT_Z + 0.82" in preserve


def test_office_door_shows_background_access_hydration_without_gating_entry():
    scene = source(SCENE_PATH)
    office = source(OFFICE_PATH)
    assert 'doorStatus.name = "forkmesh-office-door-status"' in scene
    assert '"SYNCING ACCESS"' in scene
    assert '"WALK RIGHT IN"' not in scene
    assert "doorStatus.visible = false" in scene
    assert 'officeDoorStatus.visible = normalized !== "open"' in scene
    assert "function setOfficeDoorStatus(" in scene
    assert 'world.setOfficeDoorStatus?.("syncing")' in office
    assert (
        'world.setOfficeDoorStatus?.(authorized ? "ready" : "open")'
        in office
    )
    entry = office[
        office.index("async function enterOffice(entry = {})"):
        office.index("function completeOfficeExit()")
    ]
    assert entry.index("completeOfficeEntry({") < entry.index(
        "void refreshOfficeAuthorization("
    )


def test_lobby_camera_clamp_has_one_strict_open_door_portal():
    scene = source(SCENE_PATH)
    limit = function_body(scene, "officeCameraDistanceLimit")

    # Only an outward ray through the open lobby aperture may omit the front-Z
    # face. Its real width, flush floor, and lintel are checked at the front plane.
    for contract in (
        "const rawFrontDistance =",
        "(OFFICE_FRONT_Z - localTarget.z) / frontDirection",
        'officeCurrentFloorId === "lobby"',
        "officeSlidingDoorOpen >= 0.9",
        "Math.abs(portalX) <= OFFICE_DOOR_WIDTH / 2 - 0.08",
        "portalY >= 0.08",
        "portalY <= OFFICE_DOOR_HEIGHT - 0.08",
        'axis === "z"',
        "component > 0",
        "rayThroughOpenLobbyPortal",
    ):
        assert contract in limit

    # All other envelope faces remain in the ray/AABB calculation.
    for axis in ('["x", "minX", "maxX"]', '["y", "minY", "maxY"]',
                 '["z", "minZ", "maxZ"]'):
        assert axis in limit
    assert "ridingElevator" in limit


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


def test_lobby_has_one_noah_attendance_and_the_reflective_logo_fountain():
    scene = source(SCENE_PATH)
    for contract in (
        'officeReception.name = "forkmesh-office-reception"',
        'id: "office-greeter-noah"',
        'receptionDesk.name = "forkmesh-office-reception-desk"',
        "receptionDesk.position.set(0, 1.05, -36.5)",
        "officeReceptionNoah.position.set(0, 0.38, -40)",
        "officeReceptionNoah.rotation.y = Math.PI",
        'noahNameplate.name = "forkmesh-office-reception-nameplate-noah"',
        "Walk up to Noah for World and repository tips",
        "officeGreetingBoard.position.set(0, 6.25, -OFFICE_FRONT_Z + 0.5)",
        'officeAttendanceBoard.name = "forkmesh-office-attendance"',
        "OFFICE · LAST 20 VISITS",
        "visits.slice(0, 20)",
        "IN BUILDING",
        "function setOfficeAttendance(event = {})",
        'logoFountain.name = "forkmesh-office-logo-fountain"',
        'chromeCube.name = "forkmesh-reflective-fm-cube"',
        'chromeMark.name = "forkmesh-reflective-fm-cube-fixed-tilt"',
        "const logoLowerCorner = new THREE.Vector3(-1, -1, -1).normalize()",
        "chromeMark.quaternion.setFromUnitVectors(",
        "logoHalfSize * Math.sqrt(3)",
        "addFLogoFace(2.68, 0)",
        "addFLogoFace(-2.68, Math.PI)",
        "addMLogoFace(2.68, Math.PI / 2)",
        "addMLogoFace(-2.68, -Math.PI / 2)",
        "forkmesh-reflective-fm-panel-",
        "logoPanelShape.holes.push(hole)",
        "panel.userData.logoThroughCutouts = logoPanelShape.holes.length",
        'logoContactPoint.name = "forkmesh-reflective-fm-cube-contact-point"',
        'logoSupport.name = "forkmesh-reflective-fm-cube-support"',
        "new THREE.WebGLCubeRenderTarget(",
        "new THREE.CubeCamera(",
        "reflectionTarget.texture.mapping = THREE.CubeReflectionMapping",
        "metalness: 0.94",
        "chromeCube.rotation.y = time * 0.00022",
        "reflectionCamera.update(renderer, scene)",
    ):
        assert contract in scene
    assert "office-greeter-maya" not in scene.lower()
    assert "maya and noah" not in scene.lower()
    logo = scene[
        scene.index('chromeCube.name = "forkmesh-reflective-fm-cube"'):
        scene.index("let officeLobbyPlayerMoving")
    ]
    assert "const cubeBody" not in logo
    assert "new THREE.BoxGeometry(5.6, 5.6, 5.6" not in logo
    panels = logo[
        logo.index("// Rounded mirrored top and bottom plates"):
        logo.index("const logoContactPoint")
    ]
    assert panels.count("logoPanelShape.holes.push(hole)") == 2
    assert "new THREE.CylinderGeometry(0.48" not in panels
    assert "new THREE.BoxGeometry(0.3, 0.08" not in panels
    chrome_material = scene[
        scene.index("const chrome = new THREE.MeshPhysicalMaterial"):
        scene.index("const darkChrome")
    ]
    assert 'color: "#dbe4ef"' in chrome_material
    assert "metalness: 0.94" in chrome_material
    assert "envMap: reflectionTarget.texture" in chrome_material
    assert "const logoLetterStroke = 1.04" in logo
    assert "const logoPanelChrome = chrome.clone()" in scene
    assert "logoPanelChrome.metalness = 0.8" in scene
    assert "logoInnerFace," in logo
    assert "logoFountain.add(logoSupport)" in logo
    assert "chromeMark.quaternion.identity()" not in logo
    animation = scene[
        scene.index("const logoReflectionSettleMs"):
        scene.index("// One physical selector rides inside")
    ]
    assert "chromeMark.rotation" not in animation
    assert "chromeMark.quaternion" not in animation
    assert "logoReflectionIntervalMs" not in animation
    for contract in (
        'reflectionCamera.userData.logoCapturePolicy = "dirty-idle-once"',
        "logoReflectionDirty && time >= logoReflectionEligibleAt",
        "logoReflectionEligibleAt = time + logoReflectionSettleMs",
        "player.visible = true",
        "player.updateWorldMatrix(true, true)",
        "reflectionCamera.updateWorldMatrix(true, true)",
        "player.visible = playerWasVisible",
        "reflectionCamera.userData.logoCaptureCount += 1",
        "reflectionCamera.userData.logoCapturedPlayerId",
    ):
        assert contract in scene


def test_logo_reflection_excludes_private_work_badge_but_keeps_avatar():
    capture = function_body(
        source(SCENE_PATH),
        "updateOfficeLogoReflection",
    )

    for contract in (
        "const selfWorkBadge = player.userData?.selfWorkBadge",
        "const selfWorkBadgeWasVisible = selfWorkBadge?.visible",
        "player.visible = true",
        "if (selfWorkBadge) selfWorkBadge.visible = false",
        "reflectionCamera.update(renderer, scene)",
        "selfWorkBadge.visible = selfWorkBadgeWasVisible",
        "player.visible = playerWasVisible",
    ):
        assert contract in capture

    hidden_at = capture.index(
        "if (selfWorkBadge) selfWorkBadge.visible = false",
    )
    reflected_at = capture.index("reflectionCamera.update(renderer, scene)")
    restored_at = capture.index(
        "selfWorkBadge.visible = selfWorkBadgeWasVisible",
    )
    assert hidden_at < reflected_at < restored_at


def test_noah_reuses_local_chat_bubbles_with_hysteresis_and_no_frame_spam():
    scene = source(SCENE_PATH)
    guide = function_body(scene, "updateOfficeReceptionGuide")
    bubble = function_body(scene, "showAvatarChatBubble")
    animate = function_body(scene, "animate")

    for contract in (
        'officeSceneMode === "lobby"',
        'officeCurrentFloorId === "lobby"',
        "officeReceptionDeskDistance(",
        "OFFICE_RECEPTION_RESET_RANGE",
        "OFFICE_RECEPTION_TALK_RANGE",
        "officeReceptionWasNear",
        "OFFICE_RECEPTION_TALK_COOLDOWN_MS",
        "officeFloorAccess.authenticated === true",
        "OFFICE_RECEPTION_GUEST_TIPS",
        "OFFICE_RECEPTION_MEMBER_TIPS",
        "showAvatarChatBubble(",
        "{ officeReception: true }",
    ):
        assert contract in guide
    assert "updateOfficeReceptionGuide(time)" in animate
    assert "sprite.userData.officeReceptionGreeting" in bubble
    assert "world.add(sprite)" in bubble
    # This stays entirely scene-local; approaching a desk never emits presence,
    # analytics, attendance, or room messages.
    for forbidden in (
        "fetch(",
        "onMovement(",
        "onOfficeMovement(",
        "onWorldEvent",
        "onOfficeEnter(",
    ):
        assert forbidden not in guide
    guest_tips = scene[
        scene.index("const OFFICE_RECEPTION_GUEST_TIPS"):
        scene.index("const OFFICE_RECEPTION_MEMBER_TIPS")
    ].lower()
    assert "log in" in guest_tips
    assert "restricted team floors" in guest_tips
    assert "lobby is open to everyone" in guest_tips

    # Noah is nested under officeInterior. Shared bubble animation must resolve
    # the speaker's world transform rather than copying that local position.
    emote_loop = scene[
        scene.index("for (let index = emoteSprites.length - 1"):
        scene.index("for (let index = rewardFlights.length - 1")
    ]
    assert "flight.avatar.getWorldPosition(flight.sprite.position)" in emote_loop
    assert "flight.sprite.position.copy(flight.avatar.position)" not in emote_loop


def test_rooftop_furniture_seating_and_real_source_laptop_are_complete():
    scene = source(SCENE_PATH)
    world = source(WORLD_PATH)
    rooftop = scene[
        scene.index('const rooftop = officeFloorGroups.get("rooftop")'):
        scene.index("addOfficeFunFloorProps();")
    ]
    for contract in (
        "forkmesh-office-rooftop-table-",
        "forkmesh-office-rooftop-tabletop-",
        "forkmesh-office-rooftop-table-${tableIndex + 1}-leg-",
        "rooftop-chair-",
        "forkmesh-office-${chairId}-seat",
        "forkmesh-office-${chairId}-back",
        "forkmesh-office-${chairId}-leg-",
        'chair.userData.officeFloorId = "rooftop"',
        "chair.userData.officeSeatTopY = seatTopY",
        'child.userData.interactive = "office-chair"',
        'rooftopLaptop.name = "forkmesh-office-rooftop-laptop"',
        'child.userData.interactive = "office-rooftop-laptop"',
        "CLICK TO OPEN REAL REPOSITORY",
    ):
        assert contract in rooftop
    assert "const telescopeTube" not in rooftop
    assert "new THREE.BoxGeometry(2.2, 0.34, 2.2)" not in rooftop

    sit = function_body(scene, "sitOnOfficeChair")
    pose = function_body(scene, "applyOfficeChairSeatPose")
    assert "chair.userData.officeFloorId" in sit
    assert "officeCurrentFloorId" in sit
    assert "officeFloorY(floorId) + seatTopY" in pose

    callback = world[
        world.index("onOfficeRooftopLaptopSelect:"):
        world.index("onWorldBulletinSelect:", world.index(
            "onOfficeRooftopLaptopSelect:"
        ))
    ]
    assert (
        '"/forkmesh/forkmesh/blob/cloudflare_worker/public/world/'
        'world-scene.js"' in callback
    )
    assert '"_blank"' in callback
    assert '"noopener,noreferrer"' in callback
    assert "desktop app or IDE extension" in callback
    assert "/api/" not in callback
    assert "POST" not in callback


def test_rooftop_camera_and_pointer_travel_stay_on_the_active_floor():
    scene = source(SCENE_PATH)
    camera = function_body(scene, "updateCamera")
    floor_filter = function_body(scene, "officeObjectMatchesCurrentFloor")
    ground = function_body(scene, "groundPointAt")
    double_click = function_body(scene, "handleDoubleClick")
    lobby_walk = function_body(scene, "walkOfficeLobbyPlayer")
    pointer = function_body(scene, "finishPointer")

    assert 'officeCurrentFloorId === "rooftop"' in camera
    assert 'officeFloorY("rooftop") + 0.55' in camera
    assert camera.count("localCamera.y = Math.max(") >= 1
    assert camera.index("camera.position.lerp(") < camera.rindex(
        "localCamera.y = Math.max("
    )
    assert "localDesired.y = Math.max(" in camera
    # Only Y is redirected above the slab, preserving the full outward X/Z
    # zoom that officeCameraDistanceLimit grants on the patio.
    rooftop_guard = camera[
        camera.index("if (rooftopPatioCamera)"):
        camera.index("if (reducedMotion)")
    ]
    assert "localDesired.x" not in rooftop_guard
    assert "localDesired.z" not in rooftop_guard

    assert "floorId === officeCurrentFloorId" in floor_filter
    assert 'object.userData?.interactive === "office-elevator-floor"' in (
        floor_filter
    )
    assert "officeObjectMatchesCurrentFloor(object)" in pointer
    assert "officeObjectMatchesCurrentFloor(object)" in double_click
    assert "officeInteriorPointIsWalkable(" in ground
    assert "officeCurrentFloorId" in ground
    assert "dashTarget" in lobby_walk
    assert "constrainOfficeInteriorWalls(" in lobby_walk


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
        scene.index("let logoReflectionDirty"):
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
        "officeLobbyPlayerMoving ||",
        "primaryPointerId !== null",
        "pinchActive",
        "logoReflectionDirty = true",
        "logoReflectionWasBusy = true",
        "logoReflectionDirty = false",
    ):
        assert contract in reflection
    assert "logoReflectionIntervalMs" not in reflection


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
        "const inDoorwayPassage =",
        "const readyToCommitEntry =",
        "!officeDoorwayEntryPending",
        "officeDoorwayEntryPending = true;",
        'source: "doorway"',
        "function setOfficeDoorwayEntryPending(pending = false)",
        "setOfficeDoorwayEntryPending,",
    ):
        assert contract in scene
    assert "officeDoorwayEntryArmed" not in scene
    entry = office[
        office.index("async function enterOffice(entry = {})"):
        office.index("function completeOfficeExit()")
    ]
    assert 'entry?.source === "doorway"' in entry
    assert "const entered = completeOfficeEntry({" in entry
    assert "loadPublicAttendance: !activeSession" in entry
    assert "void refreshOfficeAuthorization(" in entry
    assert "requestOfficeEntry" not in entry
    assert "world.setOfficeDoorwayEntryPending?.(false)" in entry


def test_scene_returns_the_new_office_control_surface():
    scene = source(SCENE_PATH)
    returned = scene[scene.rindex("return {"):]
    for contract in (
        "setOfficeExitHandler",
        "setOfficeDoorStatus",
        "setOfficeDoorwayEntryPending",
        "setOfficeFloorHandler",
        "setOfficeAccess",
        "setOfficeAttendance",
        "travelToOfficeFloor",
        "enterOfficeLobby",
        "leaveOfficeInterior",
    ):
        assert contract in returned
def test_marketing_furniture_and_exterior_landscaping_keep_paths_clear():
    scene = source(SCENE_PATH)
    for contract in (
        'officeTable.position.set(0, officeFloorY("marketing") + 2.1, 0)',
        'officeTableEpoxy.position.set(0, officeFloorY("marketing") + 2.395, 0)',
        "[-7.4, 0]",
        "[0, -7.4]",
        'desk.name = `forkmesh-office-marketing-desk:${member}`',
        "37.2 - row * 6.8",
        'nameplate.name = `forkmesh-office-marketing-desk-plaque:${member}`',
        "nameplate.rotation.y = Math.PI",
        "desk.position.z - 3.2",
        'deskChair.name = `forkmesh-office-marketing-desk-chair:${member}`',
        'officeTableLogo.name = "forkmesh-office-marketing-cube-inlay"',
        '"/assets/world/marketing-table-cube-inlay.png"',
        'officeLandscaping.name = "forkmesh-office-landscaping"',
        'bush.name = `forkmesh-office-landscape-bush-${index + 1}`',
        'flower.name = `forkmesh-office-landscape-flower-${index + 1}`',
    ):
        assert contract in scene
    landscaping = scene[
        scene.index('officeLandscaping.name = "forkmesh-office-landscaping"'):
        scene.index("world.add(officeLandscaping)") + 40
    ]
    assert "userData.interactive" not in landscaping
    assert "userData.ground" not in landscaping
