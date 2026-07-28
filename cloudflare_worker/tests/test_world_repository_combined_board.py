#!/usr/bin/env python3
"""Combined repository work list and cabinet-side layout."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")


def test_repository_issues_and_pulls_share_one_paginated_board():
    assert 'recordKind: "issue"' in SCENE
    assert 'recordKind: "pull"' in SCENE
    assert '"combined"' in SCENE
    assert "ISSUES  ·  ${pullCount} PULL REQUESTS" in SCENE
    assert "ONE LIVE WORK LIST" in SCENE
    assert 'addRecordBoard(pulls, "pull"' not in SCENE
    assert 'addRecordBoard(issues, "issue"' not in SCENE


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
