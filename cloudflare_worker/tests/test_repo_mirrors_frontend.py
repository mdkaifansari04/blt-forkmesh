#!/usr/bin/env python3
"""Static website repo Mirrors tab contract tests."""

from pathlib import Path


from _dashboard_shell import assembled_dashboard

PUBLIC = Path(__file__).resolve().parents[1] / "public"
# The dashboard shell is split into HTML partials the Worker composes at request
# time; assert against the assembled document a browser actually receives.
DASHBOARD = assembled_dashboard()
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")


def test_repo_tab_is_named_mirrors_and_has_table_shell():
    assert 'id="tab-mirrors"' in DASHBOARD
    assert ">Mirrors" in DASHBOARD
    assert 'id="repo-mirrors"' in DASHBOARD
    assert 'id="mirror-summary"' in DASHBOARD
    assert 'id="mirror-list"' in DASHBOARD
    for heading in ("Node", "Status", "Last seen", "Hosted since", "Sync age", "Clone available"):
        assert heading in DASHBOARD


def test_mirrors_css_has_responsive_table_hooks():
    assert ".mirror-summary-grid" in STYLES
    assert ".mirror-table-wrap" in STYLES
    assert ".mirror-health-table" in STYLES
    assert "@media (max-width: 700px)" in STYLES
