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
    # The log pile sits at ~2.6; benches must clear it with a walkable ring.
    assert float(radius.group(1)) >= 6


def test_seat_position_is_read_from_the_live_world_matrix():
    # Absolute seat coordinates captured at build time drift whenever the
    # campfire is moved, which teleported sitters far away from the fire.
    assert "seat.userData.campfireBench = true;" in SCENE
    assert "seat.getWorldPosition(new THREE.Vector3())" in SCENE
    assert "seat.x += deltaX" not in SCENE


def test_sitting_holds_a_seated_pose_facing_the_fire():
    sit = SCENE.split("function sitOnCampfireBench", 1)[1].split(
        "function walkPlayer", 1
    )[0]
    assert "campfire.position.x" in sit and "campfire.position.z" in sit
    assert "activity: CAMPFIRE_SEATED_ACTIVITY" in sit
    # adhoc #303: the pitch is positive so the legs swing forward over the
    # front edge of the bench instead of out behind the sitter.
    assert "applySeatedLegPose(player);" in sit
    assert "const SEATED_LEG_PITCH = 1.45;" in SCENE
    # adhoc #323: the knee folds back by the same amount, so the shin drops
    # straight down off the front edge instead of the whole leg pivoting.
    assert "const SEATED_KNEE_PITCH = -SEATED_LEG_PITCH;" in SCENE
    # Sitters land by their hips, not by their feet: the pose used to leave the
    # avatar standing on the plank with its shoes still flat on it.
    assert "seatedAvatarY(" in sit
    # The pose is held every frame instead of applied once as a teleport.
    assert "applyBenchSeatPose();" in SCENE
    assert "function standUpFromBench()" in SCENE


def test_double_click_on_a_bench_sits_instead_of_dashing():
    dbl = SCENE.split("function handleDoubleClick", 1)[1].split(
        "function handlePointerCancel", 1
    )[0]
    assert "campfireBench" in dbl
    assert dbl.index("sitOnCampfireBench") < dbl.index("dashTarget = point")


def test_seats_are_benches_with_planks_and_legs():
    # adhoc #291: the ring is made of wooden benches (plank + end legs,
    # long axis tangent to the circle), not cylinder stools.
    ring = SCENE.split("function rebuildCampfireCircle", 1)[1].split(
        "setShadows(campfire);", 1
    )[0]
    assert "CylinderGeometry" not in ring
    assert "new THREE.BoxGeometry(1.6, 0.14, 0.6)" in ring
    assert "bench.rotation.y = -angle + Math.PI / 2;" in ring
    assert ring.count("leg") >= 2


def test_circle_always_keeps_an_open_bench_for_the_next_guest():
    # One bench beyond the registered membership stays blank; when a guest
    # joins and takes it, the roster grows and the rebuilt ring brings a
    # fresh open bench with it.
    # adhoc #303 adds one more open bench per guest already in the world.
    assert (
        "rebuildCampfireCircle(\n      Math.max(total, roster.length) "
        "+ guestSeats + 1,\n    )" in SCENE
    )


def test_circle_keeps_a_walk_in_gap_instead_of_closing_the_ring():
    # adhoc #430: a bench at every angle walled the fire in, so the ring keeps
    # a doorway-wide arc bench-free and spreads the seats over the rest.
    assert "const CAMPFIRE_ENTRANCE_WIDTH = 3.4;" in SCENE
    assert "const CAMPFIRE_ENTRANCE_MAX_ANGLE = Math.PI / 3;" in SCENE
    # The opening faces the town centre the arrivals walk from, derived from the
    # fire's own landmark so moving the campfire cannot leave it facing a wall.
    assert (
        "const CAMPFIRE_ENTRANCE_ANGLE = Math.atan2(\n"
        "    -campfire.position.z,\n"
        "    -campfire.position.x,\n"
        "  );" in SCENE
    )
    ring = SCENE.split("function rebuildCampfireCircle", 1)[1].split(
        "setShadows(ring);", 1
    )[0]
    # The arc is reserved by the radius too, so widening the ring for a new
    # member never closes the gap back up.
    assert (
        "(count * CAMPFIRE_SEAT_SPACING + CAMPFIRE_ENTRANCE_WIDTH) / (2 * Math.PI)"
        in ring
    )
    assert (
        "const entranceAngle = Math.min(\n"
        "      CAMPFIRE_ENTRANCE_MAX_ANGLE,\n"
        "      CAMPFIRE_ENTRANCE_WIDTH / radius,\n"
        "    );" in ring
    )
    assert "const seatStep = (Math.PI * 2 - entranceAngle) / count;" in ring
    assert (
        "CAMPFIRE_ENTRANCE_ANGLE + entranceAngle / 2 + (index + 0.5) * seatStep"
        in ring
    )
    # The old full-circle spacing is gone: it is what walled the fire in.
    assert "(index / count) * Math.PI * 2" not in ring


