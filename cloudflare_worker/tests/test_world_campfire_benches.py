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
    assert "leftLeg.rotation.x = -1.3" in sit
    assert "rightLeg.rotation.x = -1.3" in sit
    # The pose is held every frame instead of applied once as a teleport.
    assert "applyBenchSeatPose();" in SCENE
    assert "function standUpFromBench()" in SCENE


def test_double_click_on_a_bench_sits_instead_of_dashing():
    dbl = SCENE.split("function handleDoubleClick", 1)[1].split(
        "function handlePointerCancel", 1
    )[0]
    assert "campfireBench" in dbl
    assert dbl.index("sitOnCampfireBench") < dbl.index("dashTarget = point")


def test_remote_bench_sitters_render_seated():
    assert (
        'const CAMPFIRE_SEATED_ACTIVITY = "sitting beside the campfire";' in SCENE
    )
    assert "avatar.userData.campfireSeated" in SCENE
    assert "seatedAtCampfire ? -1.3" in SCENE
