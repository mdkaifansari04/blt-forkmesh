from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
SCENE = (ROOT.parent / "world" / "public" / "world" / "world-scene.js").read_text(encoding="utf-8")
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def test_live_world_count_comes_from_current_durable_object_sockets():
    assert 'path.rstrip("/") == "/api/world/online"' in ENTRY
    assert '"online": len(self._live_sockets(cleanup=True))' in ENTRY
    assert '("/api/world/online", "/api/world/online/")' in ENTRY


def test_elevator_uses_upper_corner_first_person_framing_and_restores_view():
    assert "function lockOfficeElevatorCamera()" in SCENE
    assert 'setCameraMode("first-person", "office-elevator-enter")' in SCENE
    assert "cameraEye.set(-3.72, 6.46, 2.7)" in SCENE
    assert "cameraLookTarget.set(3.4, 3.15, -5.4)" in SCENE
    assert "elevatorPanel.position.set(4.34, 3.3, -0.6)" in SCENE
    assert "elevatorPanel.rotation.y = -Math.PI / 2" in SCENE
    assert "camera.fov = 58" in SCENE
    assert "function releaseOfficeElevatorCamera(released = true)" in SCENE
    assert 'setCameraMode(prior, "office-elevator-exit")' in SCENE
