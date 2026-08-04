#!/usr/bin/env python3
"""Static website repo Mirrors tab contract tests."""

from pathlib import Path


from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js

PUBLIC = Path(__file__).resolve().parents[1] / "public"
# The dashboard shell is split into HTML partials and prebuilt before deploy;
# assert against the assembled document a browser actually receives.
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


def test_live_mirror_tab_uses_responsive_summary_and_cards():
    rows = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorTabRow(")
        : DASHBOARD_JS.index("function renderMirrorNetworkSummary(")
    ]
    assert "data-mirror-card" in rows
    assert "min-w-0 overflow-hidden rounded-lg" in rows
    assert "mirrorDetailGroups(mirror, refMirror)" in rows
    assert "min-w-max" not in rows
    assert "whitespace-nowrap" not in rows

    summary = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorNetworkSummary(")
        : DASHBOARD_JS.index("function renderMirrorRow(")
    ]
    assert "data-mirror-summary" in summary
    assert "Registered nodes" in summary
    assert "Online now" in summary
    assert "revision mismatch" in summary

    mirror_list = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoMirrorLists(")
        : DASHBOARD_JS.index("const REPO_MIRROR_MIN_REFRESH_MS")
    ]
    assert "renderMirrorNetworkSummary(ordered, refMirror)" in mirror_list
    assert 'data-mirror-card-grid class="grid gap-3 p-3 md:grid-cols-2"' in mirror_list
    assert "overflow-x-auto" not in mirror_list


def test_live_mirror_details_have_scannable_sections():
    details = DASHBOARD_JS[
        DASHBOARD_JS.index("function mirrorChip(")
        : DASHBOARD_JS.index("function mirrorCountText(")
    ]
    for heading in (
        "Revision",
        "Connection",
        "Repository contents",
        "Local activity",
    ):
        assert f'mirrorDetailGroup("{heading}"' in details
    assert "sm:grid-cols-2" in details
    assert "max-w-full" in details


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


def test_mirrors_action_badge_uses_the_live_membership_list():
    loader = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoMirrors(")
        : DASHBOARD_JS.index("function repoMirrorsTabVisible(")
    ]

    # The endpoint's summary can lag the returned node records. The rail badge
    # must agree with the list rendered in the Mirrors panel.
    assert "const mirrorCount = mirrors.length;" in loader
    assert "data.summary?.mirrors" not in loader
    assert "updateRepoLiveCounts(repo, { mirrors: mirrorCount });" in loader
    assert 'setRepoTabCount("mirrors", mirrorCount);' in loader
