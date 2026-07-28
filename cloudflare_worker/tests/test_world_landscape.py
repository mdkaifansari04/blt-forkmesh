from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"


def source():
    return SCENE_PATH.read_text()


def test_world_uses_a_mixed_city_and_woodland_surface():
    scene = source()
    for contract in (
        'ground.name = "forkmesh-town-terrain-top"',
        'group.name = "forkmesh-town-mixed-landscape"',
        '"forkmesh-town-stone-plaza"',
        "`forkmesh-town-path-${id}`",
        "`forkmesh-town-grass-patch-${index + 1}`",
        "deterministicTreeLayout().forEach",
        '"#716f66"',
        '"#b29a76"',
        '"#536348"',
    ):
        assert contract in scene
    assert 'makeMaterial(THREE, "#174434"' not in scene


def test_world_land_uses_clean_low_draw_call_foundations():
    scene = source()
    for contract in (
        "function createTerrainFoundation(",
        'earth.name = `${name}-earth`',
        "new THREE.CylinderGeometry(radius, radius * 0.94, depth, 64, 1)",
        "A single clean foundation replaces the old strata/root decoration",
        '"forkmesh-town-terrain-foundation"',
        '"hosted-repository-terrain-foundation"',
        '"forkmesh-office-terrain-foundation"',
    ):
        assert contract in scene
    assert 'ring.name = `${name}-strata-${index + 1}`' not in scene
    assert 'root.name = `${name}-root-${index + 1}`' not in scene


def test_repository_and_office_connections_are_land_not_narrow_bridges():
    scene = source()
    for contract in (
        "const REPOSITORY_CONNECTION_HALF_WIDTH = 32;",
        "const OFFICE_CONNECTION_HALF_WIDTH = 36;",
        '"hosted-repository-causeway-earth"',
        '"hosted-repository-promenade"',
        '"forkmesh-office-land-connection-earth"',
        '"forkmesh-office-land-connection-top"',
        "REPOSITORY_CONNECTION_HALF_WIDTH * 2",
        "OFFICE_CONNECTION_HALF_WIDTH * 2",
        '"forkmesh-curved-district-land-loop"',
        '"forkmesh-curved-district-land-top"',
    ):
        assert contract in scene
    assert "for (let x = -5.4; x <= 5.4; x += 1.2)" not in scene


def test_visible_land_connection_dimensions_drive_walkability():
    scene = source()
    for contract in (
        "px >= REPOSITORY_CONNECTION_MIN_X + margin",
        "px <= REPOSITORY_CONNECTION_MAX_X - margin",
        "Math.abs(pz) <= REPOSITORY_CONNECTION_HALF_WIDTH - margin",
        "Math.abs(px) <= OFFICE_CONNECTION_HALF_WIDTH - margin",
        "pz >= OFFICE_CONNECTION_MIN_Z + margin",
        "pz <= OFFICE_CONNECTION_MAX_Z - margin",
    ):
        assert contract in scene


def test_paths_share_a_concrete_brick_texture_and_closed_bike_lane():
    scene = source()
    for contract in (
        "function concreteBrickTexture(THREE)",
        "function concreteBrickMaterial(THREE)",
        '"/world/assets/concrete-brick-path-v1.webp"',
        "    8,\n    2,",
        "function curvedRibbonGeometry(",
        "new THREE.CatmullRomCurve3(",
        '"forkmesh-world-bike-lane"',
        "bikeLaneMaterial(THREE)",
    ):
        assert contract in scene


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
    assert "const minuteOfDay = now.getHours() * 60 + now.getMinutes();" in scene
    assert "const easedDaylight =" in scene
    assert "nextEnvironmentCheckAt = time + 15_000;" in scene
    assert "if (minuteOfDay !== localDaylightMinute)" in scene
