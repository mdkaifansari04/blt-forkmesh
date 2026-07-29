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
    assert 'chatTerminal.addEventListener("pointerenter"' in WORLD
    assert 'chatTerminal?.addEventListener("focusin"' in WORLD
    assert '!event.target.closest("[data-world-chat-terminal]")' in WORLD
    assert 'this.$("[data-world-chat-terminal]")?.removeAttribute("open")' in WORLD
    assert "setChatTerminalLastMessage(sender, text)" in WORLD
    assert ".world-chat-terminal:is([open], :hover, :focus-within)" in CSS
    assert "this.memberDirectory.find(" in WORLD


def test_hud_popouts_keep_close_controls_sticky_and_dismiss_outside():
    for heading in (
        ".world-detail-header",
        ".world-settings-heading",
        ".world-account-heading",
        ".world-chat-heading",
        ".world-office-panel-heading",
    ):
        assert heading in CSS
    assert "position: sticky;" in CSS
    assert "top: 12px;" in CSS
    for selector in (
        '!event.target.closest("[data-world-settings]")',
        '!event.target.closest("[data-world-account]")',
        '!event.target.closest("[data-world-chat]")',
    ):
        assert selector in WORLD
    assert '"[data-world-detail], [data-world-detail-resize]"' in WORLD


def test_corner_launchers_match_and_debug_stays_open_until_outside_click():
    assert "width: 56px;" in CSS
    assert ".fm-world .world-shirt-badge" in CSS
    assert 'diagnostics.addEventListener("pointerenter"' in WORLD
    assert 'diagnostics?.addEventListener("focusin"' in WORLD
    assert 'diagnostics.removeAttribute("open")' in WORLD
    assert 'details.addEventListener("pointerleave"' not in WORLD


def test_todo_actions_are_bridged_into_the_chat_transcript():
    assert "notifyChatArea(message, kind" in WORLD
    assert 'type: "forkmesh:chat-notification"' in WORLD
    assert 'data.type === "forkmesh:chat-ready"' in WORLD
    assert "this.chatTaskNotifications" in WORLD
    assert "this.notifyChatArea(copy, kind)" in WORLD
    assert 'toast: (message) => this.toast(message)' in WORLD
    assert '"Build priorities saved.' in WORLD
    assert '"Issue assigned to What we' in WORLD
    assert "moved to Done and was added to shared QA." in WORLD


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
    assert "void runFullComposerAction(inputEl, attachmentControl)" in CHAT


def test_chat_and_status_updates_share_the_native_transcript():
    assert "data-world-activity-stream" not in WORLD
    assert 'data.type === "forkmesh:world-activity"' in WORLD
    assert "this.notifyChatArea(copy, kind)" in WORLD
    assert 'type: "forkmesh:chat-notification"' in WORLD
    assert "emitWorldActivity(text, \"status\")" in CHAT
    assert "appendSystem(text, false)" in CHAT


def test_native_chat_starts_closed_and_floats_without_open_launcher_shell():
    terminal = WORLD[
        WORLD.index('<details class="world-diagnostics world-chat-terminal'):
        WORLD.index("</details>", WORLD.index("data-world-chat-terminal"))
    ]
    assert "data-world-chat-terminal open" not in terminal
    assert terminal.index('id="fullChatMessages"') < terminal.index(
        "data-dashboard-chat-context-rail",
    )
    assert terminal.index("data-dashboard-chat-context-rail") < terminal.index(
        "data-dashboard-chat-composer",
    )
    assert ".world-chat-terminal[open] > summary" in CSS
    assert "display: none;" in CSS
    assert "border: 0 !important;" in CSS
    assert 'messageActionButton("☺"' in CHAT
    assert "content?.append(buildMessageActions(record));" in CHAT


def test_open_native_chat_uses_lower_viewport_and_keeps_composer_at_bottom():
    assert "top: auto;" in CSS
    assert "bottom: max(12px, env(safe-area-inset-bottom));" in CSS
    assert "height: min(58dvh, 680px);" in CSS
    assert "var(--world-viewport-height, 100dvh)" in CSS
    assert 'grid-template-areas:\n    "transcript"' in CSS
    assert "grid-template-rows: minmax(0, 1fr) auto auto auto;" in CSS
    assert CSS.count("grid-area: transcript;") == 2
    assert "bottom: 260px;" not in CSS
    assert "display: grid !important;" in CSS
    assert "position: absolute;\n  inset: 0;" in CSS
    assert "contain: layout paint;" in CSS
    assert "max-height: min(18rem, 35vh);" in CSS
    assert "min-width: 0 !important;" in CSS
    assert "align-self: end;" in CSS
    assert "world-chat-composer-rise 360ms" in CSS
    assert ".world-chat-terminal:not([open])" in CSS
    assert "@media (prefers-reduced-motion: reduce)" in CSS


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
