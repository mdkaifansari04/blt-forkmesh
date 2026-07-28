from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"


def source():
    return SCENE_PATH.read_text()


def test_world_uses_a_mixed_city_and_woodland_surface():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-land"',
        'group.name = "forkmesh-town-mixed-landscape"',
        '"forkmesh-town-stone-plaza"',
        "`forkmesh-town-path-${id}`",
        "deterministicTreeLayout().forEach",
        '"#b29a76"',
        '"/world/assets/city-park-grass-v1.webp"',
    ):
        assert contract in scene
    assert "forkmesh-town-grass-patch" not in scene
    assert 'makeMaterial(THREE, "#174434"' not in scene


def test_world_land_uses_one_clean_low_draw_call_foundation():
    scene = source()
    for contract in (
        '"forkmesh-continuous-city-foundation"',
        '"forkmesh-continuous-city-land"',
        "new THREE.CylinderGeometry(1, 1, 6.4, 128)",
        "CONTINUOUS_CITY_RADIUS_X",
        "CONTINUOUS_CITY_RADIUS_Z",
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
        '"forkmesh-member-promenade"',
        '"forkmesh-leaderboard-promenade"',
        '"forkmesh-office-land-promenade"',
        "concreteBrickMaterial(THREE)",
    ):
        assert contract in scene
    assert "addRoundedCausewayEnds" not in scene
    assert "rounded-pavement" not in scene
    assert "causeway-earth" not in scene
    assert "forkmesh-curved-district-land" not in scene
    assert "for (let x = -5.4; x <= 5.4; x += 1.2)" not in scene


def test_continuous_visible_foundation_drives_walkability():
    scene = source()
    for contract in (
        "const cityRadiusX = CONTINUOUS_CITY_RADIUS_X - margin;",
        "const cityRadiusZ = CONTINUOUS_CITY_RADIUS_Z - margin;",
        "((px - WORLD_BIKE_LANE_CENTER_X) / cityRadiusX) ** 2",
        "((pz - CONTINUOUS_CITY_CENTER_Z) / cityRadiusZ) ** 2",
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


def test_leaderboards_are_one_above_ground_perimeter_panel():
    scene = source()
    for contract in (
        '"forkmesh-leaderboard-super-panel"',
        "new THREE.BoxGeometry(58, 4.2, 0.35)",
        "leaderboardSuperBacking.position.set(0, 4.25, 0)",
        "leaderboardSuperPanel.add(face)",
        "const relayoutBillboardCircle = () =>",
        "billboardIslandObjects.push({ object, layoutId });",
    ):
        assert contract in scene
    assert "object.rotation.y = Math.atan2(-object.position.x" in scene


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
    ):
        assert contract in scene


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
