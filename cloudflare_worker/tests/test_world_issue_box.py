#!/usr/bin/env python3
"""Contracts for the in-world issue box and pull-request review desk.

Beside the selected repository portal the scene stands an open crate of issue
pages (click one to expand it, click the expanded sheet to open the signed
issue thread) and a board of pull-request cards that route straight into the
existing exact-ref diff review. Both are projections of records the shell
already verified — the scene never invents an issue or pull number and never
fetches anything of its own.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "public" / "world"
APP = (WORLD / "world.js").read_text(encoding="utf-8")
SCENE = (WORLD / "world-scene.js").read_text(encoding="utf-8")

DESK = SCENE[
    SCENE.index("function updateRepositoryRecordDesk("):
    SCENE.index("function updateRepositoryGraph(")
]


def test_scene_builds_a_bounded_desk_from_verified_records_only():
    assert "REPOSITORY_ISSUE_PAGES_VISIBLE = 10" in SCENE
    assert "REPOSITORY_PULL_CARDS_VISIBLE = 5" in SCENE
    assert "number >= 1 && number <= 10_000_000" in SCENE
    assert 'layer.name = "repository-record-desk"' in DESK
    assert "repository-issue-box:" in DESK
    assert "repository-pull-desk:" in DESK
    assert "repository-issue-page-expanded:" in DESK
    # Pure projection: no network, no storage, no invented records.
    assert "fetch" not in DESK
    assert "localStorage" not in DESK
    assert "sessionStorage" not in DESK
    assert ".slice(0, REPOSITORY_ISSUE_PAGES_VISIBLE)" in DESK
    assert ".slice(0, REPOSITORY_PULL_CARDS_VISIBLE)" in DESK


def test_desk_layer_is_removed_on_rebuild_and_catalog_replacement():
    # Once inside updateRepositoryRecordDesk, once when the portal catalog
    # layer that mounts the desk is torn down.
    assert DESK.count("world.userData.repositoryRecordDeskLayer = null") == 1
    assert SCENE.count("world.userData.repositoryRecordDeskLayer = null") >= 2
    assert "removeGeneratedLayer(previousMount, previousLayer, interactive)" in DESK
    assert "setRepositoryIssuePageExpanded" in SCENE
    assert SCENE.count("updateRepositoryRecordDesk,") == 1
    assert SCENE.count("setRepositoryIssuePageExpanded,") == 1


def test_pick_handler_reports_pages_without_stealing_landmark_focus():
    assert "hit.object.userData.repositoryIssuePage" in SCENE
    assert "hit.object.userData.repositoryPullPage" in SCENE
    assert "!repositoryIssuePage &&" in SCENE
    assert "!repositoryPullPage" in SCENE


def test_shell_expands_issue_pages_and_opens_the_thread_on_second_click():
    assert "this.toggleRepositoryIssuePage(meta.repositoryIssuePage)" in APP
    toggle = APP[
        APP.index("  toggleRepositoryIssuePage("):
        APP.index("  selectRepositorySizeNode(")
    ]
    # The page must belong to the active repository before anything happens.
    assert "active.owner.toLocaleLowerCase()" in toggle
    assert "active.repo.toLocaleLowerCase()" in toggle
    assert "safePullNumber(page?.number)" in toggle
    # First click expands in-scene; the second click opens the signed thread
    # in a new tab with the same noopener discipline as the portal base link.
    assert "setRepositoryIssuePageExpanded" in toggle
    assert '/issues/${number}' in toggle
    assert '"_blank", "noopener,noreferrer"' in toggle


def test_shell_routes_pull_cards_into_the_exact_ref_diff_review():
    assert "this.loadRepositoryPullReview(meta.repositoryPullPage.number)" in APP
    # The desk is fed the same commit-matched records as the explorer panel.
    assert "pulls: this.repositoryPullRecords(active)" in APP
    assert "expandedIssue: this.expandedRepositoryIssuePage" in APP
    assert 'this.world.updateRepositoryRecordDesk?.({}, {})' in APP
    # Expansion state resets whenever a repository map is (re)loaded.
    assert APP.count("this.expandedRepositoryIssuePage = 0;") >= 3
