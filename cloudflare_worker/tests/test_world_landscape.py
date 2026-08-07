import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"


def source():
    return SCENE_PATH.read_text()


def test_world_uses_a_mixed_city_and_woodland_surface():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-land"',
        'group.name = "forkmesh-town-mixed-landscape"',
        '"forkmesh-town-path-junction"',
        "`forkmesh-town-path-${id}`",
        "deterministicTreeLayout().forEach",
        "function cityGrassTexture(THREE)",
        "new THREE.DataTexture(",
    ):
        assert contract in scene
    assert "city-park-grass-v1.webp" not in scene
    assert '"forkmesh-town-stone-plaza"' not in scene
    assert '"forkmesh-town-plaza-edge"' not in scene
    assert "forkmesh-town-path-edge-" not in scene
    assert "forkmesh-town-grass-patch" not in scene
    assert 'makeMaterial(THREE, "#174434"' not in scene


def test_world_land_uses_one_flat_textured_surface():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-land"',
        "const CONTINUOUS_CITY_RADIUS = 365;",
        "new THREE.CircleGeometry(1, 160)",
        "cityGrassMaterial(THREE)",
        "continuousCityLand.userData.ground = true;",
    ):
        assert contract in scene
    assert '"forkmesh-continuous-city-foundation"' not in scene
    assert "new THREE.CylinderGeometry(1, 1, 6.4, 128)" not in scene
    assert "function createTerrainFoundation(" not in scene
    assert "hosted-repository-terrain-foundation" not in scene
    assert "forkmesh-office-terrain-foundation" not in scene
    assert 'ring.name = `${name}-strata-${index + 1}`' not in scene
    assert 'root.name = `${name}-root-${index + 1}`' not in scene


def test_repository_and_office_paths_share_the_continuous_grass():
    scene = source()
    for contract in (
        '"hosted-repository-promenade"',
        '"south-members"',
        "[0, MEMBER_PATH_END_Z]",
        '"forkmesh-leaderboard-promenade"',
        '"north-office"',
        "[0, OFFICE_BRIDGE_START_Z]",
        '"forkmesh-office-bridge"',
        '"forkmesh-office-island-approach"',
        "concreteBrickMaterial(THREE)",
    ):
        assert contract in scene
    assert "addRoundedCausewayEnds" not in scene
    assert "rounded-pavement" not in scene
    assert "causeway-earth" not in scene
    assert "forkmesh-curved-district-land" not in scene
    assert '"forkmesh-member-promenade"' not in scene
    assert '"forkmesh-office-land-promenade"' not in scene
    assert "for (let x = -5.4; x <= 5.4; x += 1.2)" not in scene


def test_repository_and_leaderboard_districts_share_textured_flush_paths():
    scene = source()
    for contract in (
        "const WORLD_PATH_WIDTH = OFFICE_BRIDGE_WIDTH;",
        "const WORLD_PATH_HEIGHT = 0.08;",
        "const WORLD_PATH_SURFACE_Y = 0.105;",
        (
            "const REPOSITORY_GROUND_RADIUS = "
            "REPOSITORY_ISLAND_RING_RADIUS + 7;"
        ),
        "function createDistrictGroundCircle(",
        "radius = DISTRICT_GROUND_RADIUS,",
        "`forkmesh-${kind}-textured-ground`",
        '"repositories",\n      REPOSITORY_GROUND_RADIUS,',
        'createDistrictGroundCircle(\n      THREE,\n      "leaderboards",\n      LEADERBOARD_DISTRICT_GROUND_RADIUS,\n    )',
        "repositoryConnectionLength,\n      WORLD_PATH_HEIGHT,\n      WORLD_PATH_WIDTH,",
        "leaderboardConnectionLength,\n      WORLD_PATH_HEIGHT,\n      WORLD_PATH_WIDTH,",
        "repositoryPromenade.position.y = WORLD_PATH_CENTER_Y;",
        "leaderboardPromenade.position.y = WORLD_PATH_CENTER_Y;",
        'importKiosk.position.set(0, 0, 0);',
        'color: repository ? "#c1bbb0" : "#8a829b"',
    ):
        assert contract in scene
    assert "new THREE.BoxGeometry(repositoryConnectionLength, 0.08, 7.2)" not in scene
    assert "new THREE.BoxGeometry(leaderboardConnectionLength, 0.08, 7.2)" not in scene
    assert "guide.position.set(0, 0.15, z)" not in scene
    assert "beacon.position.set(x, 0.72, z)" not in scene