def test_sitters_face_the_flames_not_away_from_them():
    # Avatar fronts face local -Z, so the inward (fire-facing) heading is
    # atan2(x, z); the negated form turned sitters backwards (adhoc #291).
    assert "Math.atan2(seat.x, seat.z)" in SCENE
    assert "Math.atan2(offset.x, offset.z)" in SCENE
    assert "Math.atan2(-seat.x, -seat.z)" not in SCENE
    assert "Math.atan2(-offset.x, -offset.z)" not in SCENE


def test_faces_wear_status_emoji_over_a_unique_generated_default():
    # A chosen status emoji still takes over the face; otherwise each identity
    # gets a stable generated portrait instead of every account sharing 🙂.
    assert "function avatarFaceTexture" in SCENE
    assert "function proceduralAvatarFaceTexture" in SCENE
    assert "function syncAvatarFace" in SCENE
    assert "const drawn = statusEmoji" in SCENE
    assert "? avatarFaceTexture(THREE, statusEmoji)" in SCENE
    assert "proceduralAvatarFaceTexture(THREE, identityKey)" in SCENE
    # Status changes re-render the face card.
    assert "syncAvatarFace(THREE, avatar);" in SCENE
    assert "faceMesh.rotation.y = Math.PI;" in SCENE


def test_emoji_fills_the_face_with_no_gap_of_bare_head():
    # adhoc #316: the decal used to stop short of the glyph, leaving corners
    # and a rim of head-coloured sphere showing as a seam around the face.
    # The canvas is flooded underneath the glyph with its own rim colour, so
    # the disc carries that colour all the way out to its rim.
    assert 'context.globalCompositeOperation = "destination-over";' in SCENE
    assert "context.fillRect(0, 0, 128, 128);" in SCENE
    assert 'context.globalCompositeOperation = "source-over";' in SCENE


def test_the_face_is_a_flat_disc_on_a_flat_cut_head():
    # The face used to be a curved shell wrapped round the head, which warped
    # an uploaded avatar photo. The head's front is now sliced off flat and the
    # face is a plain circle lying on that cut.
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
    # No wrapped shell left behind.
    assert "AVATAR_FACE_PHI_START" not in SCENE
    assert "AVATAR_FACE_THETA_START" not in SCENE

    def number(name):
        found = re.search(rf"const {name} = ([\d.]+);", SCENE)
        assert found, f"{name} missing"
        return float(found.group(1))

    radius = number("AVATAR_HEAD_RADIUS")
    depth = number("AVATAR_FACE_DEPTH")
    # The cut has to leave a face worth looking at without shaving the head in
    # half, and the disc radius is exactly where the plane meets the sphere.
    assert 0.4 * radius < depth < 0.8 * radius
    assert (
        "Math.sqrt(\n"
        "  AVATAR_HEAD_RADIUS * AVATAR_HEAD_RADIUS - AVATAR_FACE_DEPTH * AVATAR_FACE_DEPTH,\n"
        ")" in SCENE
    )
    # Turned to look out of the cut, and held just off it so the coplanar disc
    # and cap cannot z-fight.
    assert (
        "faceMesh.position.z = -(AVATAR_FACE_DEPTH + AVATAR_FACE_LIFT);" in SCENE
    )
    assert 0 < number("AVATAR_FACE_LIFT") < 0.02
    # An unscaled head keeps the cut a true circle, so the photo stays square.
    assert "head.scale.y" not in SCENE
    assert "faceMesh.scale.y" not in SCENE


def test_head_colour_is_sampled_from_the_edge_of_the_emoji():
    # adhoc #316: the head takes the glyph's own rim colour so the sphere and
    # the decal wrapped over it are indistinguishable. The whole-glyph
    # sampler stays as the fallback for emoji with no single rim colour.
    assert "function edgeEmojiColor" in SCENE
    assert (
        "edgeEmojiColor(context, canvas) || dominantEmojiColor(context, canvas)"
        in SCENE
    )
    assert "skin.color.set(drawn.color);" in SCENE


def test_campfire_is_a_world_map_spot_on_the_fire_itself():
    # adhoc #321: the map carries a Campfire spot, and the fire stands on that
    # landmark's coordinates so the two can never drift apart.
    assert 'id: "campfire"' in DATA
    assert 'shortLabel: "Campfire"' in DATA
    block = DATA[DATA.index('id: "campfire"'):]
    block = block[: block.index("\n  },")]
    assert "position: [8, 0, 8]" in block
    assert 'action: "campfire"' in block
    assert "campfire.position.set(...landmarkById(\"campfire\").position);" in SCENE
    # The spot is a local seating feature, so it never wears a construction
    # marker waiting on a remote integration.
    assert '  "campfire",\n' in APP.split("LOCAL_LIVE_LANDMARKS", 1)[1]


