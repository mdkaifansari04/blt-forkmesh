#!/usr/bin/env python3
"""Frontend contracts for the private Office marketing task board."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public" / "world"
TASKS = PUBLIC / "world-office-tasks.js"
SCENE = PUBLIC / "world-scene.js"
WORLD = PUBLIC / "world.js"
OFFICE = PUBLIC / "world-office.js"
CSS = PUBLIC / "world.css"


def source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def test_marketing_task_controller_is_wired_to_the_office_and_scene():
    world = source(WORLD)
    office = source(OFFICE)
    for contract in (
        'from "./world-office-tasks.js"',
        "createWorldOfficeTasksController",
        "onOfficeTaskBoardSelect",
        "this.officeTasks",
    ):
        assert contract in world
    assert 'tasks?.setActive?.(floor.id === "marketing")' in office
    assert "tasks?.setActive?.(false)" in office


def test_marketing_board_has_accessible_create_assign_and_timer_controls():
    world = source(WORLD)
    for contract in (
        "data-world-office-task-panel",
        "data-world-office-task-form",
        "data-world-office-task-name",
        "data-world-office-task-assignee",
        "Select a Marketing team member",
        "data-world-office-task-list",
        "data-world-office-task-checkin",
        'role="status"',
        'aria-live="polite"',
    ):
        assert contract in world


def test_tasks_use_https_polling_and_server_clock_without_world_socket_data():
    tasks = source(TASKS)
    assert '"/api/world/office/marketing-tasks"' in tasks
    assert 'cache: "no-store"' in tasks
    assert "serverNowAtSync" in tasks
    assert "nextCheckinAt" in tasks
    assert "elapsedMs" in tasks
    assert "performance.now()" in tasks
    assert "OFFICE_TASKS_POLL_MS" in tasks
    for forbidden in (
        "WebSocket",
        "sendPresence",
        "sendMeeting",
        "localStorage",
        "sessionStorage",
    ):
        assert forbidden not in tasks


def test_task_polling_is_visibility_aware_and_adapts_after_leaving_the_office():
    tasks = source(TASKS)
    assert "const OFFICE_TASKS_POLL_MS = 60_000" in tasks
    assert "const OFFICE_TASKS_BACKGROUND_POLL_MS = 5 * 60_000" in tasks
    schedule = tasks[
        tasks.index("  function schedulePoll() {"):
        tasks.index("\n  async function refresh(", tasks.index("  function schedulePoll() {"))
    ]
    assert (
        "document.hidden || (!officeActive && !opened && !personalView)"
        in schedule
    )
    assert "if (!document.hidden) await refresh({ quiet: true })" in schedule
    assert "function onVisibilityChange()" in schedule
    assert "void refresh({ quiet: true }).finally(schedulePoll)" in schedule
    assert (
        'document.addEventListener("visibilitychange", onVisibilityChange)'
        in tasks
    )
    assert (
        'document.removeEventListener("visibilitychange", onVisibilityChange)'
        in tasks
    )


def test_random_checkin_window_stays_between_four_and_nine_minutes():
    script = f"""
      import {{ officeTaskCheckinDelay }} from {json.dumps(TASKS.as_uri())};
      if (officeTaskCheckinDelay(0) !== 240000) throw new Error("minimum");
      if (officeTaskCheckinDelay(1) !== 540000) throw new Error("maximum");
      const midpoint = officeTaskCheckinDelay(0.5);
      if (midpoint < 240000 || midpoint > 540000) throw new Error("bounds");
    """
    subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )


def test_active_timer_uses_a_bounded_keepalive_stop_when_world_is_destroyed():
    tasks = source(TASKS)
    assert "function stopActiveForDeparture()" in tasks
    assert '`${OFFICE_TASKS_PATH}/stop-active`' in tasks
    assert 'keepalive: true' in tasks
    assert 'authorization: `Bearer ${sessionToken}`' in tasks
    destroy = tasks[
        tasks.index("  function destroy() {"):
        tasks.index("\n  root.addEventListener", tasks.index("  function destroy() {"))
    ]
    assert destroy.index("stopActiveForDeparture()") < destroy.index(
        "stopMonitoring()"
    )


def test_task_access_is_session_gated_and_wall_payload_is_explicitly_authorized():
    tasks = source(TASKS)
    assert "getSession()?.sessionToken" in tasks
    assert "authorized = false" in tasks
    assert "authorized = payload?.authorized !== false" in tasks
    assert "world.updateOfficeMarketingTasks" in tasks
    assert "authorized," in tasks
    assert "physicalState(\"locked\"" in tasks


def test_top_nav_task_count_and_owner_catalog_include_departments():
    tasks = source(TASKS)
    world = source(WORLD)
    assert "data-world-tasks-open" in world
    assert "data-world-task-count" in world
    assert "data-world-organization-task-heading" in world
    assert "taskCount.textContent = String(tasks.length)" in tasks
    assert "taskCount.hidden = !authorized || tasks.length < 1" in tasks
    assert "All organization tasks" in tasks
    assert "grouped by department" in tasks
    assert "· ${escapeHTML(task.department)}" in tasks


def test_work_tab_expands_and_active_tasks_have_readable_spinner_status():
    tasks = source(TASKS)
    world = source(WORLD)
    css = source(CSS)
    assert "panel.dataset.activeTab = selected" in world
    assert '.world-settings[data-active-tab="work"]' in css
    assert "920px" in css
    assert "world-office-task-progress-label" in tasks
    assert "In progress" in tasks
    assert "@keyframes world-office-task-spin" in css
    assert "animation: world-office-task-spin 720ms linear infinite" in css
    assert "white-space: normal" in css
    assert "-webkit-line-clamp: 2" not in css


def test_new_agent_tasks_show_one_bounded_world_bot_bubble():
    tasks = source(TASKS)
    scene = source(SCENE)
    assert "const announcedAgentTaskIds = new Set()" in tasks
    assert "function announceAgentTasks()" in tasks
    assert 'task.assigneeKind === "agent"' in tasks
    assert "world.showAgentTaskBubble?.(task.assigneeKind, task.title)" in tasks
    assert "function showAgentTaskBubble(botId, title)" in scene
    assert "if (!agentBotAccessAllowed) return false" in scene
    assert "New organization task: ${taskTitle}" in scene
    assert "showAgentTaskBubble," in scene


def test_only_assignees_receive_start_stop_and_checkin_controls():
    tasks = source(TASKS)
    assert "task.assignee === actor" in tasks
    assert 'task.status === "active"' in tasks
    assert 'data-world-office-task-action="${activeTask ? "stop" : "start"}"' in tasks
    assert "ownActiveTasks()" in tasks
    assert "showCheckin" in tasks


def test_manager_assignment_control_renders_only_returned_marketing_members():
    tasks = source(TASKS)
    world = source(WORLD)
    assert "<select" in world
    assert "data-world-office-task-assignees" in world
    assert "Select a Marketing team member" in tasks
    assert "marketingMembers" in tasks
    assert "<option value=" in tasks
    assert "<datalist" not in world


def test_physical_wall_has_direct_bounded_marketing_task_controls():
    tasks = source(TASKS)
    scene = source(SCENE)
    world = source(WORLD)
    for contract in (
        "async function physicalAction(payload = {})",
        "canStartStop: task.assignee === actor",
        "canComplete:",
        "canDelete: canManage",
        "members: canManage ? assignable : marketingMembers",
        "physicalAction,",
    ):
        assert contract in tasks
    for contract in (
        "function marketingTaskWallAction(uv)",
        '"+ ADD MARKETING TASK"',
        '"START"',
        '"DONE"',
        '"DELETE"',
        'action = task.status === "active" ? "stop" : "start"',
        "onOfficeTaskWallAction(action)",
    ):
        assert contract in scene
    assert "this.officeTasks?.physicalAction?.(action)" in world
    assert "new THREE.BoxGeometry(18.5, 11.4, 0.18)" in scene
    assert "new THREE.PlaneGeometry(18.12, 11.02)" in scene


def test_assignment_control_has_explicit_contrast_and_tasks_can_finish_or_delete():
    tasks = source(TASKS)
    css = source(CSS)
    assert 'data-world-office-task-action="complete"' in tasks
    assert 'data-world-office-task-action="delete"' in tasks
    assert '{ method: "DELETE", removeOnSuccess: true }' in tasks
    assert 'window.confirm("Delete this task' in tasks
    assert ".world-office-task-manager select option" in css
    assert "background: #071713" in css
    assert '.world-office-task[data-status="done"]' in css


def test_a_confirmed_delete_leaves_every_list_and_a_refusal_is_announced():
    """A deleted row may never be repainted by a slower or skipped read."""
    tasks = source(TASKS)
    world = source(WORLD)
    for contract in (
        "function dropTask(taskId)",
        "if (removeOnSuccess) dropTask(taskId);",
        "await refresh({ quiet: true, force: true });",
        # A read that started before the delete must not join it, and its
        # older payload must not overwrite the newer list.
        'if ((!monitoring && !force) || typeof fetchJSON !== "function")',
        "if (refreshPromise && !force) return refreshPromise;",
        "const sequence = ++refreshSequence;",
        "if (sequence !== refreshSequence) return false;",
        "dedupe: !force,",
        # The Office status line is invisible from the Local controls Work
        # tab, so a rejected change is also toasted.
        "toast(errorMessage);",
    ):
        assert contract in tasks
    # fetchJSON honors the dedupe opt-out the forced read relies on.
    assert "dedupe = true," in world
    assert "if (canDedupe && this.inflightRequests.has(requestKey))" in world


def test_completed_tasks_are_ready_for_qa_with_three_verdict_controls():
    tasks = source(TASKS)
    world = source(WORLD)
    css = source(CSS)
    for contract in (
        '"Ready for QA"',
        'data-world-office-task-action="qa-${verdict}"',
        '["pass", "fail", "unsure"]',
        "onQaVerdict({ task, verdict })",
        "Task marked done and ready for QA.",
    ):
        assert contract in tasks
    assert "this.recordTaskQaVerdict(task, verdict)" in world
    assert "async recordTaskQaVerdict(task, verdict)" in world
    assert "this.recordQaVerdict(verdict)" in world
    assert ".world-office-task-qa-actions" in css


def test_marketing_members_can_add_private_proof_from_their_own_desk():
    tasks = source(TASKS)
    scene = source(SCENE)
    for contract in (
        "function normalizedProof(proof)",
        "proofs = Array.isArray(marketingPayload?.proofs)",
        'if (action === "proof")',
        "!marketingMembers.includes(actor) || member !== actor",
        '`${MARKETING_TASKS_PATH}/proofs`',
        '"Paste the public HTTPS social post link:"',
    ):
        assert contract in tasks
    for contract in (
        "function officeMarketingDeskProofTexture(",
        "officeMarketingTaskSnapshot.proofs",
        'proofPanel.userData.interactive = "office-marketing-proof-desk"',
        'action: "proof"',
    ):
        assert contract in scene


def test_private_marketing_initiatives_are_rendered_on_a_clickable_world_panel():
    tasks = source(TASKS)
    scene = source(SCENE)
    for contract in (
        "function normalizedInitiative(initiative)",
        "initiatives = Array.isArray(marketingPayload?.initiatives)",
        'if (action === "initiative")',
        "initiatives.some((item) => item.href === href)",
        'window.open(href, "_blank", "noopener,noreferrer")',
    ):
        assert contract in tasks
    for contract in (
        "function officeMarketingInitiativesTexture(",
        '"forkmesh-office-marketing-initiatives-board"',
        '"office-marketing-initiatives-board"',
        "function marketingInitiativeWallAction(uv)",
        'return { action: "initiative", href: initiative.href }',
    ):
        assert contract in scene


def test_task_text_is_bounded_and_html_escaped_before_rendering():
    tasks = source(TASKS)
    assert "function escapeHTML" in tasks
    assert ".slice(0, limit)" in tasks
    assert "${escapeHTML(task.title)}" in tasks
    assert "task.assigneeKind === \"user\"" in tasks
    assert "`@${task.assignee}`" in tasks
    assert "escapeHTML(" in tasks
    assert "safeTaskId" in tasks


def test_task_rows_capture_and_retain_a_completion_work_note():
    tasks = source(TASKS)
    css = source(CSS)
    for contract in (
        "completionNote: text(task.completionNote, 4000)",
        "What I did to complete this task",
        "data-world-office-task-completion-note",
        'action === "complete" ? { completionNote } : {}',
        "task.completionNote || \"\"",
    ):
        assert contract in tasks
    for selector in (
        ".world-office-task-completion",
        ".world-office-task-completion-editor",
        ".world-office-task-completion-editor textarea",
    ):
        assert selector in css


def test_lobby_task_bounty_desk_creates_visible_non_custodial_bids():
    world = source(WORLD)
    scene = source(SCENE)
    tasks = source(TASKS)
    css = source(CSS)
    for contract in (
        "async openLobbyTaskBidKiosk()",
        'data-world-task-bid-form',
        'kind: "bid"',
        "bountyAmountSol: amountSol",
        '"/api/tasks"',
        "This is a compensation request only.",
        "does not reserve, custody, or transfer SOL",
        "this.officeTasks?.refreshNow?.()",
    ):
        assert contract in world
    for contract in (
        '"forkmesh-office-task-bid-kiosk"',
        '"TASK BOUNTY DESK"',
        '"office-task-bid-kiosk"',
        "onLobbyTaskBidKioskSelect()",
        '"REQUEST ONLY · NO FUNDS HELD"',
        '{ key: "task:lobby-task-bounties"',
    ):
        assert contract in scene
    for contract in (
        'const kind = task.kind === "bid" ? "bid" : "task"',
        'data-kind="${bid ? "bid" : "task"}"',
        "SOL bounty requested · bidder @",
        "world-office-task-bid-badge",
        "function refreshNow()",
    ):
        assert contract in tasks
    assert '.world-office-task[data-kind="bid"]' in css
    assert ".world-office-task-bid-badge" in css


def test_task_panel_is_overlayed_responsive_and_reduced_motion_safe():
    css = source(CSS)
    for selector in (
        ".world-office-task-panel",
        ".world-office-task-manager",
        ".world-office-task-list",
        ".world-office-task-controls",
        ".world-office-task-checkin",
    ):
        assert selector in css
    assert '.world-office-task-panel[data-open="true"]' in css
    assert (
        '.fm-world:has(.world-office-task-panel[data-open="true"])'
        in css
    )
    assert "@media (max-width: 720px)" in css
    assert "@media (prefers-reduced-motion: reduce)" in css


def test_physical_board_displays_bounded_authorized_summaries_only():
    scene = source(SCENE)
    assert "normalizeOfficeMarketingTasks(payload)" in scene
    assert "const authorized = source.authorized === true" in scene
    assert "const OFFICE_MARKETING_TASK_LIMIT = 250" in scene
    assert "task.elapsed" in scene
    assert "officeMarketingRosterGroup" in scene
    assert "officeMarketingAttendanceTexture" in scene
    assert "officeReclaimedWoodTexture" in scene


def test_one_general_bot_replaces_the_claude_and_codex_task_choice():
    tasks = source(TASKS)
    chat_view = (
        ROOT / "public" / "dashboard" / "partials" / "views" / "chat.html"
    ).read_text(encoding="utf-8")
    chat = (ROOT / "public" / "dashboard-chat.js").read_text(encoding="utf-8")
    assert '<option value="agent">Bot</option>' in chat_view
    assert '<option value="codex">Codex</option>' not in chat_view
    assert '<option value="claude">Claude</option>' not in chat_view
    assert 'const ORG_BOT_SENDER_ID = "agent"' in chat
    assert "queueOrgAgent(\n            \"agent\"," in chat
    assert '"Bot",' in tasks
    assert '"Codex"' not in tasks
    assert '"Claude"' not in tasks


def test_queued_bot_tasks_can_be_returned_to_the_task_list():
    tasks = source(TASKS)
    css = source(CSS)
    for contract in (
        'const botTask = task.assigneeKind === "agent"',
        "canManage || task.createdBy === actor",
        'data-world-office-task-action="return"',
        '"start", "stop", "complete", "delete", "return",',
        "Task returned to the task list.",
        "queued on a node",
    ):
        assert contract in tasks
    assert ".world-office-task-return" in css
