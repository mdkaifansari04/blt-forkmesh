#!/usr/bin/env python3
"""Contracts for the World "Local controls" tabs: View, Work and Security.

The Security tab is the only place the World shows an account's own signed-in
sessions and the address each one came from; the Work tab and the owner-only
avatar back plate carry assigned work instead of the retired session card.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
TASKS = (ROOT / "public" / "world" / "world-office-tasks.js").read_text(
    encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")


def test_local_controls_expose_three_labelled_tabs():
    for contract in (
        "LOCAL CONTROLS",
        'role="tablist" aria-label="Local controls section"',
        'data-world-settings-tab="view"',
        'data-world-settings-tab="work"',
        'data-world-settings-tab="security"',
        'data-world-settings-pane="view"',
        'data-world-settings-pane="work"',
        'data-world-settings-pane="security"',
        "selectSettingsTab(",
    ):
        assert contract in WORLD
    assert ".world-settings-tabs" in CSS
    # Panes are switched by the hidden attribute, so only one is readable.
    assert "pane.hidden = pane.dataset.worldSettingsPane !== selected" in WORLD


def test_security_tab_lists_sessions_with_addresses_and_logout_controls():
    for contract in (
        'this.fetchJSON("/api/accounts/sessions"',
        "data-world-session-list",
        'data-world-session-revoke="others"',
        'data-world-session-revoke="all"',
        "session.ipAddress",
        "relativeTimeLabel(session.lastSeenAt)",
        "revokeWorldSession(",
        'method: "DELETE"',
        'role="status"',
        'aria-live="polite"',
    ):
        assert contract in WORLD
    # Session copy is account data rendered into innerHTML: every field is
    # escaped, and revoking this device tears the local session down too.
    assert "escapeHTML(session.deviceLabel)" in WORLD
    assert "escapeHTML(session.ipAddress" in WORLD
    assert "await this.logoutFromWorld();" in WORLD


def test_work_tab_shows_assigned_task_stats_with_start_stop():
    for contract in (
        "data-world-work-list",
        "data-world-work-total",
        "data-world-work-active",
        "data-world-work-tracked",
    ):
        assert contract in WORLD
    assert "this.officeTasks?.setPersonalView?.(open === true)" in WORLD
    for contract in (
        "function renderWorkPane()",
        "setPersonalView,",
        "ownTasks()",
        'data-world-office-task-action="${activeTask ? "stop" : "start"}"',
    ):
        assert contract in TASKS


def test_avatar_back_plate_carries_work_instead_of_the_session_card():
    assert "world.setSelfWorkBoard?.(" in TASKS
    assert "function avatarWorkBadgeTexture" in SCENE
    assert 'badge.name = "forkmesh-self-work-back-badge"' in SCENE
    assert "HIDDEN FROM PEERS + SCREENSHOTS" in SCENE
    # Built-in screenshots still hide the owner-only plate.
    assert "world.setSelfWorkBadgeVisibility?.(false)" in WORLD
    for retired in (
        "YOUR SESSION",
        "EDGE IP",
        "avatarSecurityBadgeTexture",
        "setSelfSecurityDetails",
    ):
        assert retired not in SCENE
        assert retired not in WORLD
