"""Regression contracts for the compact circular World avatar launcher."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public/world/world.css").read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
BUILD_BOARD = (ROOT / "src/world_build_board.py").read_text(encoding="utf-8")


def test_avatar_is_the_compact_hud_launcher_with_visible_count_badges():
    assert 'data-world-hud data-hud-expanded="false"' in WORLD
    assert 'data-world-top-actions aria-label="World tools"' in WORLD
    assert 'data-world-shirt-badge' in WORLD
    assert 'aria-expanded="false"' in WORLD
    assert 'class="world-shirt-count world-shirt-count--alerts"' in WORLD
    assert 'class="world-shirt-count world-shirt-count--errors"' in WORLD
    assert 'class="world-shirt-count world-shirt-count--tasks"' in WORLD
    assert ".world-shirt-badge {" in CSS
    badge_css = CSS.split(".world-shirt-badge {", 1)[1].split(
        ".world-shirt-badge:hover", 1
    )[0]
    assert "border-radius: 50%;" in badge_css
    assert ".world-shirt-avatar" in CSS
    assert ".world-shirt-count--alerts" in CSS


def test_hud_controls_slide_out_without_resizing_and_latch_until_movement():
    assert '.world-hud[data-hud-expanded="true"] .world-top-link' in CSS
    assert "width: 40px;" in CSS
    assert "transform: translateX(18px) scale(0.84);" in CSS
    assert "const hudHoverTargets = [topActions, rightRail].filter(Boolean);" in WORLD
    assert "target.addEventListener(\"pointerenter\", openHud);" in WORLD
    assert "this.setWorldRightRailExpanded(false);" in WORLD
    assert "if (movement?.moving === true) this.setWorldRightRailExpanded(false);" in WORLD


def test_touch_first_tap_reveals_the_launcher_before_opening_settings():
    assert 'const avatarLauncher = event.target.closest("[data-world-shirt-badge]");' in WORLD
    assert "!hoverCapable" in WORLD
    assert "Touch has no hover: the first press exposes the same launcher" in WORLD


def test_recovered_launcher_is_visible_on_the_world_build_board():
    assert 'key: "task:avatar-hud-launcher"' in SCENE
    assert "Restore circular avatar HUD launcher" in SCENE
    assert '"task:avatar-hud-launcher"' in BUILD_BOARD
