from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text()
DATA = (ROOT / "public" / "world" / "world-data.js").read_text()
APP = (ROOT / "public" / "world" / "world.js").read_text()
ENTRY = (ROOT / "src" / "entry.py").read_text()


def test_town_has_four_solid_cardinal_paved_routes():
    for route in (
        '"east-repositories",\n      [junctionHalfWidth, 0],\n'
        '      [REPOSITORY_CONNECTION_MIN_X, 0]',
        '"north-office",\n      [0, -junctionHalfWidth],\n'
        '      [0, OFFICE_BRIDGE_START_Z]',
        '"west-billboards",\n      [-junctionHalfWidth, 0],\n'
        '      [LEADERBOARD_CONNECTION_MAX_X, 0]',
        '"south-members",\n      [0, junctionHalfWidth],\n'
        '      [0, MEMBER_PATH_END_Z]',
    ):
        assert route in SCENE
    assert '"forkmesh-town-path-junction"' in SCENE
    assert '"forkmesh-town-stone-plaza"' not in SCENE
    assert "function addRoundedCausewayEnds(" not in SCENE
    assert '"forkmesh-continuous-city-land"' in SCENE
    assert "concreteBrickMaterial(THREE)" in SCENE
    for district in ("repository", "leaderboard", "office", "member"):
        assert f"{district}" in SCENE


def test_all_repositories_live_expanded_on_the_east_island():
    assert "position: [130, 0, 0]" in DATA
    assert "const coreRecords = [];" in SCENE
    assert "const hostedRecords = records;" in SCENE
    assert "const islandRecord = true;" in SCENE
    assert "repository-always-expanded-profile:" in SCENE
    assert "repository-expanded-ring-" in SCENE
    assert "label.visible = true;" in SCENE


def test_billboards_use_one_aligned_perimeter():
    assert "function placeBillboardOnIsland(object)" in SCENE
    assert "const relayoutBillboardCircle = () =>" in SCENE
    assert "billboardIslandObjects.push({ object });" in SCENE
    assert "Math.cos(angle) * radius" in SCENE
    assert "Math.sin(angle) * radius" in SCENE
    for object_name in (
        "worldBulletin",
        "worldGeneralChatBoard",
        "mastodonKiosk",
        "twitterBanner",
        "redditBanner",
        "blogBanner",
        "statusBanner",
        "officeTaskBulletin",
        "worldQaBoard",
    ):
        assert f"placeBillboardOnIsland({object_name}" in SCENE or (
            object_name in SCENE
            and "placeBillboardOnIsland(board))" in SCENE
        )


def test_members_share_continuous_land_with_path_under_dirt_and_open_entrance():
    assert "const MEMBER_ISLAND_CENTER_Z = 130;" in SCENE
    assert "const MEMBER_PATH_END_Z = MEMBER_ISLAND_CENTER_Z;" in SCENE
    assert '"forkmesh-continuous-city-land"' in SCENE
    assert '"forkmesh-member-island"' not in SCENE
    assert '"forkmesh-member-island-connection"' not in SCENE
    assert '"forkmesh-member-promenade"' not in SCENE
    assert '"forkmesh-members-circle-path-sign"' not in SCENE
    assert '"🔥  MEMBERS CIRCLE"' not in SCENE
    assert "campfireGround.position.y = WORLD_PATH_SURFACE_Y + 0.01;" in SCENE
    assert (
        "startHereBoard.position.set(0, 0, MEMBER_ISLAND_CENTER_Z + 38);"
        in SCENE
    )
    assert "startHereBoard.rotation.y = Math.PI;" in SCENE
    assert "function worldWalkSurfaceContains(x, z" in SCENE
    assert "position: [0, 0, 130]" in DATA
    assert 'landmarkObjects.set("campfire", campfire);' in SCENE
    assert "const POSITION_RADIUS = 620;" in APP
    assert "or abs(x) > 620 or abs(y) > 100 or abs(z) > 620" in ENTRY


def test_live_nodes_remain_in_the_central_service_yard():
    assert (
        "const SERVER_CABINET_YARD_ORIGIN = Object.freeze([18, 0, 0]);"
        in SCENE
    )
    assert "west-billboards:network-node" not in SCENE
    assert "south-members:network-node" not in SCENE
