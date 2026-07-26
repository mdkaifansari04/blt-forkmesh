#!/usr/bin/env python3
"""Contracts for the World "Organize nodes" control and the pool rim title.

The HUD button rings every live mirror-node cabinet around the reward pool and
puts the fleet back in the server yard on a second press. The reward pool's own
title is painted around the rim, so the repeat count and the type size have to
leave a real gap between wraps instead of overlapping into one smear.
"""

import json
import math
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")


def _function_source(name):
    start = SCENE.index(f"function {name}(")
    end = SCENE.index("\n  }\n", start)
    return SCENE[start:end] + "\n  }\n"


def _reward_circle_slots(count, centre_x=0.0, centre_z=0.0):
    # The real landmark table decides the campfire keep-out, so the slot math
    # is exercised against the shipped world data rather than a stand-in.
    data_uri = json.dumps((WORLD / "world-data.js").as_uri())
    world_radius = SCENE[SCENE.index("const WORLD_RADIUS = "):]
    world_radius = world_radius[: world_radius.index(";") + 1]
    source = _function_source("rewardCircleSlots")
    completed = subprocess.run(
        [
            "node",
            "--input-type=module",
            "-e",
            f"import {{ landmarkById }} from {data_uri};\n"
            + world_radius
            + "\n"
            + source
            + (
                "process.stdout.write(JSON.stringify("
                f"rewardCircleSlots({centre_x}, {centre_z}, {count})));"
            ),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def test_hud_exposes_an_organize_nodes_button():
    assert "data-world-organize-nodes" in APP
    assert ">Organize nodes<" in APP
    assert 'aria-pressed="false"' in APP
    assert 'this.toggleOrganizedNodes();' in APP
    assert "syncOrganizeNodesButton(state)" in APP
    assert "organizeNetworkNodes()" in APP
    # The label flips so a second press is legibly the way back.
    assert 'organized ? "Node yard" : "Organize nodes"' in APP


def test_scene_exports_the_node_layout_controls():
    assert "organizeNetworkNodes,\n    getNodeLayoutState," in SCENE
    assert 'nodeLayoutMode = "yard"' in SCENE
    assert 'nodeLayoutMode = next ? "reward-circle" : "yard"' in SCENE
    # Toggling re-places the fleet from the retained list instead of waiting
    # for the next network poll.
    assert "lastNetworkNodes = Array.isArray(nodes) ? nodes : []" in SCENE
    assert "updateNetworkNodes(lastNetworkNodes)" in SCENE


def test_organized_nodes_ring_the_reward_pool_and_face_it():
    assert 'const fountain = landmarkObjects.get("fountain")' in SCENE
    assert "rewardCircleSlots(routingX, routingZ, usableNodes.length)" in SCENE
    assert "const slot = circleSlots ? circleSlots[index] : serverSlots[index]" in SCENE
    # An explicit viewer request outranks an administrator-locked placement for
    # as long as the ring is switched on.
    assert "if (rewardCircle) placeInSlot();" in SCENE


def test_reward_circle_slots_clear_the_pool_and_never_collide():
    for count in (1, 3, 8, 17, 64):
        slots = _reward_circle_slots(count)
        assert len(slots) == count
        for slot in slots:
            radius = math.hypot(slot["x"], slot["z"])
            # Outside the 5.25 pool rim and the 6.4–8.5 tree circle.
            assert radius >= 9.5
        for index, left in enumerate(slots):
            for right in slots[index + 1:]:
                assert math.dist(
                    (left["x"], left["z"]), (right["x"], right["z"])
                ) >= 2.9, count


def test_reward_circle_slots_follow_a_relocated_pool():
    slots = _reward_circle_slots(6, centre_x=12.0, centre_z=-7.0)
    assert len(slots) == 6
    for slot in slots:
        assert math.hypot(slot["x"] - 12.0, slot["z"] + 7.0) >= 9.5
    # Evenly spread around the relocated pool, not bunched on one side.
    assert abs(sum(slot["x"] for slot in slots) / 6 - 12.0) < 1.5
    assert abs(sum(slot["z"] for slot in slots) / 6 + 7.0) < 1.5


def test_reward_pool_rim_title_is_measured_so_wraps_do_not_overlap():
    start = SCENE.index("function rewardPoolRimTexture(")
    block = SCENE[start: SCENE.index("\n}\n", start)]
    assert "const repeats = 3;" in block
    assert 'const title = "GLOBAL REWARD POOL";' in block
    # The type shrinks to the measured slot rather than trusting a fixed size.
    assert "const available = slot * 0.72;" in block
    assert "context.measureText(title).width > available" in block
    assert "context.fillText(title, centre, height / 2 + 4);" in block
