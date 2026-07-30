"""Walk-in instance launcher, secret boundary, travel, and celebration contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
CSS = (ROOT / "public/world/world.css").read_text(encoding="utf-8")
ENTRY = (ROOT / "src/entry.py").read_text(encoding="utf-8")
QT_MAIN = (
    ROOT.parent / "qt_client/src/main.cpp"
).read_text(encoding="utf-8")
QT_CONTROL = (
    ROOT.parent / "qt_client/src/MainWindowControlNode.cpp"
).read_text(encoding="utf-8")


def test_outdoor_world_portal_uses_a_flat_non_modal_device_local_form():
    assert 'instanceBooth.name = "forkmesh-instance-launch-booth"' in SCENE
    assert 'userData.interactive = "instance-launch-booth"' in SCENE
    assert "INSTANCE_GARDEN_POSITION" in SCENE
    assert "LAUNCH INSTANCE" in SCENE
    assert "CREATE A NEW WORLD" in SCENE
    assert "world.add(instanceBooth)" in SCENE
    assert "insideInstanceBooth && !instanceBoothOccupied" in SCENE
    assert "onInstanceBoothSelect();" in SCENE
    assert 'class="world-instance-launcher"' in APP
    assert 'role="region"' in APP
    assert 'aria-modal="true"' not in APP.split(
        'class="world-instance-launcher"', 1
    )[1].split("</section>", 1)[0]
    assert ".world-instance-launcher[data-open=\"true\"]" in CSS
    assert "border-radius: 0" in CSS


def test_front_garden_water_and_recreation_are_interactive():
    assert 'officeFrontGarden.name = "forkmesh-office-front-garden"' in SCENE
    assert 'waterfall.name = "forkmesh-interactive-waterfall"' in SCENE
    assert "new THREE.MeshPhysicalMaterial" in SCENE
    assert 'userData.interactive = "garden-waterfall"' in SCENE
    assert 'drinkingFountain.name = "forkmesh-drinking-fountain"' in SCENE
    assert 'userData.interactive = "drinking-fountain"' in SCENE
    assert "onDrinkWater();" in SCENE
    assert "const GYM_POSITION = Object.freeze([66, 0.14, -137])" in SCENE
    assert "const SWING_SET_POSITION = Object.freeze([38, 0, -139])" in SCENE


def test_node_plaza_grows_and_sol_is_a_floating_fireball():
    assert 'nodeYardConcrete.name = "reward-node-concrete-plaza"' in SCENE
    assert "farthestRadius + 4.2" in SCENE
    assert 'sun.name = "reward-pool-fireball-sun"' in SCENE
    assert 'fireShell.name = "reward-pool-fireball-corona"' in SCENE
    fountain = SCENE.split("function createFountain(", 1)[1].split(
        "function compactSceneBytes", 1
    )[0]
    assert "const stem =" not in fountain


def test_launch_mirror_hands_token_to_desktop_without_a_secret_url():
    assert 'mirrorButton.userData.interactive = "launch-vultr-mirror"' in SCENE
    assert "launchVultrMirrorFromWorld" in APP
    launch = APP.split("async launchVultrMirrorFromWorld()", 1)[1].split(
        "validateInstanceLauncher(values)", 1
    )[0]
    assert "navigator.clipboard.writeText(token)" in launch
    assert 'launchURL.searchParams.set("mode", "vultr")' in launch
    assert 'launchURL.searchParams.set("token"' not in launch
    assert 'bounded(QStringLiteral("mode"), 16)' in QT_CONTROL
    assert "QApplication::clipboard()->clear()" in QT_CONTROL
    assert "createVultrMirrorFromForm();" in QT_CONTROL


def test_scope_help_matches_the_resources_used_by_the_bootstrapper():
    launcher = APP.split('class="world-instance-launcher"', 1)[1].split(
        "</form>", 1
    )[0]
    for permission in (
        "Account Settings · Read",
        "Workers Scripts · Edit",
        "D1 · Edit",
        "Cloudflare Tunnel · Edit",
        "Zone · Read",
        "DNS · Edit",
        "Workers Routes · Edit",
    ):
        assert permission in launcher
    assert "Do not use a Global API Key" in launcher


def test_secret_values_never_enter_the_native_url_or_setup_card():
    handoff = APP.split("async continueInstanceLaunch(form)", 1)[1].split(
        "downloadInstanceSetupCard()", 1
    )[0]
    assert 'new URL("forkmesh://control/cloudflare")' in handoff
    assert "cloudflareToken:" not in handoff.split(
        "Object.entries({", 1
    )[1].split("}))", 1)[0]
    assert "vpsPassword:" not in handoff.split(
        "Object.entries({", 1
    )[1].split("}))", 1)[0]
    assert "navigator.clipboard.writeText(values.cloudflareToken)" in handoff
    assert 'queryItems(QUrl::FullyDecoded)' in QT_MAIN
    assert "(?:token|secret|password|private|credential|api[_-]?key)" in QT_MAIN
    assert "query.queryItemValue(name, QUrl::FullyDecoded)" in QT_CONTROL
    assert "cloudflareFirstMirrorHost" in QT_CONTROL
    assert "cloudflareFirstMirrorPassword" in QT_CONTROL
    assert "installCloudflareFirstMirror()" in QT_CONTROL
    assert "runHostInstall(false" in QT_CONTROL
    card = APP.split("\n  downloadInstanceSetupCard() {", 1)[1].split(
        "async openLobbyLinkKiosk()", 1
    )[0]
    assert '["CREDENTIALS", "OMITTED · SESSION-ONLY"]' in card
    assert "values.cloudflareToken" not in card
    assert "values.vpsPassword" not in card


def test_approved_instances_become_world_portals_and_celebrate_for_ten_minutes():
    assert '"joinedAt": max(0, int(row.get("approved_at") or 0))' in ENTRY
    assert 'tower.userData.interactive = "federated-world-portal"' in SCENE
    assert 'label.userData.interactive = "federated-world-portal"' in SCENE
    assert "onFederatedWorldTravel" in SCENE
    assert "10 * 60 * 1000" in APP
    assert "startInstanceDirectoryPolling" in APP
    assert "world-instance-fireworks" in APP
    assert "A new independent World joined the federation" in APP
    assert "@keyframes world-instance-firework" in CSS
    assert "@media (prefers-reduced-motion: reduce)" in CSS
