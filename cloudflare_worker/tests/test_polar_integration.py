"""Polar checkout, webhook, storage, route, and browser integration contracts."""

import asyncio
import base64
import hashlib
import hmac
import json
import sqlite3
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import polar_integration as polar
import schema
import urls


ORG = "11111111-1111-4111-8111-111111111111"
SUPPORTER = "22222222-2222-4222-8222-222222222222"
PRO = "33333333-3333-4333-8333-333333333333"
SUBSCRIPTION = "44444444-4444-4444-8444-444444444444"
EXTERNAL = "fm_" + "a" * 32
NOW = 1_800_000_000_000
SECRET = "test-webhook-secret"


def config(**updates):
    values = {
        "apiBase": "https://api.polar.sh",
        "accessToken": "polar_oat_test",
        "webhookSecret": SECRET,
        "organizationId": ORG,
        "supporterProductId": SUPPORTER,
        "proProductId": PRO,
    }
    values.update(updates)
    return polar.normalize_config(values)


def subscription_event(event_type="subscription.active", status="active",
                       product_id=SUPPORTER, external_id=EXTERNAL,
                       modified_at="2027-01-01T12:00:00Z"):
    return {
        "type": event_type,
        "data": {
            "id": SUBSCRIPTION,
            "organization_id": ORG,
            "product_id": product_id,
            "status": status,
            "modified_at": modified_at,
            "current_period_end": "2027-01-15T12:00:00Z",
            "cancel_at_period_end": False,
            "customer": {"external_id": external_id},
        },
    }


def signed_headers(body, event_id="evt_polar_123", now_seconds=1_800_000_000,
                   secret=SECRET):
    timestamp = str(now_seconds)
    signed = event_id.encode() + b"." + timestamp.encode() + b"." + body
    signature = base64.b64encode(
        hmac.new(secret.encode(), signed, hashlib.sha256).digest()
    ).decode()
    return {
        "webhook-id": event_id,
        "webhook-timestamp": timestamp,
        "webhook-signature": "v1," + signature,
    }


def test_webhook_signature_uses_exact_body_and_rejects_replay_window():
    body = b'{"type":"subscription.active"}'
    headers = signed_headers(body)
    assert polar.verify_webhook(body, headers, SECRET, 1_800_000_000)
    assert not polar.verify_webhook(body + b" ", headers, SECRET, 1_800_000_000)
    assert not polar.verify_webhook(body, headers, SECRET, 1_800_000_301)
    assert not polar.verify_webhook(body, headers, "wrong", 1_800_000_000)

    encoded = base64.b64encode(SECRET.encode()).decode()
    assert polar.verify_webhook(
        body, headers, "whsec_" + encoded, 1_800_000_000)


def test_configuration_is_fail_closed_and_restricts_provider_origins():
    ready = config()
    assert ready["checkoutReady"] and ready["webhookReady"]
    unsafe = config(apiBase="https://polar.sh.evil.example", accessToken="")
    assert unsafe["apiBase"] == "https://api.polar.sh"
    assert not unsafe["checkoutReady"]
    assert not config(proProductId=SUPPORTER)["productsReady"]
    assert not config(organizationId="not-a-uuid")["productsReady"]


def test_subscription_projection_validates_org_customer_product_and_lifecycle():
    active = polar.subscription_projection(subscription_event(), config())
    assert active["tier"] == "supporter"
    assert active["active"] is True
    assert active["periodEnd"] > NOW

    pro = polar.subscription_projection(
        subscription_event(product_id=PRO), config())
    assert pro["tier"] == "pro"

    revoked = polar.subscription_projection(
        subscription_event("subscription.revoked", "active"), config())
    assert revoked["active"] is False
    assert polar.subscription_projection(
        subscription_event(external_id="account-name"), config()) is None
    assert polar.subscription_projection(
        {**subscription_event(), "data": {
            **subscription_event()["data"], "organization_id": PRO}},
        config(),
    ) is None


class CheckoutRuntime:
    def __init__(self, tier="supporter"):
        self.tier = tier
        self.provider_calls = []
        self.inserted = None

    def method(self):
        return "POST"

    def config(self):
        return config()

    def now(self):
        return NOW

    def new_id(self):
        return "a" * 32

    def public_origin(self):
        return "https://forkmesh.com"

    def client_ip(self):
        return "203.0.113.10"

    def response(self, data, status=200, cache_control=None, extra_headers=None):
        return {"data": data, "status": status, "headers": extra_headers or {}}

    async def ensure_schema(self):
        return None

    async def json_body(self, _limit):
        return {"tier": self.tier}, ""

    async def session(self, _data):
        return "account-bi", {"email": "member@example.com"}

    async def d1_first(self, sql, *_args):
        if "polar_customers" in sql and self.inserted:
            return {"data": self.inserted}
        return None

    async def d1_run(self, sql, *args):
        if sql.startswith("INSERT INTO polar_customers"):
            self.inserted = args[2]

    async def blind(self, value):
        return "bi:" + value

    async def seal(self, value):
        return json.dumps(value)

    async def open(self, value):
        return json.loads(value) if value else None

    async def provider(self, url, payload, token):
        self.provider_calls.append((url, payload, token))
        return {"status": 201, "data": {"url": "https://checkout.polar.sh/session"}}


