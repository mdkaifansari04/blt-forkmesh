"""Office checkout keeps one close camera distance across the façade."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")


def test_office_exit_zooms_in_before_crossing_and_keeps_that_zoom_outside():
    assert "const OFFICE_EXIT_CAMERA_ZOOM = 0.48;" in SCENE

    begin = SCENE[
        SCENE.index("function beginOfficeExit()"):
        SCENE.index("function setCameraMode(", SCENE.index("function beginOfficeExit()"))
    ]
    assert 'setCameraMode("third-person", "office-exit-approach")' in begin
    assert "cameraFocus = null;" in begin
    assert "cameraZoom = Math.min(cameraZoom, OFFICE_EXIT_CAMERA_ZOOM);" in begin

    leave = SCENE[
        SCENE.index("function leaveOfficeInterior()"):
        SCENE.index("function beginOfficeExit()")
    ]
    assert 'setCameraMode("third-person", "office-exit")' in leave
    assert "cameraZoom = OFFICE_EXIT_CAMERA_ZOOM;" in leave
    assert "cameraZoom = 1;" not in leave


def test_rooftop_space_jump_checks_out_after_clearing_the_perimeter():
    assert "function beginOfficeRoofJump()" in SCENE
    assert "jumpVelocity = 10.8;" in SCENE
    assert "function completeOfficeRoofExit()" in SCENE
    assert 'activity: "jumping from the Office roof"' in SCENE
    assert "officeExitHandler?.();" in SCENE
    assert "const roofJumpStarted = beginOfficeRoofJump();" in SCENE


def test_every_office_chair_family_uses_the_shared_sit_interaction():
    assert 'child.userData.interactive = "office-chair";' in SCENE
    assert 'officeChairs.set(chairId, chair);' in SCENE
    assert 'officeChairs.set(deskChairId, deskChair);' in SCENE
    assert 'chairId.startsWith("marketing-desk-chair:")' in SCENE
    assert "sitOnOfficeChair(hit.object.userData.officeChairId)" in SCENE
