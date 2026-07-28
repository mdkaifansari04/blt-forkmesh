#!/usr/bin/env python3
"""Split repository work panels and compact exhibit layout."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")


def test_repository_issues_and_pulls_have_separate_left_right_panels():
    desk = SCENE[
        SCENE.index("function updateRepositoryRecordDesk("):
        SCENE.index("function setRepositoryIssuePageExpanded(")
    ]
    assert "const pullBoard = addRecordBoard(" in desk
    assert 'pulls,\n      "pull",\n      -7,' in desk
    assert 'issues,\n      "issue",\n      7,' in desk
    assert '"combined"' not in desk
    assert "repository-${kind}-open-count:" in desk


def test_repository_cards_clip_text_to_their_physical_bounds():
    issue = SCENE[
        SCENE.index("function repositoryIssueCardTexture"):
        SCENE.index("function repositoryPullCardTexture")
    ]
    pull = SCENE[
        SCENE.index("function repositoryPullCardTexture"):
        SCENE.index("const REPOSITORY_FOLLOWER_ACCENTS")
    ]
    assert issue.count("clipCanvasText") >= 2
    assert pull.count("clipCanvasText") >= 3


def test_counts_are_large_open_only_headers_and_paging_is_at_panel_bottom():
    assert "function repositoryRecordCountTexture(" in SCENE
    assert '"OPEN ISSUES"' in SCENE
    assert '"OPEN PRS"' in SCENE
    assert 'context.font = \'900 205px "ForkMesh Mono"' in SCENE
    assert 'String(item?.state || "").toLowerCase() === "open"' in SCENE
    assert "function repositoryRecordPageTexture(" in SCENE
    assert "groundY + 1.02" in SCENE
    assert "groundY + boardHeight + 2.04" not in SCENE


def test_repo_exhibit_has_angled_named_pedestal_and_live_agent_terminals():
    assert "repository-commit-activity-pedestal" in SCENE
    assert "backing.rotation.x = -Math.PI / 4;" in SCENE
    assert "repositoryCommitActivityTexture(" in SCENE
    assert "`${owner}/${name}`" in SCENE
    assert "repository-agent-control-dock:" in SCENE
    assert "function createRepositoryAgentTerminal(" in SCENE
    assert "screenShell.rotation.x = -Math.PI / 4;" in SCENE
    assert "repositoryAgentTasksByRepository" in SCENE
    assert "task.status === \"running\"" in SCENE
    assert "task.targetNode" in SCENE


def test_cabinet_faces_are_swapped_and_agent_sides_are_provider_specific():
    cabinet = SCENE[
        SCENE.index("function createMirrorServerCabinet"):
        SCENE.index("function createAgentRobot")
    ]
    agents = SCENE[
        SCENE.index("function mirrorAgentTaskPanelTexture"):
        SCENE.index("function createServedVisitorFigure")
    ]
    assert 'panel.position.set(0, 1.72, -0.735)' in cabinet
    assert 'rearPanel.position.set(0, 1.72, 0.735)' in cabinet
    assert 'const provider = side < 0 ? "claude-code" : "codex";' in cabinet
    assert "task.provider === providerLabel" in agents


def test_top_right_identity_control_uses_account_avatar_not_country_flag():
    assert "data-world-shirt-avatar" in APP
    assert "data-world-shirt-initial" in APP
    assert "data-world-shirt-flag" not in APP
    badge = CSS[
        CSS.index(".world-shirt-badge {"):
        CSS.index(".world-shirt-badge:hover")
    ]
    assert "width: 40px" in badge
    assert "height: 40px" in badge
    assert ".world-shirt-avatar" in CSS
    assert "object-fit: cover" in CSS
