"""Direct Shift-drag editing uses the persisted administrator layout."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
SHELL = (ROOT / "public" / "world" / "world.js").read_text(
    encoding="utf-8")


def test_shift_drag_moves_only_layout_editable_objects_and_commits():
    assert "layoutEditingEnabled && event.shiftKey" in SCENE
    assert "const object = layoutObjectAtPointer();" in SCENE
    assert "draggedLayoutObject" in SCENE
    assert "moveWorldObject(" in SCENE
    assert "commitLayoutObject(drag.object)" in SCENE
    assert "onLayoutObjectMoved(move)" in SCENE
    assert 'this.postJSON("/api/world/layout"' in SHELL
    assert "this.identity?.isAdmin" in SHELL


def test_feedback_email_destination_has_a_real_lobby_kiosk():
    assert "forkmesh-office-feedback-kiosk" in SCENE
    assert 'userData.interactive = "office-feedback-kiosk"' in SCENE
    assert "onLobbyFeedbackKioskSelect" in SCENE
    assert "openLobbyFeedbackKiosk" in SHELL
    assert 'worldQuery.get("feedback") === "1"' in SHELL
    assert 'source: "world"' in SHELL
    assert 'vote: "feedback"' in SHELL