def test_choosing_the_campfire_spot_seats_you_on_your_own_bench():
    scene = SCENE.split("function returnToCampfireBench", 1)[1].split(
        "\n  function ", 1
    )[0]
    # Your own named bench when the roster has seated you; otherwise the bench
    # the circle always keeps open at the end of the ring.
    assert "campfire.userData.seatByName" in scene
    assert "benches.length - 1" in scene
    assert "sitOnCampfireBench(seat)" in scene
    # Leaving the Office stays a deliberate walk through its door.
    assert 'officeSceneMode !== "town"' in scene
    assert "returnToCampfireBench,\n" in SCENE

    # The map button travels instead of opening a reading panel.
    click = APP.split('const id = landmarkButton.dataset.worldLandmark;', 1)[1]
    click = click[: click.index("this.openLandmark(id")]
    assert 'if (id === "campfire")' in click
    assert (
        "this.world?.returnToCampfireBench?.(this.identity?.name || \"\")"
        in APP
    )


def test_sitting_survives_the_campfire_landmark_proximity_label():
    # The bench is inside the campfire landmark's own label radius, so the
    # proximity relabel would otherwise overwrite the seated activity other
    # visitors render the pose from.
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
    # Landmark tree clusters stand at radius 6.4-8.5, which is where the bench
    # ring already is, so the campfire opts out of them.
    trees = SCENE.split("for (let treeIndex = 0", 1)[0]
    assert 'if (landmark.id === "campfire") return;' in trees


def test_remote_bench_sitters_render_seated():
    assert (
        'const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";' in SCENE
    )
    assert "avatar.userData.campfireSeated" in SCENE
    assert "if (seatedAtCampfire) {\n        applySeatedLegPose(avatar);" in SCENE
    # Remote sitters ride the plank by their hips too, so the whole circle
    # reads the same way the local player does.
    assert "avatar.userData.targetPosition.y = seatedAvatarY(" in SCENE
    assert "figure.position.y = seatedAvatarY(" in SCENE


def test_legs_hinge_at_the_hip_and_knee_with_the_shoe_on_the_shin():
    # adhoc #323: a single leg box pivoting about its own centre pushed half
    # the leg out behind the sitter and left the shoes behind on the plank.
    avatar = SCENE.split("function createAvatar", 1)[1].split(
        "function updateAvatarBadge", 1
    )[0]
    assert "const buildLeg = (side) => {" in avatar
    assert "hip.position.set(side * 0.3, AVATAR_HIP_Y, 0);" in avatar
    assert "knee.position.y = -AVATAR_LEG_SEGMENT;" in avatar
    # The shoe is a child of the knee, so feet travel with the leg they belong
    # to instead of staying planted under the body.
    assert "knee.add(foot);" in avatar
    assert "leftKnee: leftLegRig.knee," in avatar
    assert "rightKnee: rightLegRig.knee," in avatar
    # Straight-legged poses lock the knees back out.
    assert "function applyLegPitch" in SCENE
    assert "if (legs.leftKnee) legs.leftKnee.rotation.x = 0;" in SCENE


def test_bench_height_is_derived_from_the_seated_pose():
    # A plank the sitter's shins cannot reach the ground from reads as
    # hovering, so the bench is built up to the seated hip height instead.
    # Derived from the pose, so retuning the pitch cannot leave the bench at a
    # height the sitter's legs no longer match.
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
    # The legs grow with the plank so the bench still stands on the ground.
    assert "new THREE.BoxGeometry(0.16, CAMPFIRE_BENCH_LEG_HEIGHT, 0.5)" in ring
    assert "leg.position.set(end, CAMPFIRE_BENCH_LEG_HEIGHT / 2, 0);" in ring


def test_member_total_burns_as_a_number_in_the_fire():
    # adhoc #347: the registered-account total is shown as a number hanging in
    # the flames, painted from the same count that sizes the bench ring.
    assert "function campfireMemberCountTexture(THREE, total, newest)" in SCENE
    assert "campfire.add(memberCountSprite);" in SCENE
    lounge = SCENE.split("function updateMemberLounge", 1)[1]
    assert "setCampfireMemberCount(total, newestMemberName(members));" in lounge
    # Hidden until the roster lands, so the fire never shows a placeholder 0.
    assert "memberCountSprite.visible = false;" in SCENE
    # Repainted only when the count moves; the roster refresh is on a timer.
    assert "if (memberCountShown === key) return;" in SCENE


