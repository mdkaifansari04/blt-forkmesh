#!/usr/bin/env python3
"""Static website repo Mirrors tab contract tests."""

from pathlib import Path


from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js

PUBLIC = Path(__file__).resolve().parents[1] / "public"


DASHBOARD = assembled_dashboard()
DASHBOARD_JS = assembled_dashboard_js()
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


def test_live_mirror_tab_rows_are_a_single_non_wrapping_line():
    rows = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorTabRow(")
        : DASHBOARD_JS.index("function renderMirrorRow(")
    ]
    assert "flex min-w-max flex-nowrap items-center" in rows
    assert "whitespace-nowrap" in rows
    assert "mirrorDetailChips(mirror, refMirror)" in rows
    assert "mt-2 flex flex-wrap" not in rows

    mirror_list = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoMirrorLists(")
        : DASHBOARD_JS.index("const REPO_MIRROR_MIN_REFRESH_MS")
    ]
    assert '<div class="overflow-x-auto">' in mirror_list


def test_long_mirror_capabilities_use_a_mouseover_list():
    chips = DASHBOARD_JS[
        DASHBOARD_JS.index("function mirrorListChip(")
        : DASHBOARD_JS.index("function mirrorCountText(")
    ]
    assert r'list.map((value) => `\u2022 ${value}`).join("\n")' in chips
    assert 'title="${escapeHtml(tooltip)}"' in chips
    assert 'tabindex="0"' in chips
    assert 'aria-label="${escapeHtml(`${label}: ${list.join(", ")}`)}"' in chips
    assert 'mirrorListChip("Capabilities", operations)' in chips