def test_continuous_visible_foundation_drives_walkability():
    scene = source()
    for contract in (
        "const cityRadius = CONTINUOUS_CITY_RADIUS - margin;",
        "Math.hypot(",
        "px - WORLD_BIKE_LANE_CENTER_X",
        "pz - CONTINUOUS_CITY_CENTER_Z",
        ") <= cityRadius",
        "BEACH_ROAD_MIN_X + margin",
        "BEACH_RADIUS - margin",
    ):
        assert contract in scene
    assert "WORLD_LOOP_CONTROL_POINTS" not in scene


def test_world_edge_walk_uses_only_two_zero_triangle_line_rails():
    scene = source()
    for contract in (
        "const VOID_WALK_CENTER_Z = 270;",
        "const VOID_WALK_END_X = WORLD_RADIUS + 120;",
        "function voidWalkSurfaceContains(",
        "function createVoidWalkRails(THREE)",
        'rails.name = "forkmesh-void-walk-rails"',
        "new THREE.LineBasicMaterial({",
        "new THREE.Line(geometry, railMaterial)",
        "forkmesh-void-walk-${index === 0 ? \"south\" : \"north\"}-rail",
        '"void-walk-rails", "World edge walk · line rails", "Terrain",',
        "voidWalkSurfaceContains(x, z, radius)",
    ):
        assert contract in scene

    void_walk = scene.split("function createVoidWalkRails(THREE)", 1)[1].split(
        "const projectAssetTextures", 1
    )[0]
    assert "new THREE.Mesh(" not in void_walk
    assert "new THREE.BoxGeometry(" not in void_walk
    assert void_walk.count("new THREE.Line(geometry, railMaterial)") == 1


def test_paths_share_a_concrete_brick_texture_and_closed_bike_lane():
    scene = source()
    for contract in (
        "function concreteBrickTexture(THREE)",
        "function concreteBrickMaterial(THREE)",
        '"/world/assets/concrete-brick-path-v1.webp"',
        "    8,\n    2,",
        "const WORLD_BIKE_LANE_RADIUS = 242;",
        "new THREE.RingGeometry(",
        '"forkmesh-world-bike-lane"',
        '"forkmesh-world-bike-center-groove"',
        "bikeLaneMaterial(THREE)",
    ):
        assert contract in scene
    assert "WORLD_LOOP_CONTROL_POINTS" not in scene


def test_every_leaderboard_is_its_own_card_on_the_leaderboard_circle():
    scene = source()
    physical = scene.split(
        "// Every public ranking is a card of its own again", 1
    )[1].split("function repaintActiveLeaderboardCard", 1)[0]
    for contract in (
        'addLeaderboardCard("active-members", makeActiveLeaderboardSign(THREE))',
        'addLeaderboardCard("referrals", makeReferralLeaderboardSign(THREE))',
        'addLeaderboardCard("http-referrers", makeSiteReferrerLeaderboardSign(THREE))',
        "for (const board of WORLD_LEADERBOARD_BOARD_STUBS) {",
        "addLeaderboardCard(board.id, makeLeaderboardStatSign(THREE, board));",
        'circle: "leaderboards",',
        'feed: "leaderboards",',
    ):
        assert contract in physical
    # The one 5x5 wall that replaced the cards is gone, painter included.
    assert '"forkmesh-leaderboard-super-panel"' not in scene
    assert "leaderboardSuperPanel" not in scene
    assert "function leaderboardGridTexture" not in scene
    assert "function leaderboardGridBoardDescriptors" not in scene
    assert "for (let index = 0; index < 25; index += 1)" not in scene
    assert '"forkmesh-leaderboard-ring-walk"' not in scene


def test_leaderboard_circle_is_open_without_a_cover_or_door_gate():
    scene = source()
    district = scene.split(
        'leaderboardDistrict.name = "forkmesh-leaderboard-district"', 1
    )[1].split("const leaderboardBeacon", 1)[0]
    assert 'leaderboardInterior.name = "forkmesh-leaderboard-interior"' in district
    assert "leaderboardDistrict.add(leaderboardInterior);" in district
    assert "leaderboardInterior.visible = false" not in district
    assert "createOpaqueDistrictCover" not in scene
    assert "createLeaderboardOpaqueCover" not in scene
    assert "forkmesh-leaderboard-opaque-cover" not in scene
    assert "leaderboard-cover" not in scene


