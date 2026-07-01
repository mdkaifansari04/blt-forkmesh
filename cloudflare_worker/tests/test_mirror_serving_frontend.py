#!/usr/bin/env python3
"""Live dashboard surfaces in-place mirror serving (adhoc #61): a repo whose own
host is down but whose mirror is online must read as available ("served by
mirror"), not "host offline", on both the repo card and the repo detail page."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
DASHBOARD_JS = (PUBLIC / "dashboard.js").read_text(encoding="utf-8")


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