def test_checkout_uses_opaque_customer_mapping_and_server_side_products():
    runtime = CheckoutRuntime()
    response = asyncio.run(polar.handle(runtime, "checkout"))
    assert response["status"] == 201
    assert response["data"]["url"] == "https://checkout.polar.sh/session"
    url, payload, token = runtime.provider_calls[0]
    assert url == "https://api.polar.sh/v1/checkouts"
    assert token == "polar_oat_test"
    assert payload["products"] == [SUPPORTER]
    assert payload["external_customer_id"] == EXTERNAL
    assert payload["customer_email"] == "member@example.com"
    assert payload["customer_ip_address"] == "203.0.113.10"
    assert "account-bi" not in json.dumps(payload)

    no_fulfillment = CheckoutRuntime()
    no_fulfillment.config = lambda: config(webhookSecret="")
    unavailable = asyncio.run(polar.handle(no_fulfillment, "checkout"))
    assert unavailable["status"] == 503
    assert unavailable["data"] == {"error": "polar_not_configured"}
    assert no_fulfillment.provider_calls == []


def test_customer_portal_is_created_server_side_for_linked_account():
    runtime = CheckoutRuntime()
    runtime.inserted = json.dumps({"externalId": EXTERNAL})

    async def empty_body(_limit):
        return {}, ""

    async def portal_provider(url, payload, token):
        runtime.provider_calls.append((url, payload, token))
        return {
            "status": 201,
            "data": {"customer_portal_url": "https://polar.sh/forkmesh/portal"},
        }

    runtime.json_body = empty_body
    runtime.provider = portal_provider
    response = asyncio.run(polar.handle(runtime, "portal"))
    assert response == {
        "data": {"ok": True, "url": "https://polar.sh/forkmesh/portal"},
        "status": 201,
        "headers": {"x-content-type-options": "nosniff"},
    }
    url, payload, token = runtime.provider_calls[0]
    assert url == "https://api.polar.sh/v1/customer-sessions"
    assert payload["external_customer_id"] == EXTERNAL
    assert payload["return_url"].endswith("/dashboard/settings/account")
    assert token == "polar_oat_test"


