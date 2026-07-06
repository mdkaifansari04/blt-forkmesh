#!/usr/bin/env python3
"""Consolidated /api/poll status digest remains available for compatibility,
but the dashboard no longer runs a periodic client poll."""
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


def test_dashboard_does_not_run_periodic_profile_or_notification_polling():
    assert "async function pollStatus(" not in DASHBOARD_JS
    assert "`/api/poll?node=${encodeURIComponent(node)}`" not in DASHBOARD_JS
    assert "function startProfileSync()" not in DASHBOARD_JS
    assert "setInterval(" not in DASHBOARD_JS
    assert "await refreshPublicProfile(session);" in DASHBOARD_JS
    assert "await loadNotifications();" in DASHBOARD_JS
