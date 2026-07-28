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
    assert '{ method: "DELETE" }' in tasks
    assert 'window.confirm("Delete this task' in tasks
    assert ".world-office-task-manager select option" in css
    assert "background: #071713" in css
    assert '.world-office-task[data-status="done"]' in css


def test_marketing_members_can_add_private_proof_from_their_own_desk():
    tasks = source(TASKS)
    scene = source(SCENE)
    for contract in (
        "function normalizedProof(proof)",
        "proofs = Array.isArray(payload?.proofs)",
        'if (action === "proof")',
        "!marketingMembers.includes(actor) || member !== actor",
        '`${OFFICE_TASKS_PATH}/proofs`',
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


def test_task_text_is_bounded_and_html_escaped_before_rendering():
    tasks = source(TASKS)
    assert "function escapeHTML" in tasks
    assert ".slice(0, limit)" in tasks
    assert "${escapeHTML(task.title)}" in tasks
    assert "${escapeHTML(task.assignee)}" in tasks
    assert "safeTaskId" in tasks


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
