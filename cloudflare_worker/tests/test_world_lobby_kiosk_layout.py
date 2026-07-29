"""Lobby community kiosks share one wall-aligned row."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)


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

