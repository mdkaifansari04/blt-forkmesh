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


def test_world_land_has_finished_dirt_strata_and_roots_underneath():
    scene = source()
    for contract in (
        "function createTerrainFoundation(",
        'earth.name = `${name}-earth`',
        'ring.name = `${name}-strata-${index + 1}`',
        'root.name = `${name}-root-${index + 1}`',
        '"forkmesh-town-terrain-foundation"',
        '"hosted-repository-terrain-foundation"',
        '"forkmesh-office-terrain-foundation"',
    ):
        assert contract in scene


def test_repository_and_office_connections_are_land_not_narrow_bridges():
    scene = source()
    for contract in (
        "const REPOSITORY_CONNECTION_HALF_WIDTH = 26;",
        "const OFFICE_CONNECTION_HALF_WIDTH = 36;",
        '"hosted-repository-causeway-earth"',
        '"hosted-repository-promenade"',
        '"forkmesh-office-land-connection-earth"',
        '"forkmesh-office-land-connection-top"',
        "REPOSITORY_CONNECTION_HALF_WIDTH * 2",
        "OFFICE_CONNECTION_HALF_WIDTH * 2",
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
