from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text()
DATA = (ROOT / "public" / "world" / "world-data.js").read_text()


def test_town_has_four_solid_cardinal_paved_routes():
    for route in (
        '"east-repositories", [16, 0], [88, 0], 8.4',
        '"north-office", [0, -16], [0, -88], 8.4',
        '"west-billboards", [-16, 0], [-88, 0], 8.4',
        '"south-members", [0, 16], [0, 88], 8.4',
    ):
        assert route in SCENE
    assert "function addRoundedCausewayEnds(" in SCENE
    for district in ("repository", "leaderboard", "office", "member"):
        assert f"{district}" in SCENE


def test_all_repositories_live_expanded_on_the_east_island():
    assert "const coreRecords = [];" in SCENE
    assert "const hostedRecords = records;" in SCENE
    assert "const islandRecord = true;" in SCENE
    assert "repository-always-expanded-profile:" in SCENE
    assert "repository-expanded-ring-" in SCENE
    assert "label.visible = true;" in SCENE


def test_billboards_use_the_west_island_and_ignore_legacy_coordinates():
    assert "function placeBillboardOnIsland(object, layoutId)" in SCENE
    assert "movableWorldObjects.delete(layoutId);" in SCENE
    assert "const districtLayoutId = `west-billboards:${layoutId}`;" in SCENE
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
            object_name in SCENE and "placeBillboardOnIsland(board, layoutId)"
            in SCENE
        )


def test_members_have_a_walkable_south_island_and_campfire():
    assert "const MEMBER_ISLAND_CENTER_Z = 130;" in SCENE
    assert '"forkmesh-member-island"' in SCENE
    assert '"forkmesh-member-island-connection"' in SCENE
    assert '"forkmesh-member-promenade"' in SCENE
    assert "Math.hypot(px, pz - MEMBER_ISLAND_CENTER_Z)" in SCENE
    assert "position: [0, 0, 130]" in DATA
    assert 'registerMovableObject("south-members:campfire", campfire);' in SCENE


def test_live_nodes_remain_in_the_central_service_yard():
    assert (
        "const SERVER_CABINET_YARD_ORIGIN = Object.freeze([18, 0, 0]);"
        in SCENE
    )
    assert "west-billboards:network-node" not in SCENE
    assert "south-members:network-node" not in SCENE
