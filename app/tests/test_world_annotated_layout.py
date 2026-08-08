from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT.parent / "world" / "public" / "world" / "world-scene.js").read_text()
DATA = (ROOT.parent / "world" / "public" / "world" / "world-data.js").read_text()
APP = (ROOT.parent / "world" / "public" / "world" / "world.js").read_text()
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


def test_all_repositories_share_one_east_island_ring():
    assert "position: [130, 0, 0]" in DATA
    assert "const coreRecords = [];" in SCENE
    assert "const hostedRecords = records;" in SCENE
    assert "const islandRecord = true;" in SCENE
    assert "repository-always-expanded-profile:" in SCENE
    assert "repository-expanded-ring-" in SCENE
    assert "label.visible = true;" in SCENE


def test_repository_rings_expand_only_for_a_visitor_inside_the_circle():
    """Portals are bare discs from the town; the rings are a room-scale detail."""
    # Every profile is built folded, including the ones a catalog refresh
    # rebuilds while somebody is already standing in the ring.
    assert "applyRepositoryRingExpansion(expandedProfile);" in SCENE
    assert "node.userData.repositoryExpandedProfile = expandedProfile;" in SCENE
    assert "function applyRepositoryRingExpansion(profile)" in SCENE
    assert "profile.visible = repositoryRingExpansion > 0.002;" in SCENE
    assert "const REPOSITORY_RING_COLLAPSED_SCALE = 1.08 / 1.58;" in SCENE
    # Expansion is driven by the same state the enclosure is, so "inside the
    # repository ring" is decided in exactly one place.
    assert 'repositoryEnclosureState === "raising" ||' in SCENE
    assert 'repositoryEnclosureState === "sealed"' in SCENE


def test_repository_ring_seals_a_shell_that_stops_drawing_the_outside_world():
    assert "function createRepositoryEnclosure(THREE)" in SCENE
    assert 'group.name = "repository-enclosure";' in SCENE
    # Wall, transom over the doorway and dome cap: one closed opaque surface.
    assert 'wall.name = "repository-enclosure-wall";' in SCENE
    assert 'transom.name = "repository-enclosure-door-transom";' in SCENE
    assert 'dome.name = "repository-enclosure-dome";' in SCENE
    # The cull itself: the shell is a third enclosure mode, so a sealed ring
    # hides every world root the way the Office and the beach already do.
    assert 'if (mode === "repositories") return repositoryEnclosureKeptRoots();' in SCENE
    assert "function repositoryEnclosureKeptRoots()" in SCENE
    assert 'repositoryEnclosureState === "sealed"\n          ? "repositories"' in SCENE
    # Somebody who walks into the sealed ring has to become visible again.
    assert "if (enclosureHiddenWorldRoots.has(root)) {" in SCENE
    assert "root.visible = enclosureHiddenWorldRoots.get(root);" in SCENE


def test_repository_enclosure_has_a_real_door_and_a_dissolve():
    # The wall collider and the wall geometry read the same door angle, so the
    # only way through the surface is the opening you can see.
    assert "const REPOSITORY_ENCLOSURE_DOOR_THETA = -Math.PI / 2;" in SCENE
    assert "function constrainRepositoryEnclosureWall()" in SCENE
    assert "constrainRepositoryEnclosureWall();" in SCENE
    assert (
        "if (Math.abs(doorOffset) <= REPOSITORY_ENCLOSURE_DOOR_HALF_ANGLE) {"
        in SCENE
    )
    assert 'sign.name = "repository-enclosure-door-sign";' in SCENE
    assert 'repositorySizeLabelSprite(\n    THREE,\n    "EXIT",' in SCENE
    # Leaving does not switch the cover off, it plays it out.
    assert "const REPOSITORY_ENCLOSURE_DISSOLVE_MS = 900;" in SCENE
    assert '"dissolving"' in SCENE
    assert "(1 - progress) ** 1.6," in SCENE
    # Hysteresis: sealing and releasing are different radii, so standing in the
    # doorway cannot flicker the shell.
    assert (
        "const REPOSITORY_ENCLOSURE_SEAL_RADIUS = "
        "REPOSITORY_ENCLOSURE_RADIUS - 2.2;" in SCENE
    )
    assert (
        "const REPOSITORY_ENCLOSURE_RELEASE_RADIUS = "
        "REPOSITORY_ENCLOSURE_RADIUS + 1.6;" in SCENE
    )


def test_billboards_are_split_across_three_aligned_circles():
    assert "function placeBillboardOnIsland(object, options = {})" in SCENE
    assert "const relayoutBillboardCircle = (circle) =>" in SCENE
    assert "circle.objects.push(record);" in SCENE
    assert "Math.cos(angle) * radius" in SCENE
    assert "Math.sin(angle) * radius" in SCENE
    assert "const BILLBOARD_CIRCLE_SPECS = Object.freeze({" in SCENE
    for circle_id in ("hub", "social", "leaderboards"):
        assert f"forkmesh-${{spec.id}}-billboard-circle" in SCENE
        assert f"{circle_id}: Object.freeze({{" in SCENE
    # Each board names the circle it stands on; none of them share one ring.
    for object_name, circle_id in (
        ("worldBulletin", "hub"),
        ("worldGeneralChatBoard", "hub"),
        ("statusBanner", "hub"),
        ("officeTaskBulletin", "hub"),
        ("worldQaBoard", "hub"),
        ("worldDiscordBoard", "social"),
        ("mastodonKiosk", "social"),
    ):
        placement = SCENE.split(f"placeBillboardOnIsland({object_name}", 1)
        assert len(placement) == 2, object_name
        assert f'circle: "{circle_id}"' in placement[1][:120], object_name
    # The three social banners are placed together on the social circle.
    banners = SCENE.split("[twitterBanner, redditBanner, blogBanner].forEach", 1)
    assert len(banners) == 2
    assert 'circle: "social", feed: "social"' in banners[1][:200]


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
