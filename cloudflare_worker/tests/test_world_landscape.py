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
        "texture.repeat.set(8, 2)",
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
        "`west-billboards-row-v2:${layoutId}`",
        "billboardIslandCursor + width / 2",
    ):
        assert contract in scene
    assert "Math.cos(angle) * LEADERBOARD_ISLAND_BOARD_RADIUS" not in scene


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
