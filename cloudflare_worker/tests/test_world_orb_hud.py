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


DIAGNOSTIC_DOTS = (
    "fps", "frame", "draw", "input", "network", "traffic", "queue",
    "build", "world",
)

# Renderer readings no health dot owns, charted under the nine dot traces.
DIAGNOSTIC_DETAIL_CHARTS = (
    "triangles", "cpu", "p95", "jank", "anim", "avatars",
)


def test_debug_bar_is_a_live_metric_pill_with_graded_health_dots():
    assert 'class="world-diagnostics-orb"' in WORLD
    for metric in DIAGNOSTIC_DOTS:
        assert f'data-diagnostic-dot="{metric}"' in WORLD
    assert "width: min(310px, calc(100vw - 24px));" in CSS
    assert "width: min(310px, calc(100vw - 92px));" in CSS
    assert "function diagnosticsChartPoints(" in WORLD
    assert 'this.renderDiagnosticsChart(snapshot)' in WORLD
    assert 'diagnosticsChartPoints(history, "triangles", {\n          zeroBased: false,' in WORLD
    assert "verticalInset = 1.5" in WORLD
    assert "const drawableHeight = Math.max(0, height - inset * 2);" in WORLD
    assert "A one-point polyline paints nothing" in WORLD
    assert "memoryMB: snapshot.memory.chartUsedMB" in WORLD
    assert 'chartSource: Number.isFinite(heapMB) ? "JS heap" : "estimated renderer"' in WORLD
    assert '.world-diagnostics-orb > span[data-level="good"]' in CSS
    assert '.world-diagnostics-orb > span[data-level="caution"]' in CSS
    assert '.world-diagnostics-orb > span[data-level="high"]' in CSS
    assert "border: 1px solid rgb(139 148 158 / 0.18);" in CSS
    assert "background:\n    linear-gradient(to bottom" in CSS
    assert "function diagnosticDotLevels(snapshot) {" in WORLD


def test_every_health_dot_has_its_own_chart_in_the_same_order():
    # Each of the nine dots is backed by a visible trace, laid out in the dots'
    # own order, then the renderer detail traces, then memory — none of which a
    # dot owns.
    charts = [*DIAGNOSTIC_DOTS, *DIAGNOSTIC_DETAIL_CHARTS, "memory"]
    for metric in charts:
        assert f'data-world-diagnostics-chart-metric="{metric}"' in WORLD
        assert f'data-world-diagnostics-chart-line="{metric}"' in WORLD
        assert f'data-world-diagnostics-chart-value="{metric}"' in WORLD
    strip = WORLD[
        WORLD.index('class="world-diagnostics-chart"'):
        WORLD.index("<strong>WORLD DEBUG</strong>")
    ]
    positions = [
        strip.index(f'data-world-diagnostics-chart-metric="{metric}"')
        for metric in charts
    ]
    assert positions == sorted(positions)
    dots = WORLD[
        WORLD.index("data-world-diagnostics-dots"):
        WORLD.index('class="world-diagnostics-chart"')
    ]
    assert [
        dots.index(f'data-diagnostic-dot="{metric}"')
        for metric in DIAGNOSTIC_DOTS
    ] == sorted(
        dots.index(f'data-diagnostic-dot="{metric}"')
        for metric in DIAGNOSTIC_DOTS
    )
    # Three across mirrors the 3x3 orb; memory runs the full width underneath.
    assert "grid-template-columns: repeat(3, minmax(0, 1fr));" in CSS
    assert (
        '.world-diagnostics-chart-metric[data-world-diagnostics-chart-metric="memory"] {\n'
        "  grid-column: 1 / -1;\n"
    ) in CSS
    # The collapsed pill grows for the grid; the chat orb keeps its circle.
    assert ".world-diagnostics:not(.world-chat-terminal) > summary {\n  height: auto;\n}" in CSS


def test_draw_charts_calls_and_triangles_get_their_own_live_trace():
    # DRAW is the per-frame submission count; the triangle count it used to
    # stand in for now has its own trace, so a scene that trades calls for
    # triangles (or the reverse) is readable from the strip alone.
    assert '<b>DRAW</b>' in WORLD
    assert 'title="Draw calls per frame"' in WORLD
    assert 'diagnosticsChartPoints(history, "drawCalls", {' in WORLD
    assert "value: renderer ? compactCount(renderer.calls) : \"—\"," in WORLD
    assert '<b>TRIS △</b>' in WORLD
    assert 'title="Triangles drawn per frame"' in WORLD
    assert "value: renderer ? compactCount(renderer.triangles) : \"—\"," in WORLD


