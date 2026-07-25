#!/usr/bin/env python3
"""Scene contracts for the campfire benches and sitting on them."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(encoding="utf-8")


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
    assert "const SEATED_LEG_PITCH = 1.3;" in SCENE
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


def test_sitters_face_the_flames_not_away_from_them():
    # Avatar fronts face local -Z, so the inward (fire-facing) heading is
    # atan2(x, z); the negated form turned sitters backwards (adhoc #291).
    assert "Math.atan2(seat.x, seat.z)" in SCENE
    assert "Math.atan2(offset.x, offset.z)" in SCENE
    assert "Math.atan2(-seat.x, -seat.z)" not in SCENE
    assert "Math.atan2(-offset.x, -offset.z)" not in SCENE


def test_faces_wear_the_last_used_emoji_with_a_smile_default():
    # adhoc #291: the head wears the last world-status emoji the visitor set;
    # with no stored emoji the face defaults to a smile.
    assert 'const AVATAR_DEFAULT_FACE_EMOJI = "🙂";' in SCENE
    assert "function avatarFaceTexture" in SCENE
    assert "function syncAvatarFace" in SCENE
    assert (
        "avatar.userData.statusEmoji || AVATAR_DEFAULT_FACE_EMOJI" in SCENE
    )
    # Status changes re-render the face card.
    assert "syncAvatarFace(THREE, avatar);" in SCENE
    assert "faceMesh.rotation.y = Math.PI;" in SCENE


def test_emoji_wraps_the_head_with_no_gap_of_bare_sphere():
    # adhoc #316: the decal used to stop short of the glyph, leaving corners
    # and a rim of head-coloured sphere showing as a seam around the face.
    # The canvas is now flooded underneath the glyph with its own rim colour,
    # and the shell it is mapped onto wraps wider than the glyph itself.
    assert 'context.globalCompositeOperation = "destination-over";' in SCENE
    assert "context.fillRect(0, 0, 128, 128);" in SCENE
    assert 'context.globalCompositeOperation = "source-over";' in SCENE

    def span(name):
        found = re.search(
            rf"const AVATAR_FACE_{name} = Math\.PI \* ([\d.]+);", SCENE
        )
        assert found, f"AVATAR_FACE_{name} missing"
        return float(found.group(1))

    phi = span("PHI_LENGTH")
    theta = span("THETA_LENGTH")
    # Wider than the pre-#316 patch (0.84 phi by 0.68 theta) so the padding
    # reaches past the face, and still centred on the avatar's front.
    assert phi > 0.84 and theta > 0.68
    assert abs(span("PHI_START") * 2 + phi - 1) < 1e-9
    assert abs(span("THETA_START") * 2 + theta - 1) < 1e-9
    for name in ("PHI_START", "PHI_LENGTH", "THETA_START", "THETA_LENGTH"):
        assert f"AVATAR_FACE_{name},\n" in SCENE, f"{name} unused by the face"


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


def test_remote_bench_sitters_render_seated():
    assert (
        'const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";' in SCENE
    )
    assert "avatar.userData.campfireSeated" in SCENE
    assert "seatedAtCampfire\n        ? SEATED_LEG_PITCH" in SCENE
