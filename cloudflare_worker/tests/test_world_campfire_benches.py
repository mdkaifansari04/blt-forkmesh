#!/usr/bin/env python3
"""Scene contracts for the campfire benches and sitting on them."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
APP = (WORLD / "world.js").read_text(encoding="utf-8")
DATA = (WORLD / "world-data.js").read_text(encoding="utf-8")


def test_benches_ring_the_pit_from_a_distance():
    radius = re.search(r"const CAMPFIRE_BENCH_RADIUS = ([\d.]+);", SCENE)
    assert radius, "campfire bench radius constant missing"

    assert float(radius.group(1)) >= 6


def test_seat_position_is_read_from_the_live_world_matrix():


    assert "seat.userData.campfireBench = true;" in SCENE
    assert "seat.getWorldPosition(new THREE.Vector3())" in SCENE
    assert "seat.x += deltaX" not in SCENE


def test_sitting_holds_a_seated_pose_facing_the_fire():
    sit = SCENE.split("function sitOnCampfireBench", 1)[1].split(
        "function walkPlayer", 1
    )[0]
    assert "campfire.position.x" in sit and "campfire.position.z" in sit
    assert "activity: CAMPFIRE_SEATED_ACTIVITY" in sit


    assert "applySeatedLegPose(player);" in sit
    assert "const SEATED_LEG_PITCH = 1.45;" in SCENE


    assert "const SEATED_KNEE_PITCH = -SEATED_LEG_PITCH;" in SCENE


    assert "seatedAvatarY(" in sit

    assert "applyBenchSeatPose();" in SCENE
    assert "function standUpFromBench()" in SCENE


def test_double_click_on_a_bench_sits_instead_of_dashing():
    dbl = SCENE.split("function handleDoubleClick", 1)[1].split(
        "function handlePointerCancel", 1
    )[0]
    assert "campfireBench" in dbl
    assert dbl.index("sitOnCampfireBench") < dbl.index("dashTarget = point")


def test_seats_are_benches_with_planks_and_legs():


    ring = SCENE.split("function rebuildCampfireCircle", 1)[1].split(
        "setShadows(campfire);", 1
    )[0]
    assert "CylinderGeometry" not in ring
    assert "new THREE.BoxGeometry(1.6, 0.14, 0.6)" in ring
    assert "bench.rotation.y = -angle + Math.PI / 2;" in ring
    assert ring.count("leg") >= 2
    assert "new THREE.InstancedMesh(" in ring
    assert 'seatInstances.name = "campfire-member-bench-seats"' in ring
    assert 'legInstances.name = "campfire-member-bench-legs"' in ring
    assert 'labelMesh.name = "campfire-member-bench-label-atlas"' in ring
    assert "seat.userData.raycastProxy = true;" in ring


def test_circle_always_keeps_an_open_bench_for_the_next_guest():




    assert (
        "rebuildCampfireCircle(\n      Math.max(total, roster.length) "
        "+ guestSeats + 1,\n    )" in SCENE
    )


def test_circle_keeps_a_walk_in_gap_instead_of_closing_the_ring():


    assert "const CAMPFIRE_ENTRANCE_WIDTH = 3.4;" in SCENE
    assert "const CAMPFIRE_ENTRANCE_MAX_ANGLE = Math.PI / 3;" in SCENE


    assert (
        "const CAMPFIRE_ENTRANCE_ANGLE = Math.atan2(\n"
        "    -campfire.position.z,\n"
        "    -campfire.position.x,\n"
        "  );" in SCENE
    )
    ring = SCENE.split("function rebuildCampfireCircle", 1)[1].split(
        "setShadows(campfire);", 1
    )[0]


    assert "const CAMPFIRE_MEMBERS_PER_ROW = 25;" in SCENE
    assert "const CAMPFIRE_ROW_SPACING = 2.8;" in SCENE
    assert "const row = Math.floor(index / CAMPFIRE_MEMBERS_PER_ROW);" in ring
    assert (
        "const entranceAngle = Math.min(\n"
        "        CAMPFIRE_ENTRANCE_MAX_ANGLE,\n"
        "        CAMPFIRE_ENTRANCE_WIDTH / radius,\n"
        "      );" in ring
    )
    assert "const seatStep = (Math.PI * 2 - entranceAngle) / seatsInRow;" in ring
    assert "(positionInRow + 0.5) * seatStep" in ring

    assert "(index / count) * Math.PI * 2" not in ring


def test_sitters_face_the_flames_not_away_from_them():


    assert "Math.atan2(seat.x, seat.z)" in SCENE
    assert "Math.atan2(offset.x, offset.z)" in SCENE
    assert "Math.atan2(-seat.x, -seat.z)" not in SCENE
    assert "Math.atan2(-offset.x, -offset.z)" not in SCENE


def test_faces_wear_status_emoji_over_a_unique_generated_default():


    assert "function avatarFaceTexture" in SCENE
    assert "function proceduralAvatarFaceTexture" in SCENE
    assert "function syncAvatarFace" in SCENE
    assert "const drawn = statusEmoji" in SCENE
    assert "? avatarFaceTexture(THREE, statusEmoji)" in SCENE
    assert "proceduralAvatarFaceTexture(THREE, identityKey)" in SCENE

    assert "syncAvatarFace(THREE, avatar);" in SCENE
    assert "faceMesh.rotation.y = Math.PI;" in SCENE


def test_emoji_fills_the_face_with_no_gap_of_bare_head():




    assert 'context.globalCompositeOperation = "destination-over";' in SCENE
    assert "context.fillRect(0, 0, 128, 128);" in SCENE
    assert 'context.globalCompositeOperation = "source-over";' in SCENE


def test_the_face_is_a_flat_disc_on_a_flat_cut_head():



    assert "function flattenSphereFront" in SCENE
    assert "if (position.getZ(i) < -depth) position.setZ(i, -depth);" in SCENE
    assert "geometry.computeVertexNormals();" in SCENE
    assert (
        "flattenSphereFront(\n"
        "      new THREE.SphereGeometry(AVATAR_HEAD_RADIUS, 32, 24),\n"
        "      AVATAR_FACE_DEPTH,\n"
        "    )" in SCENE
    )
    assert "new THREE.CircleGeometry(AVATAR_FACE_RADIUS, 48)" in SCENE

    assert "AVATAR_FACE_PHI_START" not in SCENE
    assert "AVATAR_FACE_THETA_START" not in SCENE

    def number(name):
        found = re.search(rf"const {name} = ([\d.]+);", SCENE)
        assert found, f"{name} missing"
        return float(found.group(1))

    radius = number("AVATAR_HEAD_RADIUS")
    depth = number("AVATAR_FACE_DEPTH")


    assert 0 < depth < 0.25 * radius
    assert (
        "Math.sqrt(\n"
        "  AVATAR_HEAD_RADIUS * AVATAR_HEAD_RADIUS - AVATAR_FACE_DEPTH * AVATAR_FACE_DEPTH,\n"
        ")" in SCENE
    )


    assert (
        "faceMesh.position.z = -(AVATAR_FACE_DEPTH + AVATAR_FACE_LIFT);" in SCENE
    )
    assert 0 < number("AVATAR_FACE_LIFT") < 0.02

    assert "head.scale.y" not in SCENE
    assert "faceMesh.scale.y" not in SCENE


def test_head_colour_is_sampled_from_the_edge_of_the_emoji():



    assert "function edgeEmojiColor" in SCENE
    assert (
        "edgeEmojiColor(context, canvas) || dominantEmojiColor(context, canvas)"
        in SCENE
    )
    assert "skin.color.set(drawn.color);" in SCENE


def test_campfire_is_a_world_map_spot_on_the_fire_itself():


    assert 'id: "campfire"' in DATA
    assert 'shortLabel: "Campfire"' in DATA
    block = DATA[DATA.index('id: "campfire"'):]
    block = block[: block.index("\n  },")]
    assert "position: [0, 0, 130]" in block
    assert 'action: "campfire"' in block
    assert "campfire.position.set(...landmarkById(\"campfire\").position);" in SCENE


    assert '  "campfire",\n' in APP.split("LOCAL_LIVE_LANDMARKS", 1)[1]


def test_choosing_the_campfire_spot_seats_you_on_your_own_bench():
    scene = SCENE.split("function returnToCampfireBench", 1)[1].split(
        "\n  function ", 1
    )[0]


    assert "campfire.userData.seatByName" in scene
    assert "benches.length - 1" in scene
    assert "sitOnCampfireBench(seat)" in scene

    assert 'officeSceneMode !== "town"' in scene
    assert "returnToCampfireBench,\n" in SCENE


    click = APP.split('const id = landmarkButton.dataset.worldLandmark;', 1)[1]
    click = click[: click.index("this.openLandmark(id")]
    assert 'if (id === "campfire")' in click
    assert (
        "this.world?.returnToCampfireBench?.(this.identity?.name || \"\")"
        in APP
    )


def test_sitting_survives_the_campfire_landmark_proximity_label():



    assert "export const CAMPFIRE_SEATED_ACTIVITY" in SCENE
    assert (
        "  CAMPFIRE_SEATED_ACTIVITY,\n  SWING_RIDING_ACTIVITY,\n"
        "  createWorldScene,\n" in APP
    )
    location = APP.split("  updateLocation(label, id) {", 1)[1].split(
        "\n  updateRegion(", 1
    )[0]
    assert "this.lastMovement.activity !== CAMPFIRE_SEATED_ACTIVITY" in location


def test_campfire_clearing_keeps_its_landmark_tree_ring_out():


    trees = SCENE.split("for (let treeIndex = 0", 1)[0]
    assert 'if (["campfire", "office"].includes(landmark.id)) return;' in trees


def test_remote_bench_sitters_render_seated():
    assert (
        'const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";' in SCENE
    )
    assert "avatar.userData.campfireSeated" in SCENE
    assert "if (seatedAtCampfire) {\n        applySeatedLegPose(avatar);" in SCENE


    assert "avatar.userData.targetPosition.y = seatedAvatarY(" in SCENE
    assert "figure.position.y = seatedAvatarY(" in SCENE


def test_legs_hinge_at_the_hip_and_knee_with_the_shoe_on_the_shin():


    avatar = SCENE.split("function createAvatar", 1)[1].split(
        "function updateAvatarBadge", 1
    )[0]
    assert "const buildLeg = (side) => {" in avatar
    assert "hip.position.set(side * 0.3, AVATAR_HIP_Y, 0);" in avatar
    assert "knee.position.y = -AVATAR_LEG_SEGMENT;" in avatar


    assert "knee.add(foot);" in avatar
    assert "leftKnee: leftLegRig.knee," in avatar
    assert "rightKnee: rightLegRig.knee," in avatar

    assert "function applyLegPitch" in SCENE
    assert "if (legs.leftKnee) legs.leftKnee.rotation.x = 0;" in SCENE


def test_bench_height_is_derived_from_the_seated_pose():




    assert (
        "const SEATED_SEAT_TO_SOLE =\n"
        "  AVATAR_LEG_SEGMENT * Math.cos(SEATED_LEG_PITCH) +\n"
        "  AVATAR_KNEE_TO_SOLE -\n"
        "  SEATED_HIP_ABOVE_SEAT;" in SCENE
    )
    assert (
        "const CAMPFIRE_SEAT_TOP_Y = WORLD_WALKING_PLANE_Y + SEATED_SEAT_TO_SOLE;"
        in SCENE
    )
    ring = SCENE.split("function rebuildCampfireCircle", 1)[1].split(
        "setShadows(campfire);", 1
    )[0]
    assert "seat.position.y = CAMPFIRE_SEAT_Y;" in ring

    assert "const legGeometry = new THREE.BoxGeometry(" in ring
    assert "CAMPFIRE_BENCH_LEG_HEIGHT," in ring
    assert "CAMPFIRE_BENCH_LEG_HEIGHT / 2" in ring
    assert "legInstances.setMatrixAt(" in ring


def test_campfire_fills_every_non_walking_member_bench_with_a_figure():
    assert "CAMPFIRE_DETAILED_MEMBER_LIMIT" not in SCENE
    assert "const seatedMemberIds = new Set(" in SCENE
    assert ".filter((member) => member?.away !== true)" in SCENE
    assert "const represented = seatedMemberIds.has(id);" in SCENE
    assert "campfire.userData.detailedMemberFigures" in SCENE
    assert "campfire-instanced-member-sitters" not in SCENE
    assert "updateCompactCampfireSitters" not in SCENE
    assert "figure = createAvatar(" in SCENE
    assert "{ remote: true, scale: 0.88 }" in SCENE
    assert "campfire.userData.seatedMemberFigures = seen.size;" in SCENE
    assert 'labelState = member.away === true' in SCENE
    assert '"ROSTER BENCH"' in SCENE
    assert "showAtBench(seatByName.get(wanted))" in SCENE
    assert "setCampfireSeatOrgTeamAction(" in SCENE
    assert "polygonOffsetFactor: -1" in SCENE
    assert "repaintCampfireSeatLabels();" in SCENE
    assert "labelMesh.material.map = texture" in SCENE


def test_member_total_changes_fire_and_the_high_member_count():
    assert "function campfireMemberCountTexture(THREE, total, newest = \"\")" in SCENE
    assert 'memberCountSprite.name = "campfire-member-count-high"' in SCENE
    assert "memberCountSprite.material.map = campfireMemberCountTexture(" in SCENE
    assert "      latest," in SCENE
    lounge = SCENE.split("function updateMemberLounge", 1)[1]
    assert "setCampfireMemberCount(total, newestMemberName(members));" in lounge
    assert "rebuildCampfireMemberLogs(count)" in SCENE
    assert "if (memberCountShown === key) return;" in SCENE


def test_fire_tracks_newest_member_without_rendering_a_center_label():
    newest = SCENE.split("function newestMemberName(members) {", 1)[1].split(
        "\n}", 1
    )[0]

    assert "if (!name || created <= joinedAt) return;" in newest


    assert "const created = Number(member?.createdAt) || 0;" in newest


    assert "const key = `${count}|${latest}`;" in SCENE


def test_member_total_tracks_arrivals_without_waiting_for_the_poll():


    note = APP.split("noteDirectoryMembers(names) {", 1)[1].split("\n  }", 1)[0]
    assert "void this.refreshMemberDirectory(true, missing);" in note
    refresh = APP.split(
        "async refreshMemberDirectory(force = false, probed = []) {", 1)[1]
    refresh = refresh.split("\n  }", 1)[0]


    assert "? USERS_DIRECTORY_TTL_MS" in refresh
    assert ": WORLD_MEMBER_DIRECTORY_POLL_MS;" in refresh
    assert "maxAge: force ? 0 : WORLD_MEMBER_DIRECTORY_POLL_MS," in refresh
    assert "const USERS_DIRECTORY_TTL_MS = 30 * 1000;" in APP

    visibility = APP.split("handleVisibility = () => {", 1)[1].split("\n  };", 1)[0]
    assert "void this.refreshMemberDirectory();" in visibility


def test_only_users_table_accounts_are_counted_at_the_fire():




    note = APP.split("noteDirectoryMembers(names) {", 1)[1].split("\n  }", 1)[0]
    assert "this.memberDirectory = [" not in note
    assert "this.unlistedDirectoryNames.has(key)" in note
    refresh = APP.split(
        "async refreshMemberDirectory(force = false, probed = []) {", 1)[1]
    refresh = refresh.split("\n  }", 1)[0]
    assert "this.memberDirectory = directory;" in refresh

    assert "if (!listed.has(key)) this.unlistedDirectoryNames.add(key);" in refresh


    lounge = APP.split("syncMemberLounge() {", 1)[1].split("\n  }", 1)[0]
    assert "if (!listed.has(key)) guests += 1;" in lounge
    assert "this.memberDirectory.length," in lounge


def test_seated_members_get_a_chat_bubble_over_their_bench():



    assert "function showMemberChatBubble(name, text)" in SCENE
    bubble = SCENE.split("function showMemberChatBubble", 1)[1].split(
        "\n  }", 1
    )[0]
    assert 'loungeMembers.has(`member:${wanted}`)' in bubble

    assert "if (wanted.length < 16) return false;" in bubble

    assert (
        'remotePlayers.get(String(peerId || "")) ||\n'
        '            loungeMembers.get(String(peerId || ""))' in SCENE
    )
    assert "showMemberChatBubble," in SCENE




    chat = APP.split("handleWorldChatMessage = (event) => {", 1)[1].split(
        "\n  };", 1
    )[0]
    assert "if (data.self === true)" in chat
    assert "this.world?.showChatBubble?.(this.identity?.id, text, true);" in chat
    assert "this.world?.showMemberChatBubble?.(sender, text);" not in chat