def test_renderer_detail_traces_sample_once_a_second_and_grade_themselves():
    # Every detail trace is a fresh per-second reading, and each one that has a
    # threshold is coloured by it rather than always drawing green.
    for label in ("<b>CPU</b>", "<b>P95</b>", "<b>JANK</b>", "<b>ANIM</b>", "<b>AVTR</b>"):
        assert label in WORLD
    for key in (
        "drawCalls: liveRenderer ? liveRenderer.calls : NaN,",
        "frameTimeP95Ms: liveRenderer ? liveRenderer.frameTimeP95Ms : NaN,",
        "cpuFrameMs: liveRenderer ? liveRenderer.cpuFrameMs : NaN,",
        "animations: liveRenderer ? liveRenderer.animations : NaN,",
        "remoteAvatars: liveRenderer ? liveRenderer.remoteAvatars : NaN,",
    ):
        assert key in WORLD
    for metric in ("triangles", "cpuFrameMs", "frameTimeP95Ms", "longFrames"):
        assert f'diagnosticLevel("{metric}", ' in WORLD
    # A paused or missing renderer dims the detail cells instead of plotting
    # zeros it never measured.
    assert WORLD.count(': "unavailable",') >= 6
    assert '.world-diagnostics-chart-metric[data-level="unavailable"]' in CSS
    # The two detail rows would swallow a phone screen, so the closed pill drops
    # them there and opening DEBUG brings them back.
    for metric in DIAGNOSTIC_DETAIL_CHARTS:
        assert (
            "data-world-diagnostics-chart-detail "
            f'data-world-diagnostics-chart-metric="{metric}"'
        ) in WORLD
    assert (
        "  .world-diagnostics:not([open])\n"
        "    .world-diagnostics-chart-metric[data-world-diagnostics-chart-detail] {\n"
        "    display: none;\n"
        "  }"
    ) in CSS


def test_state_dots_chart_their_own_verdict_on_a_fixed_scale():
    # network, build and world grade a state rather than a measurement, so
    # their traces plot the dot's own verdict and never auto-scale a steady
    # "watch" back up to a healthy-looking top line.
    assert "const WORLD_DIAGNOSTIC_HEALTH_SCORES = { good: 1, caution: 0.5, high: 0 };" in WORLD
    assert "function diagnosticHealthScore(level) {" in WORLD
    assert "if (Number.isFinite(ceiling)) high = Math.max(high, ceiling);" in WORLD
    assert "diagnosticsChartPoints(history, key, { ceiling: 1 })" in WORLD
    for key in ("networkHealth", "buildHealth", "worldHealth"):
        assert f"{key}: diagnosticHealthScore(dotLevels." in WORLD
        assert f'healthChart("{key}")' in WORLD


def test_history_keeps_sampling_while_the_scene_is_paused():
    # The socket, queue and build traces stay continuous across a paused scene;
    # renderer readings go in as NaN so their traces gap instead of dipping.
    assert "this.diagnosticsFrameHistory.push({" in WORLD
    assert "snapshot.renderer && !snapshot.renderer.paused ? snapshot.renderer : null" in WORLD
    assert "fps: liveRenderer ? liveRenderer.fps : NaN," in WORLD
    assert "longestFrameMs: liveRenderer ? liveRenderer.longestFrameMs : NaN," in WORLD
    assert "inputResponseMs: liveRenderer ? liveRenderer.inputResponseMs : NaN," in WORLD
    # Frame health only speaks for seconds that were actually rendered.
    assert (
        "const samples = (Array.isArray(history) ? history : []).filter((sample) =>\n"
        "      Number.isFinite(sample?.frameTimeMs),\n"
        "    );"
    ) in WORLD
    # Cumulative queue counters are charted as the per-second rise, not the sum.
    assert "queueEvents: Math.max(0, queueTotal - previousQueueTotal)," in WORLD
    assert "this.diagnosticsQueueTotal = NaN;" in WORLD


def test_chat_orb_becomes_an_idle_prompt_and_opens_accessibly():
    assert "data-world-chat-terminal-avatar" in WORLD
    assert "data-world-chat-terminal-avatar-image" in WORLD
    assert "data-world-chat-terminal-avatar-initial" in WORLD
    assert "world-chat-terminal-prompt-icon" in WORLD
    assert "scheduleQuickComposerIdle()" in WORLD
    assert "5 * 60 * 1000" in WORLD
    assert 'chatTerminal.addEventListener("pointerenter"' in WORLD
    assert 'chatSummary?.addEventListener("keydown"' in WORLD
    assert '"[data-world-chat-terminal-close]"' in WORLD
    assert "handleQuickChatEscape" in WORLD
    assert 'window.addEventListener("keydown", this.handleQuickChatEscape)' in WORLD
    assert '!event.target.closest("[data-world-chat-terminal]")' in WORLD
    assert 'this.$("[data-world-chat-terminal]")?.removeAttribute("open")' in WORLD
    assert "setChatTerminalLastMessage(sender, text)" in WORLD
    assert ".world-chat-terminal--idle .world-chat-terminal-prompt-icon" in CSS
    assert ".world-chat-terminal:not([open])" in CSS
    assert "this.memberDirectory.find(" in WORLD


