"""Focused contracts for the member-circle and avatar profile polish."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCENE = (ROOT / "cloudflare_worker/public/world/world-scene.js").read_text()
APP = (ROOT / "cloudflare_worker/public/world/world.js").read_text()
HTML = (ROOT / "cloudflare_worker/public/world/index.html").read_text()
ENTRY = (ROOT / "cloudflare_worker/src/entry.py").read_text()


def test_member_circle_uses_dirt_and_tracks_firewood_per_member():
    assert "function campfireDirtTexture(THREE)" in SCENE
    assert 'campfireGround.name = "campfire-member-circle-dirt"' in SCENE
    assert "campfireGround.scale.setScalar(radius + 1.45)" in SCENE
    assert "function rebuildCampfireMemberLogs(total)" in SCENE
    assert "rebuildCampfireMemberLogs(count)" in SCENE
    assert "log.userData.memberLogIndex = index" in SCENE
    assert "fireLevel = clamp(1.08 + count * 0.012, 1.08, 1.85)" in SCENE


def test_email_pin_switches_between_verified_check_and_unverified_x():
    texture = SCENE.split(
        "function emailVerificationPinTexture", 1
    )[1].split("\nfunction ", 1)[0]
    assert 'verified ? "#9ef7c6" : "#ff6b72"' in texture
    assert "context.lineTo(65, 65)" in texture
    assert "context.lineTo(31, 65)" in texture
    sync = SCENE.split(
        "function syncAvatarVerifiedPin", 1
    )[1].split("\nfunction ", 1)[0]
    assert "pin.visible = registered" in sync
    assert "emailVerificationPinTexture(THREE, verified)" in sync


def test_public_chat_line_repaints_matching_avatar_chest_only():
    assert 'context.fillText("RECENT PUBLIC CHAT", 34, 352)' in SCENE
    assert "identity.recentPublicMessage" in SCENE
    assert "function setAvatarRecentPublicMessage(sender, text)" in SCENE
    assert "setAvatarRecentPublicMessage," in SCENE
    assert "this.world?.setAvatarRecentPublicMessage?.(" in APP


def test_user_selection_has_a_scoped_clock_and_prefers_avatar_hit():
    selection = SCENE.split(
        "function setActiveAvatarSelection", 1
    )[1].split("\n  function ", 1)[0]
    assert "const now = performance.now()" in selection
    assert "if (now >= nextAvatarHighlightAt)" in selection
    assert "const avatarHit = hits.find(" in SCENE
    assert "const hit = avatarHit || firstHit" in SCENE


def test_lobby_has_one_access_checked_warp_for_every_floor():
    assert '"forkmesh-office-floor-warp-hub"' in SCENE
    assert "OFFICE_FLOORS.forEach((floor, index) =>" in SCENE
    assert 'pad.userData.interactive = "office-floor-warp"' in SCENE
    assert "function warpToOfficeFloor(floorId)" in SCENE
    assert "canAccessOfficeFloor(officeFloorAccess, floor.id)" in SCENE
    assert 'interactive === "office-floor-warp"' in SCENE


def test_world_asset_loading_and_unavailable_actions_are_console_quiet():
    for asset in (
        "city-park-grass-v1.webp",
        "concrete-brick-path-v1.webp",
        "beach-horizon-v1.webp",
    ):
        line = next(line for line in HTML.splitlines() if asset in line)
        assert 'crossorigin="anonymous"' in line
    face = SCENE.split(
        "function makeConsentedProfileFace", 1
    )[1].split("\nfunction ", 1)[0]
    assert "avatar.origin === window.location.origin" in face
    assert "missing" in face
    handler = ENTRY.split(
        "async def repository_actions_status_handler", 1
    )[1].split("\n\nasync def ", 1)[0]
    assert '"state": "unavailable"' in handler
    assert '"retryable": True' in handler
    assert "status=503" not in handler
