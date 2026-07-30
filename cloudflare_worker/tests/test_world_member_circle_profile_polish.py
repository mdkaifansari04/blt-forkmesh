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
    # adhoc #427: the blaze is a milestone marker — it holds its size through a
    # hundred accounts and steps up only when the next century lands.
    assert "const CAMPFIRE_BASE_FIRE_LEVEL = 3;" in SCENE
    assert "const CAMPFIRE_FIRE_LEVEL_PER_CENTURY = 0.35;" in SCENE
    assert "const CAMPFIRE_MAX_FIRE_LEVEL = 5.4;" in SCENE
    assert "const fireCenturies = Math.floor(count / 100);" in SCENE
    assert (
        "CAMPFIRE_BASE_FIRE_LEVEL + fireCenturies * CAMPFIRE_FIRE_LEVEL_PER_CENTURY"
        in SCENE
    )
    # Growing cones are re-pinned to the logs so the fire never sinks into the pit.
    assert "flame.position.y = FLAME_BASE_Y + (FLAME_HEIGHT * height) / 2;" in SCENE


def test_member_total_hangs_high_and_large_above_the_fire():
    # adhoc #427: the count reads as the clearing's headline, so it sits well
    # clear of the flames and rises further as the fire grows.
    assert "const MEMBER_COUNT_HOVER_Y = 10.5;" in SCENE
    assert "memberCountSprite.scale.set(8, 4, 1);" in SCENE
    assert (
        "FLAME_HEIGHT * Math.max(0, fireLevel - CAMPFIRE_BASE_FIRE_LEVEL)" in SCENE
    )


def test_newest_member_name_has_an_animated_sparkle_effect():
    assert 'newestMemberSparkles.name = "campfire-newest-member-name-sparkles"' in SCENE
    assert "newestMemberSparkles.visible = Boolean(latest);" in SCENE
    assert "newestMemberSparkles.rotation.z = time * 0.0008;" in SCENE
    assert "newestMemberSparkles.position.y = memberCountSprite.position.y - 1.55;" in SCENE


def test_fresh_arrivals_spawn_seated_without_overriding_saved_views():
    bootstrap = APP.split("async bootstrap() {", 1)[1].split(
        "\n  setupControls()", 1
    )[0]
    assert bootstrap.index("this.syncMemberLounge();") < bootstrap.index(
        "this.seatFreshArrivalAtCampfire();"
    )
    assert bootstrap.index("this.seatFreshArrivalAtCampfire();") < bootstrap.index(
        "if (this.restoredPosition) {"
    )
    arrival = APP.split("seatFreshArrivalAtCampfire() {", 1)[1].split(
        "\n  returnToCampfireBench()", 1
    )[0]
    assert "this.restoredPosition ||" in arrival
    assert "this.sharedView ||" in arrival
    assert "this.spawnSelected ||" in arrival
    assert 'this.currentSpace !== "town-square"' in arrival
    assert 'this.world?.returnToCampfireBench?.(this.identity?.name || "")' in arrival
    assert "this.freshArrivalCampfireSeated = true;" in arrival
    welcome = APP.split(
        'if (message.type === "welcome" && Array.isArray(message.peers)) {', 1
    )[1].split(
        '} else if (["presence", "join"].includes(message.type)', 1
    )[0]
    assert "this.initialPresenceWelcomePending &&" in welcome
    assert "this.syncMemberLounge();" in welcome
    assert 'this.world?.returnToCampfireBench?.(this.identity?.name || "");' in welcome


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


def test_world_users_do_not_have_a_separate_top_hair_cap():
    avatar = SCENE.split(
        "function createAvatar(THREE, identity, options = {})", 1
    )[1].split("\nconst WORK_BADGE_ROWS", 1)[0]
    assert "const hair = new THREE.Mesh(" not in avatar
    assert "group.add(hair)" not in avatar


def test_user_selection_has_a_scoped_clock_and_prefers_avatar_hit():
    selection = SCENE.split(
        "function setActiveAvatarSelection", 1
    )[1].split("\n  function ", 1)[0]
    assert "const now = performance.now()" in selection
    assert "if (now >= nextAvatarHighlightAt)" in selection
    assert "const avatarHit = hits.find(" in SCENE
    assert "const hit = avatarHit || firstHit" in SCENE


def test_lobby_has_wall_doorways_with_access_and_walk_through_checks():
    assert '"forkmesh-office-floor-warp-hub"' in SCENE
    assert "OFFICE_FLOORS.forEach((floor, index) =>" in SCENE
    assert "`forkmesh-office-floor-doorway-${floor.id}`" in SCENE
    assert "OFFICE_FLOOR_WARP_DOOR_START_X +" in SCENE
    assert 'portal.userData.interactive = "office-floor-warp"' in SCENE
    assert 'closedDoor.userData.interactive = "office-floor-warp"' in SCENE
    assert '"CLOSED · ACCESS REQUIRED"' in SCENE
    assert "function refreshOfficeFloorWarpDoors()" in SCENE
    assert "doorway.closedDoor.visible = !allowed" in SCENE
    assert "function warpToOfficeFloor(floorId)" in SCENE
    assert "canAccessOfficeFloor(officeFloorAccess, floor.id)" in SCENE
    assert "function tryOfficeFloorWarpDoorway(avatar, previousPosition)" in SCENE
    assert "current.z > OFFICE_FLOOR_WARP_TRIGGER_Z" in SCENE
    assert "tryOfficeFloorWarpDoorway(avatar, previousPosition)" in SCENE
    assert 'interactive === "office-floor-warp"' in SCENE
    assert "hit.object.userData.officeFloorTargetId" in SCENE


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
