#!/usr/bin/env python3
"""HUD wave button and the avatar arm pose it plays (issue #532)."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")
APP = (WORLD / "world.js").read_text(encoding="utf-8")
STYLES = (WORLD / "world.css").read_text(encoding="utf-8")


def test_a_small_wave_button_sits_in_the_hud_top_bar():
    button = APP.split("data-world-wave")
    assert len(button) == 3, "wave button and its click handler are both needed"
    markup = button[0].rsplit("<button", 1)[1] + button[1].split("</button>", 1)[0]
    assert "world-top-link" in markup, "the wave button matches the other HUD chips"
    assert 'aria-label="Wave your avatar\'s arm"' in markup
    # The button lives in the top-bar nav, next to the other world tools.
    assert APP.index("data-world-wave") > APP.index('aria-label="World tools"')
    assert APP.index("data-world-wave") < APP.index("data-world-sound-toggle")


def test_the_phone_tool_row_keeps_every_existing_control_on_screen():
    # The icon-only row is already tight, so the extra chip buys its slot with
    # a narrower gutter and steps aside entirely on the smallest phones rather
    # than pushing alerts or settings past the edge of the screen.
    compact = STYLES.split("@media (max-width: 720px) {", 1)
    assert len(compact) == 2, "compact top-bar media query is missing"
    assert "gap: 6px;" in compact[1].split("\n}\n", 1)[0]
    narrow = STYLES.split("@media (max-width: 375px) {", 1)
    assert len(narrow) == 2, "narrow-phone rule for the wave chip is missing"
    narrow_block = narrow[1].split("\n}\n", 1)[0]
    assert ".world-top-actions > .world-wave-button" in narrow_block
    assert "display: none;" in narrow_block


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
    assert ".world-wave-button" in STYLES
    assert "@keyframes world-wave-hand" in STYLES
    hover = STYLES.split(".world-wave-button", 1)[1].split(
        "[data-world-sound-toggle]", 1
    )[0]
    assert "@media (prefers-reduced-motion: reduce)" in hover
    # The scene-side pose drops its shake under the same preference.
    assert "const shake = reducedMotion" in SCENE
