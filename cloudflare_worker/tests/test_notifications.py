#!/usr/bin/env python3
"""Worker/dashboard notification inbox contracts."""
import ast
from pathlib import Path

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
PUBLIC = ROOT / "public"
# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + SCHEMA.read_text(encoding="utf-8"))


def _load_notification_helpers():
    want_funcs = {
        "notification_mentions",
        "notification_payload",
        "_thread_key",
        "_html_escape",
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
    # dashboard.js is split into ordered public/dashboard/js/*.js fragments the
    # Worker concatenates into one /dashboard.js (see src/dashboard_bundle.py);
    # assert on the composed script.
    if path.name == "dashboard.js":
        return assembled_dashboard_js()
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
    # The two shell files stay byte-identical; the markers live in the composed
    # document the Worker serves (header + modals partials).
    assert _read(PUBLIC / "dashboard" / "index.html") == _read(PUBLIC / "dashboard.html")
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

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


def test_worker_exposes_signed_thread_subscription_route_and_schema():
    # Subscriptions (issue #361): a signed subscribe/unsubscribe endpoint plus the
    # table that backs it.
    urls_text = (ENTRY.parent / "urls.py").read_text(encoding="utf-8")
    assert 'REPO_SUBSCRIBE_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/subscribe$")' in urls_text
    assert "async def subscribe_handler" in ENTRY_TEXT
    assert "REPO_SUBSCRIBE_RE.match(url.path)" in ENTRY_TEXT
    assert "await subscribe_handler(self.env, request, owner, repo)" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS thread_subscriptions" in ENTRY_TEXT
    assert "thread_bi TEXT NOT NULL" in ENTRY_TEXT
    assert "idx_thread_subscriptions_thread" in ENTRY_TEXT
    assert "forkmesh-subscribe-v1" in ENTRY_TEXT


def test_worker_auto_subscribes_commenters_and_fans_out_to_followers():
    # Anyone who comments is auto-subscribed and existing followers are notified,
    # for both issues and PRs.
    for marker in (
        'await notify_subscribers(env, owner, repo, "issue"',
        'await subscribe_thread(env, owner, repo, "issue"',
        'await notify_subscribers(env, owner, repo, "pull"',
        'await subscribe_thread(env, owner, repo, "pull"',
        "async def subscribe_thread",
        "async def notify_subscribers",
    ):
        assert marker in ENTRY_TEXT


def test_subscribed_notification_kind_exists():
    assert '"subscribed"' in ENTRY_TEXT
    assert '"credits_refilled"' in ENTRY_TEXT  # issue #346 rides the same rail


def test_heartbeat_reports_credits_refilled_from_the_node_itself():
    # Issue #346: only the node knows when its own Claude Code usage window
    # refilled after running out, so it rides the already-signed heartbeat
    # (accounts/heartbeat) rather than a new endpoint.
    heartbeat_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_heartbeat"):
        ENTRY_TEXT.index("async def _account_treasury_address")
    ]
    for marker in (
        'data.get("creditsRefilled", "")',
        'credits_kind in ("5h", "weekly")',
        'await enqueue_notification(\n            env, name, "credits_refilled"',
        'dedupe="credits_refilled:" + credits_kind',
    ):
        assert marker in heartbeat_body

    qt_src = ROOT.parent / "qt_client" / "src"
    qt_text = "\n".join(
        p.read_text(encoding="utf-8")
        for p in sorted(qt_src.glob("MainWindow*.cpp"))
    )
    assert 'body.insert(QStringLiteral("creditsRefilled")' in qt_text
    assert "kEmailOnCreditsRefillSetting" in qt_text
    assert "maybeEmailCreditsRefilled" in qt_text


def test_email_digest_bridge_is_wired_to_the_cron_and_verified_email():
    for marker in (
        "async def send_notification_digests",
        "await send_notification_digests(self.env)",
        'rec.get("email_verified")',
        'rec.get("email_notifications") is False',
        "NOTIFICATION_DIGEST_INTERVAL_MS",
        "NOTIFICATION_DIGEST_MIN_AGE_MS",
        "def _notification_digest_email",
        "await _send_email(env, email, subject, text, html)",
    ):
        assert marker in ENTRY_TEXT


def test_thread_key_is_stable_and_scoped():
    ns = _load_notification_helpers()
    key = ns["_thread_key"]("alice", "repo", "issue", 7)
    assert key == "thread:alice/repo:issue:7"
    # A brand-new submission (no durable number) collapses to a shared 0 key,
    # which the subscribe/notify helpers guard against.
    assert ns["_thread_key"]("alice", "repo", "issue", 0).endswith(":issue:0")
    assert ns["_thread_key"]("a", "b", "pull", None).endswith(":pull:0")


def test_digest_html_escapes_untrusted_notification_text():
    ns = _load_notification_helpers()
    assert ns["_html_escape"]("<script>&\"x") == "&lt;script&gt;&amp;&quot;x"


if __name__ == "__main__":
    for test in (
        test_worker_exposes_first_class_notifications_route_and_schema,
        test_notification_kind_contract_covers_dashboard_events,
        test_notification_mentions_extract_node_handles_once_without_false_positives,
        test_worker_indexes_notifications_from_existing_event_sources,
        test_dashboard_notifications_are_wired_to_real_api_not_mock_data,
        test_worker_exposes_signed_thread_subscription_route_and_schema,
        test_worker_auto_subscribes_commenters_and_fans_out_to_followers,
        test_subscribed_notification_kind_exists,
        test_email_digest_bridge_is_wired_to_the_cron_and_verified_email,
        test_thread_key_is_stable_and_scoped,
        test_digest_html_escapes_untrusted_notification_text,
    ):
        test()
        print("PASS", test.__name__)
