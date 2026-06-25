#!/usr/bin/env python3
"""Static website repo Mirrors tab contract tests."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
INDEX = (PUBLIC / "index.html").read_text(encoding="utf-8")
CATALOG = (PUBLIC / "catalog.js").read_text(encoding="utf-8")
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")


def test_repo_tab_is_named_mirrors_and_has_table_shell():
    assert 'id="tab-mirrors"' in INDEX
    assert ">Mirrors" in INDEX
    assert 'id="repo-mirrors"' in INDEX
    assert 'id="mirror-summary"' in INDEX
    assert 'id="mirror-list"' in INDEX
    for heading in ("Node", "Status", "Last seen", "Hosted since", "Sync age", "Clone available"):
        assert heading in INDEX


def test_catalog_js_loads_mirrors_endpoint_lazily():
    assert "tabMirrorsEl" in CATALOG
    assert "loadMirrors(" in CATALOG
    assert "/mirrors" in CATALOG
    assert 'showRepoTab("mirrors")' in CATALOG
    assert "mirrorsLoadedFor" in CATALOG


def test_catalog_js_marks_mirrors_loaded_only_after_success():
    show_tab = CATALOG[
        CATALOG.index('if (tab === "mirrors"')
        : CATALOG.index("function formatRelativeMs")
    ]
    load_mirrors = CATALOG[
        CATALOG.index("async function loadMirrors")
        : CATALOG.index("if (tabCodeEl)")
    ]

    assert "mirrorsLoadedFor = repoKey;" not in show_tab
    assert "mirrorsLoadedFor = `${owner}/${name}`;" in load_mirrors
    assert "mirrorsLoadedFor = \"\";" in load_mirrors


def test_catalog_js_clears_mirrors_summary_before_fetching_new_repo():
    load_mirrors = CATALOG[
        CATALOG.index("async function loadMirrors")
        : CATALOG.index("if (tabCodeEl)")
    ]

    assert 'mirrorListEl.innerHTML = `<tr><td colspan="6">Loading mirror health...</td></tr>`;' in load_mirrors
    assert "if (mirrorSummaryEl) mirrorSummaryEl.replaceChildren();" in load_mirrors
    assert load_mirrors.index("if (mirrorSummaryEl) mirrorSummaryEl.replaceChildren();") < load_mirrors.index("try {")


def test_mirrors_css_has_responsive_table_hooks():
    assert ".mirror-summary-grid" in STYLES
    assert ".mirror-table-wrap" in STYLES
    assert ".mirror-health-table" in STYLES
    assert "@media (max-width: 700px)" in STYLES
