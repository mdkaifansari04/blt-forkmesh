#!/usr/bin/env python3
"""Header "Reconnect node" affordance next to the profile avatar.

A signed-in user account with no linked node has nothing authenticated to drain
its issues/chats/etc. to a desktop, so the header surfaces a re-link shortcut to
the Settings > Nodes flow. It stays hidden for guests, node sessions, and when
the link state is unknown (so it never false-alarms).
"""

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js


DASHBOARD = assembled_dashboard()
DASHBOARD_JS = assembled_dashboard_js()


def test_header_has_hidden_reconnect_button_by_the_avatar():
    # Rendered right before the profile-settings avatar button, hidden until the
    # profile renderer decides a node link is missing.
    assert "data-node-reconnect" in DASHBOARD
    reconnect = DASHBOARD[
        DASHBOARD.index("data-node-reconnect")
        : DASHBOARD.index("data-profile-settings-button")
    ]
    assert 'href="/dashboard/settings"' in reconnect
    assert "hidden" in reconnect
    assert "Reconnect node" in reconnect


def test_reconnect_visibility_gated_on_missing_linked_node():
    gate = DASHBOARD_JS[
        DASHBOARD_JS.index("function nodeNeedsReconnect")
        : DASHBOARD_JS.index("function renderProfile")
    ]
    # Guests / node sessions never see it.
    assert 'if (!signedIn || session.kind === "node") return false;' in gate
    # Only when the account affirmatively has zero linked nodes (present + empty).
    assert "Array.isArray(session.nodes) && session.nodes.length === 0" in gate


def test_render_profile_toggles_the_reconnect_button():
    assert '$("[data-node-reconnect]")' in DASHBOARD_JS
    assert 'reconnect.classList.toggle("hidden", !nodeNeedsReconnect(session));' in DASHBOARD_JS