def test_open_node_yard_uses_instanced_boxes_until_the_camera_is_close():
    scene = source()
    district = scene.split(
        'nodeDistrict.name = "forkmesh-node-district"', 1
    )[1].split("const systemCapacityPlatform", 1)[0]
    lod = scene.split("function updateNodeDetailLevel", 1)[1].split(
        "function enclosureSceneRoots", 1
    )[0]
    boxes = scene.split("function createMirrorNodeBoxLod", 1)[1].split(
        "function createAgentRobot", 1
    )[0]
    assert '"forkmesh-node-opaque-cover"' not in scene
    assert "function constrainNodeCover" not in scene
    assert "const NODE_DETAIL_ENTER_DISTANCE = 58;" in scene
    assert "const NODE_DETAIL_EXIT_DISTANCE = 70;" in scene
    assert "new THREE.InstancedMesh(" in boxes
    assert "NODE_LOD_MAX_INSTANCES" in boxes
    assert 'boxes.name = "forkmesh-node-box-lod"' in boxes
    assert "new THREE.BoxGeometry(2.24, 3.28, 1.42)" in boxes
    assert 'nodeInterior.name = "forkmesh-node-interior"' in district
    assert "nodeInterior.visible = false" in district
    assert "nodeInterior.add(fountainLandmark)" in district
    assert "nodeDistrict.add(nodeInterior, nodeBoxLod)" in district
    assert "nodeInterior.add(cabinet)" in scene
    assert "nodeDistrict.getWorldPosition(nodeDetailWorldPosition)" in lod
    assert "camera.position.distanceTo(nodeDetailWorldPosition)" in lod
    assert 'world.userData.nodeDetailSource = "camera"' in lod
    assert "player.position" not in lod
    assert "distance <= boundary" in lod
    assert 'detailed ? "detailed" : "boxes"' in lod
    assert "nodeInterior.visible = detailed" in lod
    assert "nodeInterior.traverse((child) =>" in lod
    assert 'Object.hasOwn(child.userData, "nodeLodVisible")' in lod
    assert "child.visible = false" in lod
    assert "nodeBoxLod.visible = !detailed" in lod
    assert "nodeBoxLod.setMatrixAt(nodeIndex" in scene
    assert "nodeBoxLod.setColorAt(" in scene
    assert "nodeBoxLod.instanceMatrix.needsUpdate = true" in scene
    assert "updateNodeDetailLevel(true);" in scene
    assert 'if (worldElementEnabled("node-cabinets")) {' in scene
    assert "if (nodeDetailVisible) {" in scene
    assert "if (!group.parent?.visible) return;" in scene


def test_leaderboard_contents_are_always_mounted_in_the_open_district():
    scene = source()
    district = scene.split(
        'leaderboardDistrict.name = "forkmesh-leaderboard-district"', 1
    )[1].split("const leaderboardBeacon", 1)[0]
    assert (
        'leaderboardInterior.name = "forkmesh-leaderboard-interior"'
        in district
    )
    assert "leaderboardInterior.visible = false" not in district
    assert "leaderboardInterior.add(\n    createDistrictGroundCircle" in district
    # Boards mount on their own circle, and every circle mounts on the open
    # district interior, so nothing is hidden behind a cover or a gate.
    assert "leaderboardInterior.add(group);" in scene
    assert "circle.group.add(object);" in scene
    assert "updateLeaderboardCircleOccupancy" not in scene
    assert "leaderboardCoverContainsWorldPoint" not in scene


def test_only_office_and_beach_switch_to_isolated_scenes():
    scene = source()
    isolation = scene.split("function syncEnclosureSceneVisibility", 1)[1].split(
        "function compactDistrictDiagnostics", 1
    )[0]
    for mode in ("office", "beach"):
        assert f'? "{mode}"' in isolation or f': "{mode}"' in isolation
    for open_district in ("leaderboards", "repositories", "members", "nodes"):
        assert f'"{open_district}"' not in isolation
    assert "enclosureHiddenWorldRoots.set(root, root.visible)" in isolation
    assert "root.visible = false" in isolation
    assert "enclosureHiddenWorldRoots.forEach" in isolation
    assert "world.userData.activeEnclosureScene = next" in isolation
    assert "syncEnclosureSceneVisibility();" in scene