def test_fire_credits_the_newest_member_under_the_total():
    # adhoc #426: the count alone does not say who just arrived, so the most
    # recently joined account is named under the "MEMBERS" line.
    count_texture = SCENE.split(
        "function campfireMemberCountTexture", 1
    )[1].split("\nfunction ", 1)[0]
    assert 'context.fillText("NEWEST", 256, 210);' in count_texture
    assert "context.fillText(latest, 256, 240);" in count_texture
    # No newest member known yet (empty roster): the old two-line layout stays.
    assert "if (!latest) return;" in count_texture
    assert "context.fillText(digits, 256, latest ? 84 : 104);" in count_texture
    newest = SCENE.split("function newestMemberName(members) {", 1)[1].split(
        "\n}", 1
    )[0]
    # Ranked by the directory's joined timestamp, newest wins ties-free.
    assert "if (!name || created <= joinedAt) return;" in newest
    # A member seated from a presence frame has no joined date (0) yet, so it
    # must not be ranked as the newest — or as the oldest.
    assert "const created = Number(member?.createdAt) || 0;" in newest
    # The repaint key carries the name, so a new arrival repaints at the same
    # count when the directory backfills their joined date.
    assert "const key = `${count}|${latest}`;" in SCENE


def test_member_total_tracks_arrivals_without_waiting_for_the_poll():
    # adhoc #393: the count is repainted from every presence frame, so a new
    # account is counted the moment it joins instead of at the next 60s tick.
    note = APP.split("noteDirectoryMembers(names) {", 1)[1].split("\n  }", 1)[0]
    assert "void this.refreshMemberDirectory(true, missing);" in note
    refresh = APP.split(
        "async refreshMemberDirectory(force = false, probed = []) {", 1)[1]
    refresh = refresh.split("\n  }", 1)[0]
    # The forced fetch bypasses the idle throttle and the client-side copy,
    # but never fires inside the endpoint's own edge-cache TTL.
    assert "? USERS_DIRECTORY_TTL_MS" in refresh
    assert ": WORLD_MEMBER_DIRECTORY_POLL_MS;" in refresh
    assert "maxAge: force ? 0 : WORLD_MEMBER_DIRECTORY_POLL_MS," in refresh
    assert "const USERS_DIRECTORY_TTL_MS = 30 * 1000;" in APP
    # A tab coming back from hidden is up to a full tick behind.
    visibility = APP.split("handleVisibility = () => {", 1)[1].split("\n  };", 1)[0]
    assert "void this.refreshMemberDirectory();" in visibility


def test_only_users_table_accounts_are_counted_at_the_fire():
    # adhoc #427: a presence frame is not a membership. Guests, bots and
    # private profiles broadcast names too, so a name is only ever a reason to
    # re-read /api/accounts/users — the count and the benches come from that
    # snapshot alone, and a name it does not list is never seated locally.
    note = APP.split("noteDirectoryMembers(names) {", 1)[1].split("\n  }", 1)[0]
    assert "this.memberDirectory = [" not in note
    assert "this.unlistedDirectoryNames.has(key)" in note
    refresh = APP.split(
        "async refreshMemberDirectory(force = false, probed = []) {", 1)[1]
    refresh = refresh.split("\n  }", 1)[0]
    assert "this.memberDirectory = directory;" in refresh
    # A name the directory came back without must not force a fetch again.
    assert "if (!listed.has(key)) this.unlistedDirectoryNames.add(key);" in refresh
    # Everyone present without a users-table row takes a spare seat instead of
    # a named bench, so the ring still fits the crowd.
    lounge = APP.split("syncMemberLounge() {", 1)[1].split("\n  }", 1)[0]
    assert "if (!listed.has(key)) guests += 1;" in lounge
    assert "this.memberDirectory.length," in lounge


def test_seated_members_get_a_chat_bubble_over_their_bench():
    # adhoc #363: a member talking in the website chat is usually not a live
    # world peer — their avatar is the figure sitting on their own campfire
    # bench, so the bubble floats over that seated figure.
    assert "function showMemberChatBubble(name, text)" in SCENE
    bubble = SCENE.split("function showMemberChatBubble", 1)[1].split(
        "\n  }", 1
    )[0]
    assert 'loungeMembers.has(`member:${wanted}`)' in bubble
    # Truncated senders (16-character room names) still find their bench.
    assert "if (wanted.length < 16) return false;" in bubble
    # The bubble resolver reaches seated figures, not just live peers.
    assert (
        'remotePlayers.get(String(peerId || "")) ||\n'
        '            loungeMembers.get(String(peerId || ""))' in SCENE
    )
    assert "showMemberChatBubble," in SCENE
    # Only after no live peer matched, so a member walking the world keeps
    # the bubble over their own avatar.
    chat = APP.split("handleWorldChatMessage = (event) => {", 1)[1].split(
        "\n  };", 1
    )[0]
    assert chat.index("this.world?.showChatBubble?.(id, text);") < chat.index(
        "this.world?.showMemberChatBubble?.(sender, text);"
    )
