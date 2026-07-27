"""Public in-world progress bulletin contracts."""

from pathlib import Path


SCENE = (
    Path(__file__).resolve().parents[1]
    / "public/world/world-scene.js"
).read_text(encoding="utf-8")


def test_town_square_has_a_scene_native_progress_bulletin():
    assert 'officeTaskBulletin.name = "forkmesh-office-task-bulletin"' in SCENE
    assert "world.add(officeTaskBulletin)" in SCENE
    assert 'registerMovableObject("office-task-bulletin"' in SCENE
    assert "worldTaskBulletinTexture(THREE" in SCENE
    assert "officeTaskBulletin.position.set(34, 3.1, -17.5)" in SCENE
    assert "directly beside" in SCENE


def test_bulletin_separates_every_remaining_task_from_completed_work():
    board = SCENE[
        SCENE.index("const WORLD_TASK_BULLETIN_ITEMS"):
        SCENE.index("\nfunction worldTaskBulletinSeed")
    ]
    for task in (
        "Land issues on online mirrors now",
        "Open full mirror status from repo header",
        "Securely review and merge chat PR 57",
        "Add org Claude bot on headless mirrors",
        "Let Claude walk and answer @claude",
        "Add org Codex bot on headless mirrors",
        "Let Codex walk and answer @codex",
        "Codex moves completed notes to Done",
    ):
        assert task in board
    assert "Move repo controls into Settings" not in board
    assert "Build the in-world task bulletin" not in board
    assert "Fix live Office attendance" not in board
    assert "Fix Assign to taskboard colors" not in board
    assert "filter((item) => !item.done)" in SCENE
    assert "DRAG TO PRIORITIZE · 1 IS HIGHEST" in SCENE
    assert "`#${index + 1}`" in SCENE
    assert "IN PROGRESS" in SCENE
    assert "item.estimate" in SCENE


def test_pending_notes_are_marker_drawn_and_repo_issues_have_adjacent_board():
    assert "worldTaskBulletinSeed(`${side}:${item.task}`)" in SCENE
    assert '"Marker Felt", "Segoe Print", "Comic Sans MS"' in SCENE
    assert 'drawNote(item, index, "pending")' in SCENE
    assert "context.rotate(angle)" in SCENE
    assert '"forkmesh-repo-issues-board-face"' in SCENE
    assert "worldRepoIssuesTexture" in SCENE
    assert "DRAG AN ISSUE ONTO THE BUILD BOARD TO ASSIGN IT" in SCENE
    assert '"build-task-board"' in SCENE
    assert '"repo-issues-board"' in SCENE
