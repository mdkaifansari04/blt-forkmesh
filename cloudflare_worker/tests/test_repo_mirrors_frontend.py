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


def test_catalog_js_groups_empty_root_mirror_with_its_source():
    # The website must apply the same name fallback the worker's
    # repo_mirror_same_group uses, so a relay-cloned mirror with an empty
    # rootCommit (issue #243) collapses onto its rooted source of truth instead
    # of a separate card/list. Without this the source never shows its mirrors.
    assert "function sameRepoGroup(" in CATALOG
    same_group = CATALOG[
        CATALOG.index("function sameRepoGroup(")
        : CATALOG.index("function groupRepositories(")
    ]
    # Pairwise: match on shared root, else fall back to a case-insensitive name.
    assert "if (ra && rb) return ra === rb;" in same_group
    assert "na === nb" in same_group
    # groupRepositories folds empty-root, name-keyed groups into a rooted group.
    group_repos = CATALOG[
        CATALOG.index("function groupRepositories(")
        : CATALOG.index("function text(")
    ]
    assert "rootedByName" in group_repos
    # The detail-page node list groups pairwise, not by an exact key match.
    render_nodes = CATALOG[
        CATALOG.index("function renderMirrorNodes(")
        : CATALOG.index("function renderMirrorNodes(") + 600
    ]
    assert "sameRepoGroup(ref, r)" in render_nodes
    assert "repoGroupKey(r) === key" not in render_nodes