class WebhookRuntime:
    def __init__(self, event):
        self.raw = json.dumps(event, separators=(",", ":"))
        self._headers = signed_headers(
            self.raw.encode(), now_seconds=NOW // 1000)
        self.runs = []
        self.statements = []

    def method(self):
        return "POST"

    def config(self):
        return config()

    def now(self):
        return NOW

    def headers(self):
        return self._headers

    def response(self, data, status=200, cache_control=None, extra_headers=None):
        return {"data": data, "status": status, "headers": extra_headers or {}}

    async def ensure_schema(self):
        return None

    async def raw_body(self, _limit):
        return self.raw, ""

    async def d1_first(self, sql, *_args):
        if "FROM polar_customers" in sql:
            return {"account_bi": "account-bi"}
        return None

    async def d1_run(self, sql, *args):
        self.runs.append((sql, args))

    async def blind(self, value):
        return "bi:" + value

    async def seal(self, value):
        return json.dumps(value, sort_keys=True)

    async def batch(self, statements):
        self.statements = statements


def test_active_webhook_atomically_projects_membership_and_existing_role():
    runtime = WebhookRuntime(subscription_event())
    response = asyncio.run(polar.handle(runtime, "webhook"))
    assert response["status"] == 202
    assert response["data"] == {"ok": True}
    assert runtime.runs[0][0].startswith("DELETE FROM polar_webhook_events")
    assert len(runtime.statements) == 4
    membership_sql, membership_args = runtime.statements[0]
    role_sql, role_args = runtime.statements[1]
    revoke_sql, revoke_args = runtime.statements[2]
    event_sql, event_args = runtime.statements[3]
    assert "INSERT INTO polar_memberships" in membership_sql
    assert membership_args[4:6] == ("supporter", "active")
    assert "ON CONFLICT(subscription_bi)" in membership_sql
    assert "provider_modified_at" in membership_sql
    assert "'supporting_member'" in role_sql
    assert "'supporter'" not in role_sql and "'pro'" not in role_sql
    assert "MAX(current_period_end)" in role_sql
    assert role_args == ("account-bi", NOW, "account-bi", NOW, NOW)
    assert "NOT EXISTS" in revoke_sql
    assert revoke_args == (NOW, "account-bi", "account-bi", NOW)
    assert "INSERT INTO polar_webhook_events" in event_sql
    assert event_args[0] == "evt_polar_123"


def test_revoked_webhook_revokes_only_membership_role():
    runtime = WebhookRuntime(
        subscription_event("subscription.revoked", "active"))
    asyncio.run(polar.handle(runtime, "webhook"))
    role_sql, role_args = runtime.statements[2]
    assert "UPDATE role_grants" in role_sql
    assert "role='supporting_member'" in role_sql
    assert "NOT EXISTS" in role_sql
    assert role_args == (NOW, "account-bi", "account-bi", NOW)


def _execute_statements(db, statements):
    for sql, args in statements:
        db.execute(sql, args)
    db.commit()


def test_membership_sql_keeps_other_subscription_and_ignores_older_event():
    db = sqlite3.connect(":memory:")
    db.executescript((ROOT / "migrations" / "0122_polar_memberships.sql").read_text())
    db.execute("""CREATE TABLE role_grants (
        account_bi TEXT NOT NULL, role TEXT NOT NULL, scope_type TEXT NOT NULL,
        scope_bi TEXT NOT NULL, granted_by_bi TEXT, granted_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL, revoked_at INTEGER NOT NULL,
        PRIMARY KEY (account_bi, role, scope_type, scope_bi))""")

    first = WebhookRuntime(subscription_event())
    asyncio.run(polar.handle(first, "webhook"))
    _execute_statements(db, first.statements)
    assert db.execute(
        "SELECT revoked_at FROM role_grants").fetchone() == (0,)

    # A second active subscription must keep the role when the first is later
    # revoked, and its longer period becomes the role expiry.
    other_end = NOW + 20 * 24 * 60 * 60 * 1000
    db.execute(
        "INSERT INTO polar_memberships VALUES(?,?,?,?,?,?,?,?,?)",
        ("bi:other-subscription", "account-bi", "bi:other-product", "{}",
         "pro", "active", other_end, NOW + 1, NOW),
    )
    revoked = WebhookRuntime(subscription_event(
        "subscription.revoked", "canceled",
        modified_at="2027-01-02T12:00:00Z"))
    asyncio.run(polar.handle(revoked, "webhook"))
    _execute_statements(db, revoked.statements[:3])
    assert db.execute(
        "SELECT expires_at,revoked_at FROM role_grants").fetchone() == (
            other_end, 0)

    # A delayed older active event cannot overwrite the newer revoked state.
    stale = WebhookRuntime(subscription_event(
        "subscription.active", "active",
        modified_at="2027-01-01T13:00:00Z"))
    asyncio.run(polar.handle(stale, "webhook"))
    _execute_statements(db, stale.statements[:3])
    own_status = db.execute(
        "SELECT status FROM polar_memberships WHERE subscription_bi=?",
        ("bi:polar-subscription:" + SUBSCRIPTION,),
    ).fetchone()
    assert own_status == ("canceled",)

    db.execute(
        "UPDATE polar_memberships SET status='canceled' "
        "WHERE subscription_bi='bi:other-subscription'")
    _execute_statements(db, stale.statements[1:3])
    assert db.execute(
        "SELECT revoked_at FROM role_grants").fetchone() == (NOW,)
    db.close()


def test_schema_migration_routes_and_browser_surfaces_are_wired():
    db = sqlite3.connect(":memory:")
    db.executescript((ROOT / "migrations" / "0122_polar_memberships.sql").read_text())
    tables = {
        row[0] for row in db.execute(
            "SELECT name FROM sqlite_master WHERE type='table'")
    }
    assert {"polar_customers", "polar_memberships", "polar_webhook_events"} <= tables
    db.close()

    joined_schema = "\n".join(schema.SCHEMA_STATEMENTS)
    for table in tables:
        assert "CREATE TABLE IF NOT EXISTS " + table in joined_schema
    assert urls.POLAR_INTEGRATION_RE.fullmatch(
        "/api/integrations/polar/checkout")
    assert not urls.POLAR_INTEGRATION_RE.fullmatch(
        "/api/integrations/polar/checkout/extra")

    entry = (SRC / "entry.py").read_text()
    pricing = (ROOT / "public" / "pricing.html").read_text()
    dashboard = (ROOT / "public" / "dashboard" / "js" / "04-account.js").read_text()
    assert "polar_integration_handler" in entry
    assert 'data-polar-checkout="supporter"' in pricing
    assert 'data-polar-checkout="pro"' in pricing
    assert "/api/integrations/polar/" in dashboard
