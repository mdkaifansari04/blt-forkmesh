#!/usr/bin/env python3
"""Repository dome and focused-workbench presentation contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")


def test_repository_circle_is_enclosed_by_a_geodesic_dome():
    dome = SCENE[
        SCENE.index("function createRepositoryGeodesicDome("):
        SCENE.index("function createRepositoryDistrict(")
    ]
    district = SCENE[
        SCENE.index("function createRepositoryDistrict("):
        SCENE.index("function createOrganizationQuarter(")
    ]
    assert "const REPOSITORY_DOME_RADIUS = REPOSITORY_GROUND_RADIUS - 1.5" in SCENE
    assert "const REPOSITORY_DOME_DETAIL = 2" in SCENE
    assert 'dome.name = "repository-geodesic-dome"' in dome
    assert "new THREE.IcosahedronGeometry(" in dome
    assert 'shell.name = "repository-geodesic-dome-shell"' in dome
    assert 'makeMaterial(THREE, "#ffffff"' in dome
    assert "transparent: false" in dome
    assert "opacity: 1" in dome
    assert "new THREE.InstancedMesh(" in dome
    assert 'struts.name = "repository-geodesic-dome-struts"' in dome
    assert "struts.computeBoundingSphere();" in dome
    assert 'foundation.name = "repository-geodesic-dome-foundation"' in dome
    assert 'doors.name = "repository-geodesic-dome-doors"' in dome
    assert 'transparent: false' in dome
    assert "inWestDoorway" in dome
    assert "const dome = createRepositoryGeodesicDome(THREE);" in district
    assert "group.add(dome);" in district
    assert 'interior.name = "repository-geodesic-dome-interior"' in district
    assert "interior.visible = false" in district


def test_repository_dome_draws_its_interior_only_while_occupied():
    occupancy = SCENE[
        SCENE.index("  function updateRepositoryDomeOccupancy("):
        SCENE.index("  function compactDistrictDiagnostics(")
    ]
    catalog = SCENE[
        SCENE.index("  function updateRepositoryCatalog("):
        SCENE.index("  function setRepositoryImportState(")
    ]
    assert "REPOSITORY_DOME_ENTER_RADIUS" in occupancy
    assert "REPOSITORY_DOME_EXIT_RADIUS" in occupancy
    assert "interior.visible = occupied" in occupancy
    assert "catalog.visible = occupied" in occupancy
    assert "dome.visible = !occupied" in occupancy
    assert "updateRepositoryDomeOccupancy();" in SCENE
    assert "layer.userData.repositoryDomeInterior = true" in catalog
    assert "updateRepositoryDomeOccupancy(true);" in catalog
    assert "constrainRepositoryDome(previousHorizontalPosition)" in SCENE


def test_repository_refresh_no_longer_feeds_the_tall_pr_and_issue_lists():
    refresh = APP[
        APP.index("  syncRepositoryScene() {"):
        APP.index("  repositoryTreeSizePreview(")
    ]
    desk = SCENE[
        SCENE.index("function updateRepositoryRecordDesk("):
        SCENE.index("function setRepositoryIssuePageExpanded(")
    ]
    assert "updateRepositoryRecordDesk" not in refresh
    assert desk.index("return;") < desk.index("const owner")
    assert "repository-record-desk" not in desk[:desk.index("return;")]


def test_repo_exhibit_has_an_angled_named_pedestal_without_an_agent_control_dock():
    assert "repository-commit-activity-pedestal" in SCENE
    assert "backing.rotation.x = -Math.PI / 4;" in SCENE
    assert "repositoryCommitActivityTexture(" in SCENE
    assert "`${owner}/${name}`" in SCENE
    assert "repository-agent-control-dock:" not in SCENE
    assert "function createRepositoryAgentTerminal(" not in SCENE
    assert "repository-agent-robot-body:" not in SCENE
    assert "repository-agent-robot-wheel:" not in SCENE
    assert "repository-agent-robot-arm:" not in SCENE
    assert "repository-agent-terminal-screen:" not in SCENE
    assert "repositoryAgentTasksByRepository" in SCENE
    assert 'task.status === "running"' in SCENE


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
    assert "repository-agent-control-label:" not in SCENE
    assert "repository-fediverse-follower-caption:" not in SCENE


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
    assert "data-world-shirt-initial" not in APP
    assert "data-world-shirt-flag" not in APP
    badge = CSS[
        CSS.index(".world-shirt-badge {"):
        CSS.index(".world-shirt-badge:hover")
    ]
    assert "width: 40px" in badge
    assert "height: 40px" in badge
    assert ".world-shirt-avatar" in CSS
    assert "object-fit: cover" in CSS