def test_leaderboard_has_no_wall_or_door_collision_constraint():
    scene = source()
    assert "leaderboardDoorCrossingIsClear" not in scene
    assert "constrainLeaderboardCover" not in scene
    assert "LEADERBOARD_COVER_APOTHEM" not in scene
    assert "LEADERBOARD_DOOR_WIDTH" not in scene


def test_two_clickable_bikes_use_normal_movement_and_collision():
    scene = source()
    for contract in (
        'addWorldBike(0, "#77d9ff", 0.38)',
        'addWorldBike(1, "#9ef7c6", 0.43)',
        "function rideBike(index)",
        "function applyBikeRidePose(moving, delta)",
        "function dismountBike(",
        "function toggleNearestBikeRide()",
        'event.code === "KeyE"',
        "placeBikeOnLane(",
        "BIKE_RIDE_SPEED_MULTIPLIER",
        "input.ridingBike ? BIKE_RIDE_SPEED_MULTIPLIER : 1",
        "worldWalkSurfaceContains(",
        "hit.object.userData.bikeIndex",
        "circularRideCameraYaw(",
        "const previousBikeHeading = state.bike.rotation.y",
        '"RIDE BIKE"',
        '"CLICK OR E · W/S PEDAL · A/D STEER"',
        "state.hint.visible = false",
    ):
        assert contract in scene


def test_two_cloned_lod_quadcopters_fly_on_three_axes():
    scene = source()
    for contract in (
        "function createWorldQuadcopterPrototype()",
        "const quadcopterPrototype = createWorldQuadcopterPrototype();",
        "const quadcopter = quadcopterPrototype.clone(true);",
        'quadcopter.name = `forkmesh-world-quadcopter-${index + 1}`',
        'quadcopterLod.addLevel(near, 0, 0.12)',
        'quadcopterLod.addLevel(far, 68, 0.18)',
        "addWorldQuadcopter(0, [-34, 0.24, -42]);",
        "addWorldQuadcopter(1, [34, 0.24, -42]);",
        "const QUADCOPTER_HORIZONTAL_SPEED = 42",
        "const QUADCOPTER_VERTICAL_SPEED = 28",
        "const QUADCOPTER_MAX_ALTITUDE = 480",
        "function rideQuadcopter(index)",
        "function dismountQuadcopter(",
        "function toggleNearestQuadcopterRide()",
        "function updateQuadcopterRide(input, delta, time)",
        '["Space", "KeyC", "ControlLeft", "ControlRight"]',
        "hit.object.userData.quadcopterIndex",
        "prepareRoofParachute()",
        "QUADCOPTER_RIDING_ACTIVITY",
        "getQuadcopterState:",
        '"FLY QUADCOPTER"',
        '"CLICK OR E · WASD · SPACE UP · C DOWN"',
        "const legacyForwardInput =",
        "movement.addScaledVector(forward, legacyForwardInput);",
        "movement.addScaledVector(right, legacyRightInput);",
    ):
        assert contract in scene


def test_avatar_full_detail_reuses_buffers_without_camera_lod():
    scene = source()
    avatar = scene.split("function createAvatar(THREE, identity", 1)[1].split(
        "function applyAvatarLookDirection", 1
    )[0]
    assert 'highDetail.name = "avatar-full-detail"' in avatar
    assert "group.add(highDetail);" in avatar
    assert "new THREE.LOD()" not in avatar
    assert "cloneAvatarFarModel" not in scene
    assert "cloneSharedMesh(" in avatar
    assert "function disposeOwnedMaterial(material, disposeMap = false)" in scene
    assert "disposeOwnedMaterial(child.material, true);" in scene


def test_world_scale_drone_course_has_ten_low_poly_instanced_rings():
    scene = source()
    course = scene.split("function createDroneRaceCourse()", 1)[1].split(
        "const droneRaceCourse = createDroneRaceCourse();", 1
    )[0]
    assert "const DRONE_COURSE_RING_COUNT = 10;" in scene
    assert "const DRONE_COURSE_RADIUS = 500;" in scene
    assert "new THREE.InstancedMesh(" in course
    assert "DRONE_COURSE_RING_COUNT" in course
    assert "new THREE.TorusGeometry(8, 0.55, 4, 12)" in course
    assert "new THREE.TorusGeometry(8, 0.72, 3, 8)" in course
    assert "course.addLevel(detailedRings, 0, 0.12);" in course
    assert "course.addLevel(coarseRings, DRONE_COURSE_COARSE_DISTANCE, 0.16);" in course
    assert "activeCamera.getWorldPosition(cameraCoursePosition);" in course
    assert "cameraCoursePosition.distanceTo(center)" in course
    assert "DRONE_COURSE_DETAIL_ENTER_DISTANCE" in course
    assert 'course.userData.detailLevel = detailedVisible ? "detailed" : "coarse"' in course
    assert 'course.name = "forkmesh-drone-race-course"' in course


