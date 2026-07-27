"""Public in-world progress bulletin contracts."""

from pathlib import Path


SCENE = (
    Path(__file__).resolve().parents[1]
    / "public/world/world-scene.js"
).read_text(encoding="utf-8")


def test_office_lobby_has_a_scene_native_progress_bulletin():
    assert 'officeTaskBulletin.name = "forkmesh-office-task-bulletin"' in SCENE
    assert "officeInterior.add(officeTaskBulletin)" in SCENE
    assert "worldTaskBulletinTexture(THREE)" in SCENE
    assert "officeTaskBulletin.position.set(84.3, 10.7, 10)" in SCENE


def test_bulletin_separates_every_remaining_task_from_completed_work():
    board = SCENE[
        SCENE.index("const WORLD_TASK_BULLETIN_ITEMS"):
        SCENE.index("\nfunction worldTaskBulletinSeed")
    ]
    for task in (
        "Add real PR section tabs",
        "Let admins group offline users",
        "Add the HTTP Referer leaderboard",
        "Review and merge mdkaifan direct chat",
        "Run focused worker API tests",
        "Run focused Qt pull and issue tests",
        "Confirm only intentional changes remain",
    ):
        assert task in board
    assert "filter((item) => !item.done)" in SCENE
    assert "filter((item) => item.done)" in SCENE


def test_done_notes_are_varied_and_crossed_out_like_marker_tasks():
    assert "worldTaskBulletinSeed(`${side}:${item.task}`)" in SCENE
    assert '"Marker Felt", "Segoe Print", "Comic Sans MS"' in SCENE
    assert 'drawNote(item, index, "pending")' in SCENE
    assert 'drawNote(item, index, "completed")' in SCENE
    assert 'context.strokeStyle = "rgba(164,29,38,0.9)"' in SCENE
    assert "context.rotate(angle)" in SCENE
