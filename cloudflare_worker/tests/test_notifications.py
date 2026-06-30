#!/usr/bin/env python3
"""Worker/dashboard notification inbox contracts."""
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
PUBLIC = ROOT / "public"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _load_notification_helpers():
    want_funcs = {
        "notification_mentions",
        "notification_payload",
    }
    want_assigns = {
        "NODE_NAME_RE",
        "MAX_NODE_NAME",
        "MENTION_RE",
        "NOTIFICATION_KINDS",
    }
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    body = []
    for node in tree.body:
        if isinstance(node, (ast.Import, ast.ImportFrom)) and any(
            alias.name == "re" for alias in node.names
        ):
            body.append(node)
        elif isinstance(node, ast.Assign):
            targets = {t.id for t in node.targets if isinstance(t, ast.Name)}
            if targets & want_assigns:
                body.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in want_funcs:
            body.append(node)
    mod = ast.Module(body=body, type_ignores=[])
    ast.fix_missing_locations(mod)
    ns = {}
    exec(compile(mod, str(ENTRY), "exec"), ns)
    return ns


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_worker_exposes_first_class_notifications_route_and_schema():
    assert 'url.path in ("/api/notifications", "/api/notifications/")' in ENTRY_TEXT
    assert "async def notifications_handler" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS notifications" in ENTRY_TEXT
    assert "recipient_bi TEXT NOT NULL" in ENTRY_TEXT
    assert "dedupe_bi TEXT PRIMARY KEY" in ENTRY_TEXT
    assert "read_at INTEGER NOT NULL DEFAULT 0" in ENTRY_TEXT
    assert "idx_notifications_recipient_ts" in ENTRY_TEXT



def test_notification_kind_contract_covers_dashboard_events():
    for kind in (
        "mention",
        "pull_submitted",
        "issue_assigned",
        "repo_shared",
        "bounty_funded",
        "bounty_paid",
        "release_published",
        "host_online",
        "host_offline",
        "pending_inbox",
    ):
        assert f'"{kind}"' in ENTRY_TEXT



def test_notification_mentions_extract_node_handles_once_without_false_positives():
    ns = _load_notification_helpers()
    mentions = ns["notification_mentions"](
        "cc @demo-node and @alice @demo-node, ignore email@example.com and @bad_name"
    )
    assert mentions == ["alice", "demo-node"]



def test_worker_indexes_notifications_from_existing_event_sources():
    for marker in (
        "await notify_pending_inbox(env, owner, repo, \"issue\"",
        "await notify_pending_inbox(env, owner, repo, \"pull\"",
        "await notify_pending_inbox(env, owner, repo, \"commit_comment\"",
        "await notify_pending_inbox(env, owner, repo, \"discussion\"",
        "await notify_mentions(env, owner, repo,",
        "await notify_issue_assignees(env, owner, repo,",
        "await enqueue_notification(env, grantee, \"repo_shared\"",
        "await notify_bounty_event(env, rec, \"bounty_funded\"",
        "await notify_bounty_event(env, rec, \"bounty_paid\"",
        "async def notify_release_published",
        "await notify_host_status(env, repo_bi, \"host_online\"",
    ):
        assert marker in ENTRY_TEXT



def test_dashboard_notifications_are_wired_to_real_api_not_mock_data():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert "Notifications are intentionally hidden" not in dashboard
    assert "Notification reader modal is intentionally hidden" not in dashboard
    assert "Notifications disabled until notification data is wired" not in dashboard
    assert "const notifications = []" not in dashboard
    assert "notificationToggle" in dashboard
    assert "notificationModal" in dashboard
    assert "data-notification-count" in dashboard
    assert "loadNotifications" in dashboard_js
    assert 'fetchJson(`/api/notifications?node=${encodeURIComponent(node)}`)' in dashboard_js
    assert "renderNotificationPreview" in dashboard_js
    assert "renderNotificationModal" in dashboard_js
    for mock in (
        "alice-node",
        "relay-eu-1",
        "Cloudflare had a blip",
    ):
        assert mock not in dashboard_js


if __name__ == "__main__":
    for test in (
        test_worker_exposes_first_class_notifications_route_and_schema,
        test_notification_kind_contract_covers_dashboard_events,
        test_notification_mentions_extract_node_handles_once_without_false_positives,
        test_worker_indexes_notifications_from_existing_event_sources,
        test_dashboard_notifications_are_wired_to_real_api_not_mock_data,
    ):
        test()
        print("PASS", test.__name__)
