#!/usr/bin/env python3
"""Projects tab with gantt view (issue #384).

Projects live in .forkmesh/projects/<n>/project-<n>.json (signed-event JSON
like issues), link issue numbers and a milestone, and carry start/end dates.
These contract tests pin the website half: the Projects tab sits next to
Issues, loads lazily over the live-mirror tree/blobs tunnel, and renders a
gantt view (default) plus a list view.
"""

import ast
import hashlib
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


SRC = Path(__file__).resolve().parents[1] / "src"
DASHBOARD_JS = assembled_dashboard_js()


def test_projects_tab_registered_next_to_issues():
    # tabMeta entry + the ordered tab row place Projects right after Issues.
    assert 'projects: { label: "Projects", icon: "chart-gantt", count: "" }' in DASHBOARD_JS
    assert '"issues", "projects", "pulls"' in DASHBOARD_JS
    assert 'data-dashboard-repo-tab-panel="projects"' in DASHBOARD_JS
    assert "data-repo-projects" in DASHBOARD_JS


def test_projects_route_is_a_recognized_tab():
    # Deep links like /owner/repo/projects must parse as a tab route on both
    # the client router and the worker's static-route classifier.
    assert '"issues", "projects", "pulls", "discussions", "mirrors"]' in DASHBOARD_JS
    routes = (SRC / "static_routes.py").read_text(encoding="utf-8")
    assert '"projects",' in routes


def test_projects_load_from_forkmesh_projects_folder():
    # Same batched tree+blobs flow as issues, against .forkmesh/projects/.
    assert '".forkmesh/projects"' in DASHBOARD_JS
    assert ".forkmesh/projects/${Number(number)}/project-${Number(number)}.json" in DASHBOARD_JS
    assert 'if (tab === "projects") loadRepoProjects(state.selectedRepo);' \
        in DASHBOARD_JS.replace("else if", "if")


def test_projects_gantt_is_default_view_with_list_fallback():
    assert 'projectsView: { filter: "open", mode: "gantt", items: [] }' in DASHBOARD_JS
    assert "renderRepoProjectsGantt" in DASHBOARD_JS
    assert "renderRepoProjectsList" in DASHBOARD_JS
    assert 'data-dashboard-project-view="${mode}"' in DASHBOARD_JS
    assert "data-repo-projects-gantt" in DASHBOARD_JS


def test_projects_link_issues_and_milestones_for_progress():
    # Progress = closed share of linked issues, falling back to the linked
    # milestone's issues; linked issue files are fetched in one batched read.
    assert "function projectProgress(project, issues)" in DASHBOARD_JS
    assert "issue.milestone === project.milestone" in DASHBOARD_JS
    assert "project.issues.includes(issue.number)" in DASHBOARD_JS


def test_issue_json_exposes_gantt_dates():
    # parseIssueJson surfaces startDate/endDate so issue bars can render.
    assert "startDate: Number(issue.startDate || 0) || 0" in DASHBOARD_JS
    assert "endDate: Number(issue.endDate || 0) || 0" in DASHBOARD_JS


# --- signed "dates" event canonicalization ----------------------------------
# The desktop client signs start/end date changes as a "dates" event; the
# worker's issue_event_content must byte-match IssueStore::contentForSigning.


def _load_issue_event_content():
    source = (SRC / "events.py").read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(SRC / "events.py"))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "issue_event_content"
    ]
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = {}
    exec(compile(module, str(SRC / "events.py"), "exec"), namespace)
    return namespace["issue_event_content"]


def test_dates_event_content_matches_client_contentforsigning():
    issue_event_content = _load_issue_event_content()
    ev = {"type": "dates", "startDate": 1783206983436, "endDate": 1785206983436}
    assert issue_event_content(ev) == "1783206983436\x001785206983436"
    # Unset halves stay "0" so a start-only or end-only edit still verifies.
    assert issue_event_content({"type": "dates"}) == "0\x000"
    assert issue_event_content({"type": "dates", "startDate": "junk"}) == "0\x000"
    content_hash = hashlib.sha256(b"1783206983436\x001785206983436").hexdigest()
    assert len(content_hash) == 64
