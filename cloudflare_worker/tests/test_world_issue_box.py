#!/usr/bin/env python3
"""Contracts for the in-world issue and pull-request review boards.

Beside the selected repository portal the scene stands matching, two-column
review boards with up to 25 issue and pull-request cards. Issue records can
expand before opening the signed thread; pull records route into exact-ref diff
review. Both are projections of records the shell already verified.
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
    assert "REPOSITORY_ISSUE_CARDS_VISIBLE = 25" in SCENE
    assert "REPOSITORY_PULL_CARDS_VISIBLE = 25" in SCENE
    assert "number >= 1 && number <= 10_000_000" in SCENE
    assert 'layer.name = "repository-record-desk"' in DESK
    assert "addRecordBoard(issues, \"issue\"" in DESK
    assert "addRecordBoard(pulls, \"pull\"" in DESK
    assert "const columns = items.length > 13 ? 2 : 1;" in DESK
    assert "repository-issue-page-expanded:" in DESK
    # Pure projection: no network, no storage, no invented records.
    assert "fetch" not in DESK
    assert "localStorage" not in DESK
    assert "sessionStorage" not in DESK
    assert "repositoryRecordPage(kind, allItems.length)" in DESK
    assert "allItems.slice(pageInfo.start, pageInfo.end)" in DESK


def test_issue_and_pull_cards_show_commit_pinned_metadata():
    assert "function repositoryIssueCardTexture(" in SCENE
    assert "function repositoryPullCardTexture(" in SCENE
    for field in ("issue.title", "issue.author", "issue.labels",
                  "issue.assignees", "issue.metadataAvailable"):
        assert field.replace("pull.", "pull?.") in SCENE
    for field in ("pull.title", "pull.author", "pull.head", "pull.base",
                  "pull.createdAt", "pull.metadataAvailable"):
        assert field in SCENE
    loader = APP[
        APP.index("  async loadRepositoryEntityRecords("):
        APP.index("  flagshipCatalogCommits(")
    ]
    assert "selectedIssues" in loader
    assert "this.loadRepositoryBlobBatches(" in loader
    assert "base,\n          commit,\n          issuePaths," in loader
    assert "issue metadata batch commit mismatch" in loader
    assert "record.metadataAvailable = true" in loader


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


def test_open_issue_cards_have_engineering_agent_and_model_controls():
    for marker in (
        "repositoryIssueAgentProviderTexture",
        "repositoryIssueAgentModelTexture",
        "repositoryIssueAgentProvider",
        "repositoryIssueAgentAssignment",
        '["haiku", "sonnet", "opus", "fable"]',
        '["sol", "luna", "terra"]',
        "setRepositoryIssueAgentPicker",
    ):
        assert marker in SCENE
    assert "selectRepositoryIssueAgentProvider" in APP
    assert "assignRepositoryIssueToAgent" in APP
    assert "Only Engineering team members can assign issues" in APP
    assert "Haiku security review runs first" in APP


def test_shell_opens_issue_cards_in_the_live_world_workbench():
    assert "this.openRepositoryIssueWorkbench(meta.repositoryIssuePage)" in APP
    workbench = APP[
        APP.index("  openRepositoryIssueWorkbench("):
        APP.index("  selectRepositorySizeNode(")
    ]
    assert "safePullNumber(page?.number)" in workbench
    assert '/issues/${number}' in workbench
    assert "data-world-issue-workbench" in workbench
    assert "<iframe" in workbench


def test_shell_routes_pull_cards_into_the_exact_ref_diff_review():
    assert "this.loadRepositoryPullReview(meta.repositoryPullPage.number)" in APP
    # The desk is fed the same commit-matched records as the explorer panel.
    assert "pulls: this.repositoryPullRecords(active)" in APP
    assert "expandedIssue: this.expandedRepositoryIssuePage" in APP
    assert 'this.world.updateRepositoryRecordDesk?.({}, {})' in APP
    # Expansion state resets whenever a repository map is (re)loaded.
    assert APP.count("this.expandedRepositoryIssuePage = 0;") >= 3