def test_quick_composer_keeps_feed_channels_and_enter_action_independent():
    for channel in (
        "general",
        "private",
        "direct",
        "errors",
        "tasks",
        "notifications",
    ):
        assert f'data-world-quick-channel="{channel}"' in WORLD
    assert 'data-world-quick-composer-avatar' in WORLD
    assert 'data-world-quick-attachment' in WORLD
    assert 'class="world-quick-input-shell"' in WORLD
    assert 'class="world-quick-actions"' in WORLD
    assert 'data-world-chat-terminal-close' in WORLD
    assert 'terminal.dataset.showFeed = String(selected === "general")' in WORLD
    assert 'const selected = "general";' in WORLD
    assert "forkmesh.worldComposer.selectedChannel" in WORLD
    terminal = WORLD[
        WORLD.index('<details class="world-diagnostics world-chat-terminal'):
        WORLD.index("</details>", WORLD.index("data-world-chat-terminal"))
    ]
    assert terminal.index("data-world-quick-channels") < terminal.index(
        "data-world-quick-chat-feed",
    )
    assert "justify-content: flex-start;" in CSS
    assert "justify-self: stretch;" in CSS
    assert "text-align: left;" in CSS
    assert "grid-template-columns: minmax(0, 1fr);" in CSS
    assert ".world-quick-channels {\n  display: flex;\n  width: 100%;" in CSS
    assert "Hovering the chat orb opens only the composer" not in CSS
    assert 'fullAction.value = "chat";' in CHAT
    keydown = CHAT.split('inputEl.addEventListener("keydown"', 1)[1].split(
        'inputEl.addEventListener("input"', 1
    )[0]
    assert 'fullAction.value = "chat"' not in keydown
    assert "pulseWorldQuickComposer(fullAction?.value || \"chat\")" in CHAT
    assert "fullComposerStatus.classList.remove(" in CHAT
    assert "fullComposerStatus.className =" not in CHAT
    assert 'sourceLabel:' in CHAT
    assert '[data-just-sent="task"]' in CSS


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


def test_corner_launchers_stay_clear_and_debug_stays_open_until_outside_click():
    assert "width: min(310px, calc(100vw - 24px));" in CSS
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
    # Non-chat notices still land in the native transcript, and every event a
    # visitor may see also raises a floating bubble right above the chat icon.
    assert "data-world-activity-stream" in WORLD
    assert 'data.type === "forkmesh:world-activity"' in WORLD
    assert 'if (kind !== "chat" && transcript) this.notifyChatArea(copy, kind)' in WORLD
    assert 'type: "forkmesh:chat-notification"' in WORLD
    assert "emitWorldActivity(text, \"status\")" in CHAT
    assert "appendSystem(text, false)" in CHAT
    # Bubbles stack bottom-up from just above the chat launcher; chat lines
    # are never forwarded to the transcript twice.
    assert "stream.prepend(article)" in WORLD
    assert '{ kind: "chat", sender }' in WORLD
    assert "flex-direction: column-reverse" in CSS


def test_native_chat_starts_closed_and_floats_without_open_launcher_shell():
    terminal = WORLD[
        WORLD.index('<details class="world-diagnostics world-chat-terminal'):
        WORLD.index("</details>", WORLD.index("data-world-chat-terminal"))
    ]
    assert "data-world-chat-terminal open" not in terminal
    assert terminal.index("world-quick-chat-header") < terminal.index(
        'id="fullChatMessages"',
    )
    assert terminal.index('id="fullChatMessages"') < terminal.index(
        "data-dashboard-chat-composer",
    )
    assert "data-dashboard-chat-context-rail" not in terminal
    assert "data-dashboard-chat-composer-toolbar hidden inert" in terminal
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
    assert "var(--world-chat-viewport-height, 100dvh)" in CSS
    assert "var(--world-chat-covered-bottom, 0px)" in CSS
    assert '"--world-chat-viewport-height"' in WORLD
    assert '"--world-chat-covered-bottom"' in WORLD
    assert "dataset.worldChatCompact" in WORLD
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
    assert "grid-template-areas:\n    \"header\"\n    \"feed\"\n    \"composer\";" in CSS
    assert "grid-template-rows: auto minmax(80px, 1fr) auto;" in CSS
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
    # The QA deck moved out of entry.py so the isolate only pays for it on the
    # QA route; the cards themselves still have to spell out the new HUD work.
    qa = (ROOT / "src/world_qa.py").read_text(encoding="utf-8")
    assert "WORLD_QA_DECK_REVISION = " in qa
    assert '"world-compact-debug-chat-orbs"' in qa
    assert "logo-sized circles while closed" in qa
    assert "fades after ten " in qa
    assert '"world-debug-live-telemetry-strip"' in qa
    assert "DRAW must read draw calls per frame and TRIS " in qa
