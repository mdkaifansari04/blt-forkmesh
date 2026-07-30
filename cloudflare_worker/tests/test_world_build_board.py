"""Shared World build-board authorization and drag/drop contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
API = (ROOT / "src/world_build_board.py").read_text(encoding="utf-8")
ENTRY = (ROOT / "src/entry.py").read_text(encoding="utf-8")
SCENE = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
WORLD = (ROOT / "public/world/world.js").read_text(encoding="utf-8")
MIGRATION = (ROOT / "migrations/0091_world_build_board.sql").read_text(
    encoding="utf-8")


def test_build_board_has_bounded_public_state_and_authorized_audited_writes():
    assert "MAX_ITEMS = 64" in API
    assert 'role in ("owner", "admin")' in API
    assert 'permission in ("maintain", "admin")' in API
    assert "origin_not_allowed" in API
    assert "world.build_board_" in API
    assert "updated_by_bi" in MIGRATION
    assert "CHECK (priority >= 1 AND priority <= 64)" in MIGRATION
    assert "world_build_board_handler" in ENTRY
    assert "Reject unauthorized mutations before touching a repository mirror" in ENTRY


def test_world_renders_and_drags_priority_and_issue_stickies():
    assert "function updateBuildBoard(payload = {})" in SCENE
    assert "function buildBoardGridIndex(uv, count)" in SCENE
    assert "draggedBuildCard" in SCENE
    assert "onBuildBoardReorder" in SCENE
    assert "onBuildIssueAssign" in SCENE
    assert "onBuildSendQa" in SCENE
    assert "DONE → QA" in SCENE
    assert "buildBoardSendQaHit" in SCENE
    assert "updateBuildBoard," in SCENE
    assert '"/api/world/build-board"' in WORLD
    assert "reorderBuildBoard(order)" in WORLD
    assert "assignBuildIssue(key, title)" in WORLD
    assert "sendBuildTaskToQa(key, title)" in WORLD
    assert 'action: "send_qa"' in WORLD
    assert 'action == "send_qa"' in API
    assert "`.forkmesh/issues/open/${number}/issue-${number}.json`" in WORLD
    assert "WORLD_BUILD_BOARD_POLL_MS = 60 * 1000" in WORLD


def test_build_board_lists_active_work_on_straight_detailed_cards():
    texture = SCENE[
        SCENE.index("function worldTaskBulletinTexture("):
        SCENE.index("function worldQaCardTexture(")
    ]
    assert 'key: "task:world-board-detail"' in SCENE
    assert 'key: "task:world-node-delete-regression"' in SCENE
    assert "const x = 58 + column * 574;" in texture
    assert "const y = 200 + row * 218;" in texture
    assert "context.rotate(angle)" not in texture
    assert "`#${index + 1} · OPEN TODO`" in texture
    assert "item.detail ||" in texture
    assert "Active implementation item; details will update" in texture
    assert "Returned from QA for another implementation" in SCENE
    assert "Assigned from the forkmesh/forkmesh repository issue queue" in SCENE
    for key in (
        "task:world-board-detail",
        "task:world-mobile-pan-stability",
        "task:world-stable-hydration",
        "task:repo-board-view-pads",
        "task:world-continuous-city",
        "task:world-start-here-map",
        "task:world-roof-and-seating",
        "task:world-beach-road",
        "task:world-bike-perimeter",
        "task:world-panel-layout",
        "task:world-github-theme",
        "task:world-time-stars-textures",
        "task:world-node-delete-regression",
    ):
        assert f'"{key}"' in API


def test_build_board_refreshes_on_approach_with_a_scene_native_spinner():
    assert 'buildBoardSpinner.name = "forkmesh-build-board-updating-spinner"' in SCENE
    assert "buildBoardSpinner.visible = false;" in SCENE
    assert "function setBuildBoardLoading(loading)" in SCENE
    assert "function updateBuildBoardProximity()" in SCENE
    assert "if (distance <= 18 && !buildBoardWasNearby)" in SCENE
    assert "else if (distance >= 23)" in SCENE
    assert "onBuildBoardNearby();" in SCENE
    assert "updateBuildBoardProximity();" in SCENE
    assert "onBuildBoardNearby: () =>" in WORLD
    assert "void this.refreshBuildBoard({ quiet: true })" in WORLD
    refresh = WORLD[
        WORLD.index("  async refreshBuildBoard("):
        WORLD.index("\n  applyQaDeck(", WORLD.index("  async refreshBuildBoard("))
    ]
    assert "this.world?.setBuildBoardLoading?.(true);" in refresh
    assert "this.world?.setBuildBoardLoading?.(false);" in refresh
    assert "maxAge: WORLD_BUILD_BOARD_POLL_MS" in refresh
    assert "backoff: true" in refresh
    assert "staleIfError: true" in refresh
    assert "Repository issue enrichment is optional" in refresh
    assert "location.reload()" not in refresh


def test_engineering_agent_signals_feed_a_separate_human_todo_board():
    assert "function humanTodoItemsFromAgentSessions" in SCENE
    assert "function worldHumanTodoTexture" in SCENE
    assert "HUMAN TODO · FROM CLAUDE + CODEX" in SCENE
    assert "ENGINEERING · BOT-REQUESTED MANUAL ACTIONS" in SCENE
    assert 'humanTodoBoardFace.name = "forkmesh-human-todo-board-face"' in SCENE
    assert "availability.binaryFound === false" in SCENE
    assert 'availability.loginState === "missing"' in SCENE
    assert "session?.agentInfo?.lastError" in SCENE
    task_update = SCENE.split("function updateMirrorAgentTasks", 1)[1]
    assert "repaintHumanTodoBoard();" in task_update
    assert "worldHumanTodoTexture(" in SCENE.split(
        "function repaintHumanTodoBoard", 1
    )[1]
