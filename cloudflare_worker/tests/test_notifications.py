#!/usr/bin/env python3
"""Worker/dashboard notification inbox contracts."""
import ast
from pathlib import Path

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
PUBLIC = ROOT / "public"


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
    assert "if method == \"DELETE\":" in ENTRY_TEXT
    assert "WHERE recipient_bi=? AND dedupe_bi=?" in ENTRY_TEXT
    assert '"allow": "GET, POST, DELETE"' in ENTRY_TEXT


def test_world_notification_table_has_owner_scoped_direct_delete():
    world = _read(PUBLIC / "world" / "world.js")
    css = _read(PUBLIC / "world" / "world.css")
    assert "world-notification-table-header" in world
    assert "world-notification-row" in world
    assert "data-world-notification-delete" in world
    assert "async deleteWorldNotification(" in world
    assert '{ method: "DELETE" }' in world
    assert ".world-notification-table-header" in css
    delete_block = ENTRY_TEXT[
        ENTRY_TEXT.index('    if method == "DELETE":', ENTRY_TEXT.index(
            "async def notifications_handler"))
        :ENTRY_TEXT.index("async def mirror_requests_handler")
    ]


    assert "_alert_inbox_account_name(" in delete_block
    assert "resource=item_id) != node" in delete_block
    assert "recipient_bi=? AND dedupe_bi=?" in delete_block



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
    assert "_best_effort_inbox_side_effect(\n            notify_pending_inbox(" in ENTRY_TEXT
    assert 'env, owner, repo, "issue", actor' in ENTRY_TEXT
    assert "_best_effort_inbox_side_effect(\n            notify_issue_assignees(" in ENTRY_TEXT
    for marker in (
        "await notify_pending_inbox(env, owner, repo, \"pull\"",
        "await notify_pending_inbox(env, owner, repo, \"discussion\"",
        "await notify_mentions(env, owner, repo,",
        "await enqueue_notification(env, grantee, \"repo_shared\"",
        "async def notify_release_published",
        "await notify_host_status(env, repo_bi, \"host_offline\"",
    ):
        assert marker in ENTRY_TEXT


    bounty_handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def bounties_handler")
        :ENTRY_TEXT.index("\n\n# Cap on collaborators")
    ]
    assert "notify_bounty_event" not in bounty_handler



def test_dashboard_notifications_are_wired_to_real_api_not_mock_data():


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


    assert "_best_effort_inbox_side_effect(\n            notify_subscribers(" in ENTRY_TEXT
    assert 'env, owner, repo, "issue", number, actor' in ENTRY_TEXT
    assert "_best_effort_inbox_side_effect(\n            subscribe_thread(" in ENTRY_TEXT
    assert 'subscribe_thread(env, owner, repo, "issue", number, actor)' in ENTRY_TEXT
    for marker in (
        'await notify_subscribers(env, owner, repo, "pull"',
        'await subscribe_thread(env, owner, repo, "pull"',
        "async def subscribe_thread",
        "async def notify_subscribers",
    ):
        assert marker in ENTRY_TEXT


def test_subscribed_notification_kind_exists():
    assert '"subscribed"' in ENTRY_TEXT
    assert '"credits_refilled"' in ENTRY_TEXT


def test_heartbeat_reports_credits_refilled_from_the_node_itself():



    heartbeat_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_heartbeat"):
        ENTRY_TEXT.index("async def _account_treasury_address")
    ]
    for marker in (
        'data.get("creditsRefilled", "")',
        'credits_kind in ("5h", "weekly")',
        'await enqueue_notification(\n            env, name, "credits_refilled"',
        'dedupe="credits_refilled:" + credits_kind',
        "HEARTBEAT_SOLANA_BALANCE_TIMEOUT_MS",
        "asyncio.wait_for(\n                _solana_balance_lamports(env, wallet)",
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


def test_notification_email_preferences_are_editable_and_host_alerts_default_off():
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_profile"):
        ENTRY_TEXT.index("# --- Users vs nodes")
    ]
    heartbeat_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_heartbeat"):
        ENTRY_TEXT.index("async def _mirroring_owners")
    ]
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        '"host_online": False',
        '"host_offline": False',
        '"general_chat": True',
        '"notificationPreferences": notification_email_preferences(rec)',
        '"emailNotifications": rec.get("email_notifications") is not False',
        '"notificationPreferences" in data',
        "notification_email_preferences(email_rec)",
    ):
        assert marker in ENTRY_TEXT

    assert '"notificationPreferences" in data' in profile_body
    assert '"notificationPreferences" in data' in heartbeat_body
    assert "prefs_bi = owner_bi" in heartbeat_body
    assert "data-notification-preferences" in dashboard
    assert 'data-notification-pref="host_online"' in dashboard
    assert 'data-notification-pref="host_offline"' in dashboard
    assert 'data-notification-pref="general_chat"' in dashboard
    assert "host_online: false" in dashboard_js
    assert "host_offline: false" in dashboard_js
    assert "general_chat: true" in dashboard_js
    assert "saveNotificationPreferences" in dashboard_js


def test_daily_general_chat_digest_counts_encrypted_messages_without_emailing_contents():
    for marker in (
        "GENERAL_CHAT_EMAIL_INTERVAL_MS",
        "async def send_general_chat_digests",
        "await send_general_chat_digests(env)",
        "SELECT COUNT(*) AS c FROM chat_history",
        "FLAGSHIP_ROOM_KEY",
        "Message contents are not sent over email",
        "notification_email_enabled(rec, \"general_chat\")",
    ):
        assert marker in ENTRY_TEXT
    assert "GENERAL_CHAT_ROOM_PASSPHRASE" not in ENTRY_TEXT
    assert "decrypt_general_chat" not in ENTRY_TEXT


def test_qt_node_syncs_email_notification_preferences_over_heartbeat():
    qt_src = ROOT.parent / "qt_client" / "src"
    header = (qt_src / "MainWindowInternal.h").read_text(encoding="utf-8")
    setup = (qt_src / "MainWindowSetup.cpp").read_text(encoding="utf-8")
    settings = (qt_src / "MainWindowSettings.cpp").read_text(encoding="utf-8")

    assert "kEmailNotifyGeneralChatSetting" in header
    assert "kEmailNotifyHostOnlineSetting" in header
    assert "kEmailNotifyHostOfflineSetting" in header
    assert "QJsonObject MainWindow::emailNotificationPreferencesPayload() const" in setup
    assert 'body.insert(QStringLiteral("notificationPreferences")' in setup
    assert "enabled(kEmailNotifyGeneralChatSetting, true)" in setup
    assert "enabled(kEmailNotifyHostOnlineSetting, false)" in setup
    assert "enabled(kEmailNotifyHostOfflineSetting, false)" in setup
    assert "Email me a daily #general count" in settings


def test_thread_key_is_stable_and_scoped():
    ns = _load_notification_helpers()
    key = ns["_thread_key"]("alice", "repo", "issue", 7)
    assert key == "thread:alice/repo:issue:7"


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
        test_notification_email_preferences_are_editable_and_host_alerts_default_off,
        test_daily_general_chat_digest_counts_encrypted_messages_without_emailing_contents,
        test_qt_node_syncs_email_notification_preferences_over_heartbeat,
        test_thread_key_is_stable_and_scoped,
        test_digest_html_escapes_untrusted_notification_text,
    ):
        test()
        print("PASS", test.__name__)
