#!/usr/bin/env python3
"""Static website repo Mirrors tab contract tests."""

from pathlib import Path


from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js

PUBLIC = Path(__file__).resolve().parents[1] / "public"
WWW_PUBLIC = PUBLIC.parents[1] / "www" / "public"
# The dashboard shell is split into HTML partials and prebuilt before deploy;
# assert against the assembled document a browser actually receives.
DASHBOARD = assembled_dashboard()
DASHBOARD_JS = assembled_dashboard_js()
STYLES = (WWW_PUBLIC / "styles.css").read_text(encoding="utf-8")


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


def test_live_mirror_tab_uses_compact_borderless_table():
    rows = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorTabRow(")
        : DASHBOARD_JS.index("function renderMirrorRow(")
    ]
    assert "data-mirror-row" in rows
    assert '<tr data-mirror-row' in rows
    assert "max-w-28 truncate whitespace-nowrap px-1 py-1" in rows
    assert "<article" not in rows
    assert "border" not in rows

    headers = DASHBOARD_JS[
        DASHBOARD_JS.index("function mirrorTableHeader(")
        : DASHBOARD_JS.index("function renderMirrorTabRow(")
    ]
    assert '<th scope="col"' in headers
    assert 'class="inline-block h-3 w-3"' in headers
    assert '<span class="sr-only">${escapeHtml(label)}</span>' in headers
    for heading in (
        "Node",
        "Status",
        "Revision",
        "Last synced",
        "Endpoint",
        "Issues",
        "Commits",
        "Clones served",
        "Website requests",
    ):
        assert f'"{heading}"' in headers

    mirror_list = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoMirrorLists(")
        : DASHBOARD_JS.index("const REPO_MIRROR_MIN_REFRESH_MS")
    ]
    assert "data-mirror-table-wrap" in mirror_list
    assert "data-mirror-table" in mirror_list
    assert "border-separate border-spacing-0 whitespace-nowrap" in mirror_list
    assert "mirrorTableHead()" in mirror_list
    assert "data-mirror-card" not in mirror_list


def test_live_mirror_table_keeps_dense_values_in_tooltips():
    rows = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorTabRow(")
        : DASHBOARD_JS.index("function renderMirrorRow(")
    ]
    assert 'const capabilityTitle = operations.length' in rows
    assert 'Capabilities: ${operations.join(", ")}' in rows
    assert "formatCount(operations.length)" in rows
    assert "Source of truth is at" in rows


def test_mirror_page_shell_has_no_outer_margin_or_border():
    panel = DASHBOARD_JS[
        DASHBOARD_JS.index('<section data-dashboard-repo-tab-panel="mirrors"')
        : DASHBOARD_JS.index('<section data-dashboard-repo-tab-panel="agents"')
    ]
    assert 'class="hidden"><div data-mirror-request' in panel
    assert "mt-4" not in panel
    assert "border" not in panel
    assert "rounded-lg" not in panel


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
