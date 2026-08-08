from pathlib import Path


SCENE = (
    Path(__file__).resolve().parents[2]
    / "world"
    / "public"
    / "world"
    / "world-scene.js"
).read_text(encoding="utf-8")


def test_beach_is_an_isolated_scene_with_paired_portals():
    for contract in (
        'beachScene.name = "forkmesh-beach-scene"',
        'beachScene.userData.beachScene = true',
        '"forkmesh-beach-entry-portal"',
        '"beach-scene-enter"',
        '"forkmesh-beach-return-portal"',
        '"beach-scene-exit"',
        'if (mode === "beach") return [beachScene]',
        'world.userData.activeEnclosureScene = next',
    ):
        assert contract in SCENE


def test_beach_travel_switches_walk_surfaces_and_can_return_to_town():
    for contract in (
        "beachSceneActive = active",
        'currentSpace = active ? "beach" : "town-square"',
        "? beachWalkSurfaceContains(x, z, radius)",
        "const enterBeachScene = () => setBeachScene(true)",
        "const leaveBeachScene = () => setBeachScene(false)",
        "enterBeachScene,",
        "leaveBeachScene,",
    ):
        assert contract in SCENE
