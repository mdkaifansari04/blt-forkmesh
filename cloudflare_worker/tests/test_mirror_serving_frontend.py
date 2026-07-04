#!/usr/bin/env python3
"""Live dashboard surfaces in-place mirror serving (adhoc #61): a repo whose own
host is down but whose mirror is online must read as available ("served by
mirror"), not "host offline", on both the repo card and the repo detail page."""

from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


PUBLIC = Path(__file__).resolve().parents[1] / "public"
# dashboard.js is split into ordered public/dashboard/js/*.js fragments composed
# into one /dashboard.js by the Worker (see src/dashboard_bundle.py).
DASHBOARD_JS = assembled_dashboard_js()


def test_dashboard_defines_group_liveness_helpers():
    # The availability verdict comes from the worker's group-liveness flag
    # (cloneOnline), falling back to liveHost for older payloads.
    assert "function repoIsLive(repo)" in DASHBOARD_JS
    assert "function repoServedByMirror(repo)" in DASHBOARD_JS
    assert "repo?.cloneOnline ?? repo?.liveHost" in DASHBOARD_JS


def test_repo_card_uses_group_liveness_not_raw_livehost():
    card = DASHBOARD_JS[
        DASHBOARD_JS.index("function repositoryCard(group)")
        : DASHBOARD_JS.index("function updateRepositoryPagination(")
    ]
    assert "const live = repoIsLive(repo);" in card
    assert "const viaMirror = repoServedByMirror(repo);" in card
    # A repo served by a live mirror reads as "via mirror" / "served by mirror",
    # never a flat "offline".
    assert "viaMirror ? \"via mirror\"" in card
    assert "viaMirror ? \"served by mirror\"" in card


def test_repo_card_groups_mirrors_under_source_of_truth():
    # The list shows one card per logical repo (grouped by root commit, name
    # fallback), named after the source of truth (the node that published from a
    # local working copy, source === "local-node"), not each mirror separately.
    assert "function groupRepositories(" in DASHBOARD_JS
    source_of_truth = DASHBOARD_JS[
        DASHBOARD_JS.index("function sourceOfTruth(group)")
        : DASHBOARD_JS.index("function sourceOfTruth(group)") + 200
    ]
    assert '(m.source || "").trim() === "local-node"' in source_of_truth
    assert "|| group.primary" in source_of_truth

    card = DASHBOARD_JS[
        DASHBOARD_JS.index("function repositoryCard(group)")
        : DASHBOARD_JS.index("function updateRepositoryPagination(")
    ]
    # Title, clone command, and Browse target all name the source of truth.
    assert "const origin = sourceOfTruth(group);" in card
    assert "const key = repoKey(origin);" in card
    assert 'escapeHtml(origin.owner || "owner")' in card
    assert 'escapeHtml(origin.name || "repository")' in card
    assert "cloneUrl(origin)" in card
    # A grouped repo advertises how many nodes mirror it.
    assert "const nodeCount = group.members.length;" in card
    assert "${nodeCount} nodes" in card


def test_repo_list_paginates_grouped_repos_not_raw_mirrors():
    pager = DASHBOARD_JS[
        DASHBOARD_JS.index("function updateRepositoryPagination(")
        : DASHBOARD_JS.index("function updateRepositoryPagination(") + 800
    ]
    # Pagination and the count summary operate on grouped logical repos.
    assert "const total = state.filteredGroups.length;" in pager
    assert "const visible = state.filteredGroups.slice(start, end);" in pager


def test_repo_detail_reflects_mirror_serving():
    detail = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoDetail(repo)")
        : DASHBOARD_JS.index("function findRepository(")
    ]
    assert "const live = repoIsLive(repo);" in detail
    assert "const viaMirror = repoServedByMirror(repo);" in detail
    # Header badge, Host fact, and Clone availability all key off the group verdict.
    assert "viaMirror ? \"served by mirror\" : live ? \"host online\" : \"host offline\"" in detail
    assert "viaMirror ? \"via mirror\" : live ? \"online\" : \"offline\"" in detail
    assert "viaMirror ? \"via mirror\" : live ? \"available\" : \"offline\"" in detail
