from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
HOME = (PUBLIC / "index.html").read_text(encoding="utf-8")
SCENE = (PUBLIC / "world" / "world-scene.js").read_text(encoding="utf-8")
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def test_mobile_home_header_has_live_world_count_between_brand_and_menu():
    brand = HOME.index('class="brand flex items-center space-x-2"')
    live = HOME.index('class="mobile-world-join"')
    menu = HOME.index('id="mobile-menu-toggle"')
    assert brand < live < menu
    assert 'data-home-world-count' in HOME
    assert 'href="/world">Join World</a>' in HOME
    assert 'fetch("/api/world/online"' in HOME
    assert "@media (max-width: 1023px)" in HOME


def test_live_world_count_comes_from_current_durable_object_sockets():
    assert 'path.rstrip("/") == "/api/world/online"' in ENTRY
    assert '"online": len(self._live_sockets(cleanup=True))' in ENTRY
    assert '("/api/world/online", "/api/world/online/")' in ENTRY


def test_elevator_uses_upper_corner_first_person_framing_and_restores_view():
    assert "function lockOfficeElevatorCamera()" in SCENE
    assert 'setCameraMode("first-person", "office-elevator-enter")' in SCENE
    assert "new THREE.Vector3(-3.72, 6.46, 2.7)" in SCENE
    assert "new THREE.Vector3(2.45, 2.9, -3.7)" in SCENE
    assert "function releaseOfficeElevatorCamera(released = true)" in SCENE
    assert 'setCameraMode(prior, "office-elevator-exit")' in SCENE
