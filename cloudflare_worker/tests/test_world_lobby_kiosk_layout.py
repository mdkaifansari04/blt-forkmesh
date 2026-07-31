"""Lobby community kiosks share one wall-aligned row."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")


def test_lobby_kiosks_line_up_left_of_the_office_clock():
    for contract in (
        "officeFeedbackKiosk.position.set(84, 0, -52);",
        "officeTaskBidKiosk.position.set(84, 0, -37);",
        "officeLinkKiosk.position.set(84, 0, -22);",
        "officeLinkRewards.position.set(84, 0, -6);",
        "officeFeedbackKiosk.rotation.y = -Math.PI / 2;",
        "officeTaskBidKiosk.rotation.y = -Math.PI / 2;",
        "officeLinkKiosk.rotation.y = -Math.PI / 2;",
        "officeLinkRewards.rotation.y = -Math.PI / 2;",
        "officeClockInBoard.position.set(84.5, 7.1, 18);",
    ):
        assert contract in SCENE


def test_feedback_email_destination_has_a_real_lobby_kiosk():
    assert "forkmesh-office-feedback-kiosk" in SCENE
    assert 'userData.interactive = "office-feedback-kiosk"' in SCENE
    assert "onLobbyFeedbackKioskSelect" in SCENE
    assert "openLobbyFeedbackKiosk" in WORLD
    assert 'worldQuery.get("feedback") === "1"' in WORLD
    assert 'source: "world"' in WORLD
    assert 'vote: "feedback"' in WORLD
