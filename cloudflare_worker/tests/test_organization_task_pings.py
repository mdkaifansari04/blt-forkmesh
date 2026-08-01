#!/usr/bin/env python3
"""Organization task pings name who acted, on what, and in which org."""
import ast
import importlib.util
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

_catalog_spec = importlib.util.spec_from_file_location(
    "forkmesh_catalog_for_pings", ROOT / "src" / "catalog.py")
catalog = importlib.util.module_from_spec(_catalog_spec)
_catalog_spec.loader.exec_module(catalog)


def _load_ping_copy():
    want_funcs = {"organization_task_ping_copy"}
    want_assigns = {
        "MAX_NODE_NAME",
        "ORGANIZATION_TASK_ACTION_COPY",
        "ORGANIZATION_TASK_STATUS_COPY",
    }
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    body = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if targets & want_assigns:
                body.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in want_funcs:
            body.append(node)
    mod = ast.Module(body=body, type_ignores=[])
    ast.fix_missing_locations(mod)
    ns = {"clean_string": catalog.clean_string}
    exec(compile(mod, str(ENTRY), "exec"), ns)
    return ns["organization_task_ping_copy"]


TASK = {
    "id": "task-1",
    "title": "Fix the login bug",
    "department": "engineering",
    "team": "frontend",
    "priority": 20,
    "status": "active",
    "assignee": "dana",
    "assigneeKind": "user",
    "repository": "forkmesh/site",
}


def test_ping_copy_names_actor_action_task_and_organization():
    copy = _load_ping_copy()("forkmesh", "jett", "started", TASK)
    assert copy["title"] == '@jett started "Fix the login bug" in forkmesh'
    assert copy["body"].startswith(
        '@jett started "Fix the login bug" in the forkmesh organization')
    for fragment in (
            "engineering / frontend",
            "priority 20",
            "assigned to @dana",
            "status in progress",
            "repo forkmesh/site",
    ):
        assert fragment in copy["body"]
    assert copy["meta"]["organization"] == "forkmesh"
    assert copy["meta"]["actor"] == "jett"
    assert copy["meta"]["action"] == "started"
    assert copy["meta"]["taskId"] == "task-1"
    assert copy["meta"]["taskTitle"] == "Fix the login bug"
    assert copy["meta"]["department"] == "engineering"
    assert copy["meta"]["team"] == "frontend"
    assert copy["meta"]["priority"] == 20
    assert copy["meta"]["status"] == "active"
    assert copy["meta"]["assignee"] == "dana"
    assert copy["meta"]["repository"] == "forkmesh/site"


def test_ping_copy_survives_missing_fields_and_stays_inside_caps():
    copy = _load_ping_copy()("", "", "qa_requested", None)
    assert copy["title"] == 'a team member sent to QA "Organization task"'
    assert "in your organization" in copy["body"]
    assert copy["meta"]["action"] == "qa requested"
    assert copy["meta"]["actor"] == ""
    long_title = "x" * 400
    long = _load_ping_copy()(
        "forkmesh", "j" * 80, "created", {"title": long_title})
    assert len(long["title"]) <= 160
    assert len(long["body"]) <= 500
    assert long["title"].endswith("in forkmesh")


def test_agent_assignment_and_unassigned_tasks_read_naturally():
    agent = _load_ping_copy()("forkmesh", "jett", "created", {
        "title": "Ship the digest", "assigneeKind": "agent",
        "assignee": "agent",
    })
    assert "assigned to an agent" in agent["body"]
    assert agent["meta"]["assignee"] == "agent"
    idle = _load_ping_copy()("forkmesh", "jett", "created", {
        "title": "Ship the digest", "assigneeKind": "unassigned",
        "status": "idle",
    })
    assert "unassigned" in idle["body"]
    assert "status not started" in idle["body"]


def test_worker_notifier_resolves_the_org_name_and_credits_the_actor():
    notifier = ENTRY_TEXT.split(
        "async def notify_organization_task_activity", 1)[1].split(
            "async def notify_engineering_task_started", 1)[0]
    assert 'SELECT name FROM orgs WHERE org_bi=?' in notifier
    assert "copy = organization_task_ping_copy(" in notifier
    assert 'copy["title"]' in notifier
    assert 'body=copy["body"]' in notifier
    assert "meta=copy[\"meta\"]" in notifier


    assert 'actor=safe_actor if valid_node_name(safe_actor) else ""' in notifier
    assert 'href="/world/"' in notifier


def test_dashboard_ping_modal_lists_the_organization_task_fields():
    dashboard = assembled_dashboard_js()
    assert "function organizationTaskDetailsHtml(item)" in dashboard
    assert "${organizationTaskDetailsHtml(item)}" in dashboard
    assert 'String(item.kind || "").startsWith("organization_task")' in dashboard
    for label in ("Who", "What", "Task", "Organization", "Department",
                  "Team", "Assigned to", "Priority", "Status", "Repository"):
        assert '["%s",' % label in dashboard
    assert "organization_task_activity: \"clipboard-list\"" in dashboard


def test_dashboard_preview_opens_the_selected_ping_detail():
    dashboard = assembled_dashboard_js()
    assert "setNotificationDropdownOpen(false);" in dashboard
    assert "!event.target.closest(\"#notificationModal\")" in dashboard
