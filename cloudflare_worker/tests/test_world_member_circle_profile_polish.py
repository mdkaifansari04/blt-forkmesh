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
    assert "campfireGround.scale.setScalar(outerRadius + 1.45)" in SCENE
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
    # The procedural fire stays rooted at the logs while milestone growth is
    # applied to the effect itself, so it cannot sink into the pit.
    assert "proceduralFire.position.y = FLAME_BASE_Y;" in SCENE
    assert "const fireGrowth = fireLevel / CAMPFIRE_BASE_FIRE_LEVEL;" in SCENE
    assert "proceduralFire.scale.set(" in SCENE


def test_campfire_uses_layered_procedural_flames_and_atmosphere():
    effect = SCENE.split("function createProceduralCampfireEffect(THREE)", 1)[1]
    effect = effect.split("\n// Who joined last", 1)[0]
    assert "new THREE.ConeGeometry" not in effect
    assert "const flameLayers = [" in effect
    assert "fragmentShader: flameFragmentShader" in effect
    assert "blending: THREE.AdditiveBlending" in effect
    assert "if (alpha < 0.008) discard;" in effect
    assert '"campfire-rising-embers"' in effect
    assert '"campfire-ground-glow"' in effect
    assert "campfire-smoke-puff-" in effect
    assert '"campfire-charred-burning-logs"' in SCENE
    assert "fireLight.position.set(" in SCENE


def test_member_count_floats_high_above_the_fire_without_a_plate():
    assert "function campfireMemberCountTexture(THREE, total, newest = \"\")" in SCENE
    assert 'memberCountSprite.name = "campfire-member-count-high"' in SCENE
    assert "memberCountSprite.position.y = 13.5;" in SCENE
    assert "memberCountSprite.scale.set(10.8, 4.05, 1);" in SCENE


def test_newest_member_is_named_with_sparkles_high_above_the_fire():
    assert '"✦ NEWEST MEMBER ✦"' in SCENE
    assert 'newestMemberSparkles.name = "campfire-newest-member-name-sparkles"' in SCENE
    assert "newestMemberSparkles.visible = Boolean(latest);" in SCENE


def test_campfire_is_three_times_large_not_only_three_times_tall():
    assert (
        "proceduralFire.scale.set(\n"
        "    CAMPFIRE_BASE_FIRE_LEVEL,\n"
        "    CAMPFIRE_BASE_FIRE_LEVEL,"
    ) in SCENE
    assert (
        "CAMPFIRE_BASE_FIRE_LEVEL * (1 + (fireGrowth - 1) * 0.32)"
        in SCENE
    )
    assert "fireLevel * (1 + slowFlicker * 0.055" in SCENE
    assert "new THREE.CylinderGeometry(1.45, 1.65, 0.22, 16)" in SCENE
    assert "Math.cos(stoneAngle) * 1.85" in SCENE


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
    assert "FRESH_ARRIVAL_CAMPFIRE_PREVIEW" in APP
    assert "restoreCampfireSeatIfNearby" in APP
    assert "function restoreCampfireSeatIfNearby(spawn, name)" in SCENE
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
    assert 'context.fillText("RECENT PUBLIC CHAT", 34, 356)' in SCENE
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


def test_avatar_profile_signals_moved_to_head_and_back():
    assert "const AVATAR_FACE_DEPTH = 0.08;" in SCENE
    assert "shirtMeshes: [torso, leftArm, rightArm, head]" in SCENE
    assert 'activityRing.name = "avatar-activity-ring"' in SCENE
    assert '"avatar-mood-icon-word"' not in SCENE
    assert "MOOD · ${frontMood}" in SCENE
    assert 'backName.name = "avatar-back-username"' in SCENE
    assert "const firstCounts = `PRS ${pulls} · ISSUES ${issues}`;" in SCENE
    assert "const discussionCounts = `DISCUSSIONS ${discussions}`;" in SCENE
    assert 'context.fillText("OPEN FILTERED ACTIVITY ↗", 320, 181);' in SCENE
    assert "`TASKS ${taskTotal} · RUNNING ${taskActive}`" in SCENE
    assert '"SELECT USER FOR FULL PROFILE"' in SCENE
    assert "typeTotals:" in APP
    assert "#contributions" in APP


def test_badge_has_large_qr_email_and_social_icons_without_activity_border():
    assert "context.strokeRect(376, 12, 124, 124);" in SCENE
    assert "drawQrModules(context, walletAddress, 384, 20, 108);" in SCENE
    assert '"LAST EMAIL · NOT SHARED"' in SCENE
    assert "loadWorldEmailActivity" in APP
    assert "[270, 294].forEach" in SCENE
    assert "[326, 350, 374].forEach" in SCENE
    assert "const activityBorder =" not in SCENE
    assert "verifiedPin.position.set(-0.66, 2.92, -0.22);" in SCENE


def test_mirror_lights_are_colored_and_warning_states_blink():
    assert "function mirrorNodeVisualState(node)" in SCENE
    assert 'return { color: "#22e06a", blink: false };' in SCENE
    assert 'return { color: "#ffd23f", blink: true };' in SCENE
    assert 'return { color: "#ff4d57", blink: true };' in SCENE
    assert "child.userData?.mirrorStatusLight" in SCENE
    assert "mirrorByName" in APP


def test_directory_members_stay_seated_at_the_campfire():
    assert 'status: "sitting around the campfire"' in SCENE
    assert "figure.position.add(seat)" in SCENE
    assert "applySeatedLegPose(figure)" in SCENE
    assert "figure.userData.ambientInteraction = null" in SCENE
    assert "const memberWorldDestinations" not in SCENE
    assert "const memberNpcStations" not in SCENE
    assert "ambientRoutePosition" not in SCENE


def test_camera_pan_and_tilt_turn_the_avatar_body_and_head():
    assert "function applyAvatarLookDirection(avatar, yaw, pitch, turnBody = true)" in SCENE
    assert 'headRig.name = "avatar-head-look-rig"' in SCENE
    assert "const lookPitch = clamp(-(Number(pitch) || 0), -0.62, 0.62);" in SCENE
    assert "avatar.userData.headRig.rotation.x = lookPitch;" in SCENE
    assert "avatar.userData.torso.rotation.x = lookPitch * 0.12;" in SCENE
    rotate = SCENE.split("function rotateCamera(deltaX, deltaY)", 1)[1].split(
        "\n  function ", 1
    )[0]
    assert "applyAvatarLookDirection(" in rotate
    assert "cameraYaw," in rotate


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
