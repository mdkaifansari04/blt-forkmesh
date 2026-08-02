#!/usr/bin/env python3
"""Operational status alerts are available in the encrypted Pings inbox."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def test_operational_pings_are_a_first_class_default_on_channel():
    defaults = ENTRY_TEXT.split("REPO_ALERT_SETTING_DEFAULTS = {", 1)[1] \
        .split("}", 1)[0]
    kinds = ENTRY_TEXT.split("NOTIFICATION_KINDS = frozenset({", 1)[1] \
        .split("})", 1)[0]
    assert '"statusPings": True' in defaults
    assert '"statusEmails": False' in defaults
    assert '"operational_alert"' in kinds
    assert "async def _enqueue_operational_alert_pings" in ENTRY_TEXT


def test_both_operational_monitor_paths_enqueue_pings():
    watchdog = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _send_cron_watchdog_email"):
        ENTRY_TEXT.index("async def _cron_watchdog_completion")
    ]
    transitions = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _record_status_monitor_transitions"):
        ENTRY_TEXT.index("def _status_expected_checks_for_hour")
    ]
    assert "await _enqueue_operational_alert_pings(env, [alert], now)" in watchdog
    assert (
        "await _enqueue_operational_alert_pings(env, ping_pending, now)"
        in transitions)
    assert 'href="/status"' in ENTRY_TEXT
    assert 'source="operational-status"' in ENTRY_TEXT


def test_ping_channel_tracks_its_own_delivered_transition():
    """Outage pings must not be crowded out by repeated recovery pings.

    Pings default on and mail defaults off, so a ping-only deployment gets no
    notified_state updates. Without an independent marker every later sample
    re-announced the last transition, and recoveries keyed off a stamp that
    moved each minute, so duplicated "recovered" rows trimmed the outage pings
    out of the capped inbox.
    """
    transitions = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _record_status_monitor_transitions"):
        ENTRY_TEXT.index("def _status_expected_checks_for_hour")
    ]
    enqueue = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _enqueue_operational_alert_pings"):
        ENTRY_TEXT.index("async def _repository_monitor_admin_emails")
    ]
    assert "pinged_state TEXT NOT NULL DEFAULT ''" in ENTRY_TEXT
    assert 'prior_pinged != "down"' in transitions
    assert "should_ping = pinged != state" in transitions
    assert "await _record_operational_alert_pings_sent(env, ping_pending)" \
        in transitions
    assert 'alert.get("changed_at")' in enqueue
    assert "UPDATE repository_monitor_state SET pinged_state=?" in ENTRY_TEXT


def test_admin_setting_exposes_independent_ping_and_email_controls():
    renderer = ENTRY_TEXT[
        ENTRY_TEXT.index("def _render_admin_operational_alerts"):
        ENTRY_TEXT.index("def _render_admin_nav")
    ]
    handler = ENTRY_TEXT[
        ENTRY_TEXT.index('elif action == "set_operational_alerts"'):
        ENTRY_TEXT.index('elif action == "set_repo_terms_flag"')
    ]
    assert 'name="pings_enabled"' in renderer
    assert 'name="email_enabled"' in renderer
    assert 'settings.get("statusPings", True)' in renderer
    assert 'updated_alerts["statusPings"] = pings_enabled' in handler
    assert 'updated_alerts["statusEmails"] = email_enabled' in handler
