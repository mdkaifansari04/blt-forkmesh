#!/usr/bin/env python3
"""Tasks are paged, not truncated, on every surface that lists them.

The World board used to render ``tasks.slice(0, 100)`` and the Qt table used to
paint one row per task with no bound at all, so an organization past its first
hundred tasks either lost the rest silently or paid for every row at once. All
three surfaces — World, Qt desktop, and the dashboard's own Tasks page — now
render one hundred rows a page with every further page reachable.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
SRC = ROOT / "src"
QT_SRC = ROOT.parent / "qt_client" / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import dashboard_shell  # noqa: E402
import static_routes  # noqa: E402

from _dashboard_bundle import assembled_dashboard_js  # noqa: E402

WORLD_TASKS = (PUBLIC / "world" / "world-office-tasks.js").read_text(encoding="utf-8")
WORLD = (PUBLIC / "world" / "world.js").read_text(encoding="utf-8")
WORLD_CSS = (PUBLIC / "world" / "world.css").read_text(encoding="utf-8")
QT_TASKS = (QT_SRC / "MainWindowTasks.cpp").read_text(encoding="utf-8")
TASKS_VIEW = (PUBLIC / dashboard_shell.view_path("tasks")).read_text(encoding="utf-8")


def test_world_board_pages_the_catalog_instead_of_dropping_it():
    # The truncating slice is gone: the controller keeps the whole catalog the
    # relay returns and cuts a page out of it at render time. (The remaining
    # 100-slice in this file bounds the Marketing member list, not tasks.)
    assign = WORLD_TASKS[WORLD_TASKS.index("tasks = Array.isArray(payload?.tasks)"):]
    assert ".slice(0, 100)" not in assign[:assign.index("announceAgentTasks")]
    assert "const TASK_PAGE_SIZE = 100;" in WORLD_TASKS
    assert "const MAX_TASKS = 2000;" in WORLD_TASKS
    assert ".slice(0, MAX_TASKS)" in WORLD_TASKS
    for contract in (
        "function taskPageCount(",
        "function currentTaskPage(",
        "function taskPagerHTML(",
        "pageTasks.map(taskBoardHTML)",
        'data-world-task-page="prev"',
        'data-world-task-page="next"',
    ):
        assert contract in WORLD_TASKS, contract


def test_world_pager_is_rendered_and_styled():
    assert "data-world-organization-task-pager" in WORLD
    assert "data-world-organization-task-pager" in WORLD_TASKS
    assert ".world-task-pager {" in WORLD_CSS


def test_narrowing_the_world_board_returns_to_the_first_page():
    # A search, a filter, or a re-sort changes which tasks are visible, so the
    # board must not keep showing page 9 of a set that is now one page long.
    assert WORLD_TASKS.count("workPage = 0;") >= 5
    # And a page that no longer exists clamps down instead of painting empty.
    assert "Math.min(Math.max(0, workPage), taskPageCount(visible) - 1)" in WORLD_TASKS


def test_qt_table_paints_one_page_and_searches_the_whole_catalog():
    assert "constexpr int kOrganizationTaskPageSize = 100;" in QT_TASKS
    assert "void MainWindow::renderOrganizationTaskRows(" in QT_TASKS
    # The row's Qt::UserRole is the task's absolute index, not its row number,
    # so selecting a row on page 3 still resolves to the right task.
    assert "item->setData(Qt::UserRole, index);" in QT_TASKS
    # The search filters every task, then the page slices the matches.
    assert "matches.append(index);" in QT_TASKS
    assert "m_organizationTasksPage" in QT_TASKS
    # The old hide-rows-in-place search (which could only ever hide rows the
    # table already held) is gone.
    assert "setRowHidden(" not in QT_TASKS


def test_dashboard_serves_a_tasks_page_wired_into_the_shell():
    meta = dashboard_shell.PAGES["tasks"]
    assert meta["route"] == "/dashboard/tasks"
    assert (PUBLIC / meta["asset"]).is_file()
    assert static_routes.DASHBOARD_PAGE_ASSETS["/dashboard/tasks"] == meta["asset"]
    assert "/" + meta["asset"] in static_routes.BLOCKED_STATIC_HTML_PATHS
    sidebar = (PUBLIC / "dashboard" / "partials" / "sidebar.html").read_text(
        encoding="utf-8")
    assert 'data-nav="tasks"' in sidebar
    assert 'href="/dashboard/tasks"' in sidebar


def test_dashboard_tasks_page_reads_the_private_catalog_and_pages_it():
    js = assembled_dashboard_js()
    assert "const TASKS_PAGE_SIZE = 100;" in js
    assert '"tasks": initTasksPage,' in js
    assert 'fetchJson("/api/tasks", { fresh: true })' in js
    # Paging is over the filtered set, and the page buttons are real controls.
    assert "Math.ceil(matches.length / TASKS_PAGE_SIZE)" in js
    for marker in ("data-tasks-list", "data-tasks-pages", "data-tasks-prev",
                   "data-tasks-next", "data-tasks-search", "data-tasks-filter"):
        assert marker in TASKS_VIEW, marker
        assert marker in js, marker


def test_dashboard_tasks_page_never_renders_private_task_text_unescaped():
    js = assembled_dashboard_js()
    body = js[js.index("function taskRowHtml("):js.index("function renderTasksPage(")]
    for field in ("task.title", "taskAssigneeLabel(task)"):
        assert "escapeHtml(%s)" % field in body, field
