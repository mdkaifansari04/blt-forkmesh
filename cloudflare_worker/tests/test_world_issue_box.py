#!/usr/bin/env python3
"""Contracts for the in-world issue and pull-request work panels.

Beside the selected repository portal the scene stands independent left PR and
right issue panels, each with up to 25 cards and its own bottom pagination.
Issue records can expand before opening the signed thread; pull records route
into exact-ref diff review. Both project records the shell already verified.
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
    assert "const pullBoard = addRecordBoard(" in DESK
    assert 'pulls,\n      "pull",\n      -7.75,' in DESK
    assert 'issues,\n      "issue",\n      7.75,' in DESK
    assert "const rows = Math.max(1, allItems.length);" in DESK
    assert "const towerSlot = pageInfo.start + index;" in DESK
    assert "new THREE.InstancedMesh(" in DESK
    assert "const cardX = 0;" in DESK
    assert '"combined"' not in DESK
    assert "items.length > 13 ? 2 : 1" not in DESK
    assert "repository-issue-page-expanded:" in DESK

    assert "fetch" not in DESK
    assert "localStorage" not in DESK
    assert "sessionStorage" not in DESK
    assert "repositoryRecordPage(kind, allItems.length)" in DESK
    assert ".slice(pageInfo.start, pageInfo.end)" in DESK
    assert "repositoryRecordPageTexture(THREE, pageInfo, accent)" in DESK


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
    issue_workbench = APP[
        APP.index("  openRepositoryIssueWorkbench("):
        APP.index("  openRepositoryRecordWebWorkbench(")
    ]
    assert 'this.openRepositoryRecordWebWorkbench("issue", page)' in issue_workbench
    workbench = APP[
        APP.index("  openRepositoryRecordWebWorkbench("):
        APP.index("  selectRepositorySizeNode(")
    ]
    assert "safePullNumber(page.number)" in workbench
    assert '"pulls" : "issues"' in workbench
    assert "data-world-repository-web-workbench" in workbench
    assert "<iframe" in workbench


def test_shell_routes_pull_cards_into_the_canonical_web_workbench():
    assert "this.openRepositoryPullWorkbench(meta.repositoryPullPage)" in APP
    assert "this.openRepositoryPullWorkbench({" in APP
    pull_workbench = APP[
        APP.index("  openRepositoryPullWorkbench("):
        APP.index("  async fetchRepositoryPullReview(")
    ]
    assert 'this.openRepositoryRecordWebWorkbench("pull"' in pull_workbench
    generic = APP[
        APP.index("  openRepositoryRecordWebWorkbench("):
        APP.index("  selectRepositorySizeNode(")
    ]
    assert 'detail.dataset.openLandmark = `repository-${recordKind}`' in generic
    assert '"pulls" : "issues"' in generic
    assert "Repository portals" not in generic

    assert "pulls: this.repositoryPullRecords(active)" in APP
    assert "expandedIssue: this.expandedRepositoryIssuePage" in APP
    assert 'this.world.updateRepositoryRecordDesk?.({}, {})' in APP

    assert APP.count("this.expandedRepositoryIssuePage = 0;") >= 3
