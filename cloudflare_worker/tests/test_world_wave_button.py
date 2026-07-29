#!/usr/bin/env python3
"""HUD wave button and the avatar arm pose it plays (issue #532)."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
APP = (WORLD / "world.js").read_text(encoding="utf-8")
STYLES = (WORLD / "world.css").read_text(encoding="utf-8")


def test_a_small_wave_button_sits_beside_the_bottom_chat_control():
    button = APP.split("data-world-wave")
    assert len(button) == 3, "wave button and its click handler are both needed"
    markup = button[0].rsplit("<button", 1)[1] + button[1].split("</button>", 1)[0]
    assert "world-chat-wave-button" in markup
    assert 'aria-label="Wave your avatar\'s arm"' in markup
    assert APP.index("data-world-wave") > APP.index("data-world-diagnostics")
    assert APP.index("data-world-wave") < APP.index("data-world-chat-terminal")
    top_tools = APP.split('aria-label="World tools"', 1)[1].split("</nav>", 1)[0]
    assert "data-world-wave" not in top_tools


def test_the_phone_launcher_reveals_controls_without_overflowing_the_row():
    # Touch starts with the circular avatar launcher only.  The controls fan
    # out in the same fixed-size row after a tap, where the row can scroll
    # rather than pushing the avatar past the viewport edge.
    compact = STYLES.split("@media (max-width: 720px) {", 1)
    assert len(compact) == 2, "compact top-bar media query is missing"
    compact_block = compact[1].split("\n}\n", 1)[0]
    assert "width: 0;" in compact_block
    assert ".world-hud[data-hud-expanded=\"true\"] .world-top-actions" in compact_block
    assert "overflow-x: auto;" in compact_block
    assert ".world-hud[data-hud-expanded=\"true\"] .world-top-link" in compact_block


def test_clicking_the_button_waves_locally_and_broadcasts_one_emote():
    assert 'event.target.closest("[data-world-wave]")' in APP
    assert "this.waveToWorld();" in APP
    wave = APP.split("  waveToWorld() {", 1)[1].split("\n  }\n", 1)[0]
    # The local arm plays without waiting on the relay.
    assert 'this.world?.playEmote?.(this.identity?.id || "", "wave", true);' in wave
    # And everybody else sees it through the existing text-free emote frame.
    assert '{ type: "interaction", kind: "emote", emote: "wave" }' in wave
    # Holding the button down must not turn one gesture into a frame stream.
    assert "WORLD_WAVE_COOLDOWN_MS" in wave
    assert "const WORLD_WAVE_COOLDOWN_MS = 2000;" in APP


def test_the_wave_lifts_the_right_arm_and_returns_it_to_rest():
    assert "function startAvatarWave(avatar" in SCENE
    # Local clicks and relayed frames both arrive through playEmote.
    emote = SCENE.split("  function playEmote(", 1)[1].split("\n  }\n", 1)[0]
    assert 'if (emote === "wave") startAvatarWave(avatar);' in emote

    activity = SCENE.split("function animateAvatarActivity(", 1)[1].split(
        "\nfunction ", 1
    )[0]
    # The pose is replayed inside animateAvatarActivity, which every walk,
    # sit, and ride path calls after it has written the frame's arm rotations,
    # so the gait cannot overwrite the raised arm.
    assert "avatar.userData.waveStartedAt" in activity
    assert "poseWavingArm(waveArm, rest, lift" in activity
    # It ends by putting the arm back exactly where it hung.
    assert "waveArm.rotation.z = 0;" in activity
    assert "waveArm.position.copy(rest);" in activity

    duration = re.search(r"const AVATAR_WAVE_DURATION_MS = (\d+);", SCENE)
    assert duration and 800 <= int(duration.group(1)) <= 5000


def test_the_shoulder_stays_pinned_while_the_arm_swings():
    pose = SCENE.split("function poseWavingArm(", 1)[1].split("\n}\n", 1)[0]
    # Arm meshes rotate about their own centre, so the mesh has to travel the
    # arc that keeps its shoulder end on the torso.
    assert "halfLength * Math.sin(angle)" in pose
    assert "halfLength * (1 - Math.cos(angle))" in pose


def test_the_button_hand_animation_respects_reduced_motion():
    assert ".world-chat-wave-button" in STYLES
    assert "@keyframes world-wave-hand" in STYLES
    assert ".world-chat-wave-button:hover .world-chat-wave-icon" in STYLES
    reduced = STYLES.rsplit("@media (prefers-reduced-motion: reduce)", 1)[1]
    assert ".world-chat-wave-button:hover .world-chat-wave-icon" in reduced
    assert "animation: none;" in reduced
    # The scene-side pose drops its shake under the same preference.
    assert "const shake = reducedMotion" in SCENE
