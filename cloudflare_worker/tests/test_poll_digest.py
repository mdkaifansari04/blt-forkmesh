#!/usr/bin/env python3
"""Consolidated /api/poll status digest: one lightweight call the dashboard
polls each tick instead of separately re-fetching the full profile and the
full notification list, firing the heavier fetches only when a token moved."""
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
# dashboard.js is split into ordered public/dashboard/js/*.js fragments composed
# into one /dashboard.js by the Worker (see src/dashboard_bundle.py).
DASHBOARD_JS = assembled_dashboard_js()


def test_worker_registers_poll_route_and_handler():
    assert 'url.path in ("/api/poll", "/api/poll/")' in ENTRY_TEXT
    assert "return await poll_handler(self.env, request)" in ENTRY_TEXT
    assert "async def poll_handler(env, request):" in ENTRY_TEXT


def test_poll_handler_returns_cheap_change_tokens():
    # Grab just the handler body.
    start = ENTRY_TEXT.index("async def poll_handler(env, request):")
    body = ENTRY_TEXT[start:start + 2600]
    # Build rev + profile + notification digests, all in one response.
    assert '"rev": _build_rev(env)' in body
    assert '"profile"' in body
    assert '"notif"' in body
    # Notifications are summarised with an aggregate query, NOT by decrypting
    # every row (that was the per-request cost we are removing from the tick).
    assert "COUNT(*)" in body and "MAX(ts)" in body
    assert "decrypt_row" not in body


def test_version_endpoint_shares_build_rev_helper():
    assert "def _build_rev(env):" in ENTRY_TEXT
    assert '"rev": _build_rev(self.env)' in ENTRY_TEXT


def test_dashboard_polls_once_and_gates_heavy_fetches_on_change():
    assert "async function pollStatus(" in DASHBOARD_JS
    assert "`/api/poll?node=${encodeURIComponent(node)}`" in DASHBOARD_JS
    # The 60s sync tick now drives the lightweight poll, not the full profile
    # fetch directly.
    sync = DASHBOARD_JS[
        DASHBOARD_JS.index("function startProfileSync()")
        : DASHBOARD_JS.index("function startProfileSync()") + 400
    ]
    assert "pollStatus()" in sync
    assert "refreshPublicProfile(state.session)" not in sync
    # Heavy fetches only fire when the corresponding token changed.
    poll = DASHBOARD_JS[
        DASHBOARD_JS.index("async function pollStatus(")
        : DASHBOARD_JS.index("function startProfileSync()")
    ]
    assert "state.pollProfileToken" in poll
    assert "state.pollNotifToken" in poll
    assert "await refreshPublicProfile(state.session)" in poll
    assert "await loadNotifications()" in poll
    # Unread count rides along so the badge updates from the poll alone.
    assert "state.notificationUnread = notif.unread" in poll
