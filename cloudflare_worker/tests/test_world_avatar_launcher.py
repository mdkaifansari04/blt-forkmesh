"""Regression contracts for the compact circular World avatar launcher."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public/world/world.css").read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
BUILD_BOARD = (ROOT / "src/world_build_board.py").read_text(encoding="utf-8")


def test_avatar_launcher_keeps_actionable_counts_on_their_related_tools():
    assert 'data-world-hud data-hud-expanded="false"' in WORLD
    assert 'data-world-top-actions aria-label="World tools"' in WORLD
    assert 'data-world-shirt-badge' in WORLD
    assert 'aria-expanded="false"' in WORLD
    assert (
        'class="world-tool-count" data-world-notification-count'
        in WORLD.split("data-world-notifications-open", 1)[1].split("</button>", 1)[0]
    )
    assert (
        'class="world-tool-count" data-world-admin-error-count'
        in WORLD.split("data-world-admin-errors", 1)[1].split("</button>", 1)[0]
    )
    assert (
        'class="world-tool-count" data-world-task-count'
        in WORLD.split("data-world-tasks-open", 1)[1].split("</button>", 1)[0]
    )
    avatar_markup = WORLD.split('class="world-shirt-badge"', 1)[1].split(
        "</button>", 1
    )[0]
    assert "data-world-notification-count" not in avatar_markup
    assert "data-world-admin-error-count" not in avatar_markup
    assert "data-world-task-count" not in avatar_markup
    assert ".world-shirt-badge {" in CSS
    badge_css = CSS.split(".world-shirt-badge {", 1)[1].split(
        ".world-shirt-badge:hover", 1
    )[0]
    assert "border-radius: 50%;" in badge_css
    final_launcher_css = CSS.rsplit("/* Fixed launcher geometry.", 1)[1]
    assert ".fm-world .world-shirt-badge" in final_launcher_css
    assert "border-radius: 50%;" in final_launcher_css
    assert "clip-path: circle(50%);" in final_launcher_css
    assert ".world-shirt-avatar" in CSS
    assert ".world-tool-count:not([hidden])" in CSS
    assert 'content: attr(data-world-tooltip);' in CSS


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
