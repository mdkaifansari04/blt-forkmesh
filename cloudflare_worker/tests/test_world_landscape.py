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
        '"/world/assets/city-park-grass-v1.webp"',
    ):
        assert contract in scene
    assert '"forkmesh-town-stone-plaza"' not in scene
    assert '"forkmesh-town-plaza-edge"' not in scene
    assert "forkmesh-town-path-edge-" not in scene
    assert "forkmesh-town-grass-patch" not in scene
    assert 'makeMaterial(THREE, "#174434"' not in scene


def test_world_land_uses_one_clean_low_draw_call_foundation():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-foundation"',
        '"forkmesh-continuous-city-land"',
        "new THREE.CylinderGeometry(1, 1, 6.4, 128)",
        "const CONTINUOUS_CITY_RADIUS = 365;",
        "CONTINUOUS_CITY_RADIUS,\n    1,\n    CONTINUOUS_CITY_RADIUS,",
    ):
        assert contract in scene
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


def test_leaderboards_have_one_square_raised_grid_without_circle_boards():
    scene = source()
    for contract in (
        '"forkmesh-leaderboard-super-panel"',
        "new THREE.BoxGeometry(23, 23, 0.45)",
        "leaderboardSuperBacking.position.set(0, 12, 0)",
        "leaderboardSuperPanel.add(leaderboardGridFace)",
        "function leaderboardGridTexture",
        "for (let index = 0; index < 25; index += 1)",
        "function leaderboardRowIsSpecial",
        "leaderboardSuperPanel.position.set(-42, 0.22, 0)",
    ):
        assert contract in scene
    physical = scene.split(
        "// One square 5×5 wall preserves", 1
    )[1].split("let leaderboardGridKey", 1)[0]
    assert "makeActiveLeaderboardSign" not in physical
    assert "makeReferralLeaderboardSign" not in physical
    assert "makeSiteReferrerLeaderboardSign" not in physical
    assert '"forkmesh-leaderboard-ring-walk"' not in scene


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
    ):
        assert contract in scene


def test_clickable_quadcopter_flies_on_three_axes_to_a_bounded_high_altitude():
    scene = source()
    for contract in (
        'quadcopter.name = "forkmesh-world-quadcopter"',
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
    ):
        assert contract in scene


def test_treasury_sign_is_centered_with_a_new_window_node_download():
    scene = source()
    for contract in (
        "treasurySign.position.set(0, 0, 0)",
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


def test_land_is_continuous_and_uses_preloaded_local_image_textures():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-foundation"',
        '"forkmesh-continuous-city-land"',
        '"/world/assets/city-park-grass-v1.webp"',
        '"/world/assets/concrete-brick-path-v1.webp"',
        "function projectAssetTexture(",
        "function worldWalkSurfaceContains(",
    ):
        assert contract in scene


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
