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
    assert 'pulls,\n      "pull",\n      -7.75,' in desk
    assert 'issues,\n      "issue",\n      7.75,' in desk
    assert '"combined"' not in desk
    assert "repository-${kind}-open-count:" in desk


def test_each_repository_panel_uses_one_total_count_height_record_column():
    desk = SCENE[
        SCENE.index("function updateRepositoryRecordDesk("):
        SCENE.index("function setRepositoryIssuePageExpanded(")
    ]
    assert "const rowPitch = 0.52;" in desk
    assert "const boardHeight = 0.74 + rows * rowPitch;" in desk
    assert "const rows = Math.max(1, allItems.length);" in desk
    assert "const towerSlot = pageInfo.start + index;" in desk
    assert "repository-${kind}-tower-slots:" in desk
    assert "const cardX = 0;" in desk
    assert "new THREE.PlaneGeometry(3.78, 0.46)" in desk
    assert "items.length > 13 ? 2 : 1" not in desk


def test_repository_lists_read_chronologically_downward_with_counts_below():
    desk = SCENE[
        SCENE.index("function updateRepositoryRecordDesk("):
        SCENE.index("function setRepositoryIssuePageExpanded(")
    ]
    assert ".slice(pageInfo.start, pageInfo.end)" in desk
    assert ".reverse();" not in desk
    assert "right.updatedAt || right.createdAt || right.number" in desk
    assert "right.createdAt || right.number" in desk
    assert "const boardBottomY = groundY + 2.62;" in desk
    assert "groundY + 1.75" in desk
    assert "groundY + 0.7" in desk
    assert "groundY + boardHeight + 1.72" not in desk


def test_repository_panels_do_not_add_ground_view_placards():
    desk = SCENE[
        SCENE.index("function updateRepositoryRecordDesk("):
        SCENE.index("function setRepositoryIssuePageExpanded(")
    ]
    assert "repository-${kind}-viewing-pad:" not in desk
    assert '"FIRST PERSON"' not in desk
    assert "repositoryViewingPad" not in SCENE


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
    assert "groundY + 1.75" in SCENE
    assert "groundY + 0.7" in SCENE
    assert "groundY + boardHeight + 2.04" not in SCENE


def test_repo_exhibit_has_angled_named_pedestal_and_live_agent_terminals():
    assert "repository-commit-activity-pedestal" in SCENE
    assert "backing.rotation.x = -Math.PI / 4;" in SCENE
    assert "repositoryCommitActivityTexture(" in SCENE
    assert "`${owner}/${name}`" in SCENE
    assert "repository-agent-control-dock:" in SCENE
    assert "function createRepositoryAgentTerminal(" in SCENE
    assert "repository-agent-robot-body:" in SCENE
    assert "repository-agent-robot-wheel:" in SCENE
    assert "repository-agent-robot-arm:" in SCENE
    assert "screen.rotation.x = -Math.PI / 2;" in SCENE
    assert "side: THREE.DoubleSide" in SCENE
    assert "repositoryAgentTasksByRepository" in SCENE
    assert "task.status === \"running\"" in SCENE
    assert "task.targetNode" in SCENE
    assert "latestTerminalLine" in SCENE
    assert "CLICK · TRANSCRIPT + PROMPT" in SCENE
    assert "repositoryAgentSession || null" in SCENE
    assert "sessionId: String(session?.id || \"\")" in APP
    assert "focusSessionId: String(sessionId || \"\")" in APP
    assert "focusSessionId === String(session?.id || \"\")" in APP
    assert "world-agent-prompt-form" in APP


def test_agent_and_fediverse_counts_share_the_repository_placard():
    texture = SCENE[
        SCENE.index("function repositoryCommitActivityTexture("):
        SCENE.index("const REPOSITORY_RECORD_STATE_COLORS")
    ]
    assert 'label: "AGENT CONTROL"' in texture
    assert "runningAgents.toLocaleString" in texture
    assert 'label: "FEDIVERSE"' in texture
    assert '"FOLLOWERS"' in texture
    activity = SCENE[
        SCENE.index("function updateRepositoryActivity("):
        SCENE.index("function updateRepositorySizeMap(")
    ]
    assert "repositoryAgentTasksByRepository.get(repositoryKey)" in activity
    assert "portalRecord?.record?.fediverseFollowerCount" in activity
    assert "{ runningAgents, fediverseFollowers }" in activity
    # Counts no longer float as separate labels beside the robot dock or ring.
    assert "repository-agent-control-label:" not in SCENE
    assert "repository-fediverse-follower-caption:" not in SCENE


def test_agent_robots_offer_a_terminal_first_person_button():
    terminal = SCENE[
        SCENE.index("function createRepositoryAgentTerminal("):
        SCENE.index("function repositoryIssueAgentProviderTexture(")
    ]
    assert "repository-agent-terminal-first-person:" in terminal
    assert 'context.fillText("1P"' in terminal
    assert "repositoryAgentViewingPad" in terminal
    assert "standingPoint: viewingPoint" in terminal
    assert "interactive.push(terminal.userData.viewButton)" in SCENE
    focus = SCENE[
        SCENE.index("function focusRepositoryAgentTerminal("):
        SCENE.index("function lockOfficeElevatorCamera(")
    ]
    assert 'setCameraMode("first-person", "repository-agent-terminal")' in focus
    assert "firstPersonZoom = 1.35" in focus
    assert "standingTarget.getWorldPosition" in focus
    assert "target.getWorldPosition" in focus
    assert "focusRepositoryAgentTerminal(hit.object)" in SCENE


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
