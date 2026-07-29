#!/usr/bin/env python3
"""Contracts for automatic World node placement and the pool rim title.

Every live mirror-node cabinet is placed around the reward pool as part of the
existing catalog update, with stable slots that do not jitter when health data
reorders the payload. The reward pool's own title is painted around the rim, so
the repeat count and type size must leave a gap between wraps.
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


def _reward_circle_slots(count, centre_x=0.0, centre_z=0.0, options=None):
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
                f"rewardCircleSlots({centre_x}, {centre_z}, {count}, "
                f"{json.dumps(options or {})})));"
            ),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def test_manual_organize_nodes_control_is_removed():
    assert "data-world-organize-nodes" not in APP
    assert "Organize nodes" not in APP
    assert "toggleOrganizedNodes" not in APP
    assert "syncOrganizeNodesButton" not in APP


def test_manual_node_layout_mode_and_scene_controls_are_removed():
    assert "organizeNetworkNodes" not in SCENE
    assert "getNodeLayoutState" not in SCENE
    assert "nodeLayoutMode" not in SCENE
    assert "lastNetworkNodes" not in SCENE


def test_live_nodes_are_automatically_ringed_and_face_the_reward_pool():
    assert 'const fountain = landmarkObjects.get("fountain")' in SCENE
    assert "const circleSlots = rewardCircleSlots(" in SCENE
    assert "nodeSlotAssignments" in SCENE
    assert "const slot = circleSlots[nodeSlotAssignments.get(id)]" in SCENE
    assert "cabinet.position.set(slot.x, 0.38, slot.z)" in SCENE
    assert "routingX - slot.x" in SCENE
    assert "routingZ - slot.z" in SCENE
    assert "serverSlots" not in SCENE


def test_node_slots_are_stable_and_not_overridden_by_saved_layout():
    block = _function_source("updateNetworkNodes")
    assert ".sort((left, right) => {" in block
    assert "left.name.toLowerCase()" in block
    assert "right.name.toLowerCase()" in block
    assert ".filter(" in block
    assert ".slice(0, 64)" in block
    assert "nodeSlotAssignments" in block
    assert "networkNodeSnapshot" in block
    assert 'worldLayoutId("node-"' not in block
    assert "registerMovableObject(" not in block
    # Automatic placement piggybacks on the catalog update: it does not add
    # another timer, request, or listener.
    assert "fetch(" not in block
    assert "setInterval(" not in block
    assert "addEventListener(" not in block


def test_reward_circle_slots_clear_the_pool_and_never_collide():
    for count in (1, 3, 8, 17, 64):
        slots = _reward_circle_slots(count)
        assert len(slots) == count
        for slot in slots:
            radius = math.hypot(slot["x"], slot["z"])
            # Outside the 5.25 pool rim and the 6.4–8.5 tree circle.
            assert radius >= 10.7
            # The member fire now occupies its own south island, safely clear
            # of every central service-yard slot.
            assert math.hypot(slot["x"], slot["z"] - 130) >= 8.2
        for index, left in enumerate(slots):
            for right in slots[index + 1:]:
                assert math.dist(
                    (left["x"], left["z"]), (right["x"], right["z"])
                ) >= 3.6, count


def test_reward_circle_slots_are_deterministic():
    assert _reward_circle_slots(64) == _reward_circle_slots(64)


def test_reward_circle_slot_prefix_does_not_shift_when_membership_changes():
    all_slots = _reward_circle_slots(64)
    for count in (1, 2, 3, 8, 17, 32):
        assert _reward_circle_slots(count) == all_slots[:count]


def test_reward_circle_slots_honor_live_scene_keepouts():
    slots = _reward_circle_slots(
        32,
        options={
            "campfirePosition": [30, 0, 0],
            "campfireClearance": 11,
            "circleKeepouts": [{"x": -14, "z": 0, "radius": 7}],
            "rectangleKeepouts": [
                {
                    "minX": -5,
                    "maxX": 5,
                    "minZ": 8,
                    "maxZ": 22,
                    "padding": 2,
                }
            ],
        },
    )
    assert len(slots) == 32
    for slot in slots:
        assert math.hypot(slot["x"] - 30, slot["z"]) >= 11
        assert math.hypot(slot["x"] + 14, slot["z"]) >= 7
        assert not (-7 <= slot["x"] <= 7 and 6 <= slot["z"] <= 24)


def test_live_node_layout_reanchors_after_pool_or_obstacle_changes():
    assert "function relayoutNetworkNodes()" in SCENE
    apply_start = SCENE.index("function applyWorldLayout(")
    apply_end = SCENE.index("\n  }\n", apply_start)
    assert "relayoutNetworkNodes();" in SCENE[apply_start:apply_end]
    member_start = SCENE.index("function updateMemberLounge(")
    member_end = SCENE.index("\n  }\n", member_start)
    assert "previousSeatRadius" in SCENE[member_start:member_end]
    assert "relayoutNetworkNodes();" in SCENE[member_start:member_end]


def test_reward_circle_slots_follow_a_relocated_pool():
    slots = _reward_circle_slots(
        6,
        centre_x=12.0,
        centre_z=-7.0,
        options={"campfirePosition": [1000, 0, 1000]},
    )
    assert len(slots) == 6
    for slot in slots:
        assert math.hypot(slot["x"] - 12.0, slot["z"] + 7.0) >= 10.7
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
