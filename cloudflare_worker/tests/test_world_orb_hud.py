"""Contracts for the compact World diagnostics and unified activity HUD."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public/world/world.css").read_text(encoding="utf-8")
CHAT = (ROOT / "public/dashboard-chat.js").read_text(encoding="utf-8")
CHAT_VIEW = (
    ROOT / "public/dashboard/partials/views/chat.html"
).read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
BUILD_BOARD = (ROOT / "src/world_build_board.py").read_text(encoding="utf-8")


def test_debug_bar_is_a_logo_sized_orb_with_graded_metric_dots():
    assert 'class="world-diagnostics-orb"' in WORLD
    for metric in (
        "fps", "frame", "draw", "input", "network", "traffic", "queue",
        "build", "world",
    ):
        assert f'data-diagnostic-dot="{metric}"' in WORLD
    assert "width: 56px;" in CSS
    assert '.world-diagnostics-orb > span[data-level="good"]' in CSS
    assert '.world-diagnostics-orb > span[data-level="caution"]' in CSS
    assert '.world-diagnostics-orb > span[data-level="high"]' in CSS
    assert "const dotLevels = {" in WORLD


def test_chat_orb_shows_last_speaker_and_expands_on_hover_or_focus():
    assert "data-world-chat-terminal-avatar" in WORLD
    assert "data-world-chat-terminal-avatar-image" in WORLD
    assert "data-world-chat-terminal-avatar-initial" in WORLD
    assert "wireHoverOrb(chatTerminal" in WORLD
    assert "setChatTerminalLastMessage(sender, text)" in WORLD
    assert ".world-chat-terminal:is([open], :hover, :focus-within)" in CSS
    assert "this.memberDirectory.find(" in WORLD


def test_world_embed_has_separate_chat_and_task_buttons_and_routing_step():
    assert 'id="fullChatSend"' in CHAT_VIEW
    assert 'id="fullChatTaskSend"' in CHAT_VIEW
    assert "data-dashboard-task-routing" in CHAT_VIEW
    assert "data-dashboard-task-department" in CHAT_VIEW
    assert "data-dashboard-task-team" in CHAT_VIEW
    assert "data-dashboard-task-assignee" in CHAT_VIEW
    assert "wireTaskSend()" in CHAT
    assert 'fullAction.value = "task"' in CHAT
    assert "Choose a team, then assign this task to a person or an agent." in CHAT
    assert "wireInput(fullInput, fullSend)" in CHAT
    assert "forceChat" not in CHAT
    assert 'fullSendLabel.textContent = presentation.label || "Send"' in CHAT
    assert 'fullAction.value !== "chat"' in CHAT
    assert "void runFullComposerAction(inputEl)" in CHAT


def test_chat_and_status_updates_share_a_ten_second_activity_stream():
    assert "data-world-activity-stream" in WORLD
    assert 'data.type === "forkmesh:world-activity"' in WORLD
    assert "this.activityNotice(" in WORLD
    assert "window.setTimeout(() => article.remove(), 10_100)" in WORLD
    assert "emitWorldActivity(text, \"status\")" in CHAT
    assert ".world-activity-stream article" in CSS
    assert "world-activity-out 420ms ease 9.55s forwards" in CSS


def test_new_hud_work_is_visible_as_a_completed_build_board_item():
    marker = '{ key: "task:world-orb-hud"'
    board = SCENE[
        SCENE.index("const WORLD_TASK_BULLETIN_ITEMS"):
        SCENE.index("function worldTaskBulletinSeed")
    ]
    assert marker in board
    assert board.index(marker) < board.index("task:avatar-selection-runtime")
    assert "deployed · ready for QA" in board
    assert 'estimate: "deployed · ready for QA", done: true' in board
    assert '"task:world-orb-hud"' in BUILD_BOARD


def test_new_hud_has_an_explicit_shared_qa_card():
    entry = (ROOT / "src/entry.py").read_text(encoding="utf-8")
    assert '"2026-07-28-24h-25"' in entry
    assert '"world-compact-debug-chat-orbs"' in entry
    assert "logo-sized circles while closed" in entry
    assert "fades after ten " in entry