def test_car_and_quadcopter_hints_explain_directional_controls_nearby():
    scene = source()
    for contract in (
        '"forkmesh-beach-road-car-control-hint"',
        '"CLICK TO ENTER · WASD DRIVE · SHIFT BOOST"',
        '"forkmesh-world-quadcopter-control-hint"',
        "state.hint.visible = false",
        "if (state?.hint) state.hint.visible = true",
    ):
        assert contract in scene


def test_treasury_sign_is_centered_with_a_new_window_node_download():
    scene = source()
    for contract in (
        "treasurySign.position.set(0, 1, 0)",
        '"reward-treasury-start-node-button"',
        '"start-node-download"',
        '"START A NODE"',
        '"DOWNLOAD APP  ↗"',
        "onStartNodeDownload();",
    ):
        assert contract in scene


def test_bike_first_person_yaw_follows_wrapped_lane_heading_delta():
    script = f"""
      import {{ circularRideCameraYaw }} from {
          json.dumps(SCENE_PATH.as_uri())
      };
      const wrapped = circularRideCameraYaw(0.4, 3.1, -3.1);
      const ordinary = circularRideCameraYaw(-0.2, 0.8, 0.5);
      process.stdout.write(JSON.stringify([wrapped, ordinary]));
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    wrapped, ordinary = json.loads(result.stdout)
    assert abs(wrapped - 0.4831853071795864) < 1e-9
    assert abs(ordinary - (-0.5)) < 1e-9


def test_connected_beach_has_local_horizon_car_and_clickable_seating():
    scene = source()
    for contract in (
        '"forkmesh-beach-road"',
        '"forkmesh-beach-sand"',
        '"forkmesh-beach-water"',
        '"forkmesh-beach-peripheral-horizon"',
        '"/world/assets/beach-horizon-v1.webp"',
        '"forkmesh-beach-road-car"',
        "function rideCar(index)",
        "function applyCarRidePose(moving, delta)",
        "CAR_RIDE_SPEED_MULTIPLIER",
        "hit.object.userData.carIndex",
        "function addWorldBench({",
        "function sitOnWorldSeat(seat)",
        "hit?.object?.userData?.worldSeat",
    ):
        assert contract in scene


def test_land_is_continuous_and_uses_a_cached_procedural_grass_texture():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-land"',
        '"/world/assets/concrete-brick-path-v1.webp"',
        "function projectAssetTexture(",
        "const CITY_GRASS_PATTERN_SIZE = 64;",
        "const cityGrassTextures = new WeakMap();",
        "function cityGrassTexture(THREE)",
        "new THREE.DataTexture(",
        "texture.generateMipmaps = true;",
        "cityGrassTextures.set(THREE, texture);",
        "function worldWalkSurfaceContains(",
    ):
        assert contract in scene
    assert "city-park-grass-v1.webp" not in scene


def test_start_here_map_persists_bounded_progress_between_visits():
    scene = source()
    for contract in (
        "const START_HERE_STEPS = Object.freeze([",
        '"forkmesh-start-here-map"',
        '"forkmesh.world.start-here.v1"',
        "validStartHereStepIds.has(step)",
        "localStorage.setItem(",
        'completeStartHereStep("people")',
        'completeStartHereStep("nodes")',
        'completeStartHereStep("repositories")',
        'completeStartHereStep("office")',
    ):
        assert contract in scene


def test_environment_tracks_the_visitors_local_daylight_without_frame_churn():
    scene = source()
    assert "const observedMinute = now.getHours() * 60 + now.getMinutes();" in scene
    assert 'daylightMode === "day"' in scene
    assert 'daylightMode === "night"' in scene
    assert "const easedDaylight =" in scene
    assert "nextEnvironmentCheckAt = time + 15_000;" in scene
    assert "if (minuteOfDay !== localDaylightMinute)" in scene
    assert 'moon.name = "forkmesh-world-moonlight"' in scene
    assert "moon.castShadow = false" in scene
    assert "scene.add(sun.target)" in scene
    assert "(1 - easedDaylight) * 0.9" in scene
