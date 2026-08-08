"""Polar checkout, customer portal, and membership webhook policy.

The Workers adapter in ``entry.py`` owns network and D1 primitives.  This
module stays importable under ordinary CPython so the signature, payload, and
membership behavior can be tested without a Cloudflare runtime.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import re
from datetime import datetime, timezone
from urllib.parse import urlparse


BODY_MAX_BYTES = 256 * 1024
WEBHOOK_TOLERANCE_SECONDS = 5 * 60
WEBHOOK_EVENT_RETENTION_MS = 90 * 24 * 60 * 60 * 1000
MEMBERSHIP_ROLES = ("supporter", "pro")
ACTIVE_SUBSCRIPTION_STATES = frozenset({"active", "trialing"})
SUBSCRIPTION_EVENTS = frozenset({
    "subscription.created",
    "subscription.active",
    "subscription.uncanceled",
    "subscription.canceled",
    "subscription.past_due",
    "subscription.updated",
    "subscription.revoked",
})
_UUID_RE = re.compile(
    r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-"
    r"[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
    re.IGNORECASE,
)
_EXTERNAL_ID_RE = re.compile(r"^fm_[0-9a-f]{32}$")
_WEBHOOK_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,160}$")


def _response(runtime, data, status=200, allow=""):
    headers = {"x-content-type-options": "nosniff"}
    if allow:
        headers["allow"] = allow
    return runtime.response(
        data,
        status=status,
        cache_control="no-store, max-age=0, must-revalidate",
        extra_headers=headers,
    )


def _header(headers, name):
    """Read a header from a dict-like or Workers Headers object."""

    name = str(name).lower()
    try:
        value = headers.get(name)
        if value is not None:
            return str(value)
    except Exception:
        pass
    try:
        for key, value in headers.items():
            if str(key).lower() == name:
                return str(value)
    except Exception:
        pass
    return ""


def _webhook_key(secret):
    """Return the Standard Webhooks HMAC key for a Polar secret.

    Polar accepts an operator-provided plain secret and its SDK base64-wraps it
    before passing it to Standard Webhooks.  That is equivalent to using the
    plain UTF-8 bytes as the HMAC key.  Native ``whsec_`` values already carry
    Standard Webhooks' base64 form and are decoded directly.
    """

    secret = str(secret or "").strip()
    if not secret:
        return b""
    if secret.startswith("whsec_"):
        encoded = secret[6:]
        try:
            return base64.b64decode(encoded, validate=True)
        except Exception:
            return b""
    return secret.encode("utf-8")


def verify_webhook(body, headers, secret, now_seconds=None,
                   tolerance_seconds=WEBHOOK_TOLERANCE_SECONDS):
    """Verify Polar's Standard Webhooks id/timestamp/signature envelope."""

    if isinstance(body, str):
        body = body.encode("utf-8")
    if not isinstance(body, bytes) or not body or len(body) > BODY_MAX_BYTES:
        return False
    webhook_id = _header(headers, "webhook-id").strip()
    timestamp_text = _header(headers, "webhook-timestamp").strip()
    signature_header = _header(headers, "webhook-signature").strip()
    if not _WEBHOOK_ID_RE.fullmatch(webhook_id):
        return False
    try:
        timestamp = int(timestamp_text)
        now = int(datetime.now(timezone.utc).timestamp()
                  if now_seconds is None else now_seconds)
        tolerance = max(1, min(3600, int(tolerance_seconds)))
    except (TypeError, ValueError, OverflowError):
        return False
    if timestamp <= 0 or abs(now - timestamp) > tolerance:
        return False
    key = _webhook_key(secret)
    if not key:
        return False
    signed = (
        webhook_id.encode("utf-8") + b"."
        + timestamp_text.encode("ascii") + b"." + body
    )
    expected = base64.b64encode(
        hmac.new(key, signed, hashlib.sha256).digest()
    ).decode("ascii")
    for item in signature_header.split():
        version, separator, presented = item.partition(",")
        if (
            separator
            and version == "v1"
            and hmac.compare_digest(presented, expected)
        ):
            return True
    return False


def _iso_ms(value):
    value = str(value or "").strip()
    if not value:
        return 0
    try:
        if value.endswith("Z"):
            value = value[:-1] + "+00:00"
        parsed = datetime.fromisoformat(value)
        if parsed.tzinfo is None:
            parsed = parsed.replace(tzinfo=timezone.utc)
        return max(0, int(parsed.timestamp() * 1000))
    except (TypeError, ValueError, OverflowError):
        return 0


def _uuid(value):
    value = str(value or "").strip().lower()
    return value if _UUID_RE.fullmatch(value) else ""


def normalize_config(config):
    config = config if isinstance(config, dict) else {}
    api_base = str(config.get("apiBase") or "").strip().rstrip("/")
    if api_base not in {
        "https://api.polar.sh",
        "https://sandbox-api.polar.sh",
    }:
        api_base = "https://api.polar.sh"
    result = {
        "apiBase": api_base,
        "accessToken": str(config.get("accessToken") or "").strip(),
        "webhookSecret": str(config.get("webhookSecret") or "").strip(),
        "organizationId": _uuid(config.get("organizationId")),
        "supporterProductId": _uuid(config.get("supporterProductId")),
        "proProductId": _uuid(config.get("proProductId")),
    }
    result["productsReady"] = bool(
        result["organizationId"]
        and result["supporterProductId"]
        and result["proProductId"]
        and result["supporterProductId"] != result["proProductId"]
    )
    result["checkoutReady"] = bool(
        result["productsReady"] and result["accessToken"]
    )
    result["webhookReady"] = bool(
        result["productsReady"] and result["webhookSecret"]
    )
    return result


def subscription_projection(payload, config):
    """Return the small trusted projection used for membership updates."""

    if not isinstance(payload, dict):
        return None
    event_type = str(payload.get("type") or "").strip().lower()
    if event_type not in SUBSCRIPTION_EVENTS:
        return {"ignored": True, "eventType": event_type}
    data = payload.get("data")
    if not isinstance(data, dict):
        return None
    organization_id = _uuid(data.get("organization_id"))
    product_id = _uuid(data.get("product_id"))
    if not product_id and isinstance(data.get("product"), dict):
        product_id = _uuid(data["product"].get("id"))
    customer = data.get("customer")
    external_id = ""
    if isinstance(customer, dict):
        external_id = str(customer.get("external_id") or "").strip()
    external_id = str(
        data.get("external_customer_id") or external_id
    ).strip()
    subscription_id = _uuid(data.get("id"))
    if (
        organization_id != config.get("organizationId")
        or not subscription_id
        or not _EXTERNAL_ID_RE.fullmatch(external_id)
    ):
        return None
    if product_id == config.get("supporterProductId"):
        tier = "supporter"
    elif product_id == config.get("proProductId"):
        tier = "pro"
    else:
        return {"ignored": True, "eventType": event_type}
    status = str(data.get("status") or "").strip().lower()[:40]
    period_end = _iso_ms(
        data.get("current_period_end") or data.get("ends_at"))
    provider_modified_at = _iso_ms(
        data.get("modified_at") or data.get("created_at"))
    if not provider_modified_at:
        return None
    active = status in ACTIVE_SUBSCRIPTION_STATES
    if event_type == "subscription.revoked":
        active = False
    return {
        "ignored": False,
        "eventType": event_type,
        "externalId": external_id,
        "subscriptionId": subscription_id,
        "productId": product_id,
        "tier": tier,
        "status": status,
        "periodEnd": period_end,
        "providerModifiedAt": provider_modified_at,
        "active": active,
        "cancelAtPeriodEnd": bool(data.get("cancel_at_period_end")),
    }


def _polar_url(value):
    value = str(value or "").strip()
    try:
        parsed = urlparse(value)
    except Exception:
        return ""
    host = str(parsed.hostname or "").lower()
    if (
        parsed.scheme != "https"
        or not host
        or not (host == "polar.sh" or host.endswith(".polar.sh"))
        or parsed.username
        or parsed.password
    ):
        return ""
    return value[:2083]


async def _customer_record(runtime, account_bi, create=False):
    row = await runtime.d1_first(
        "SELECT external_id_bi,data FROM polar_customers WHERE account_bi=?",
        account_bi,
    )
    record = await runtime.open((row or {}).get("data", "")) if row else None
    external_id = str((record or {}).get("externalId") or "")
    if _EXTERNAL_ID_RE.fullmatch(external_id):
        return external_id
    if not create:
        return ""
    external_id = "fm_" + runtime.new_id()
    if not _EXTERNAL_ID_RE.fullmatch(external_id):
        return ""
    now = runtime.now()
    external_bi = await runtime.blind("polar-customer:" + external_id)
    sealed = await runtime.seal({"externalId": external_id})
    try:
        await runtime.d1_run(
            "INSERT INTO polar_customers"
            "(account_bi,external_id_bi,data,created_at,updated_at) "
            "VALUES(?,?,?,?,?)",
            account_bi, external_bi, sealed, now, now,
        )
        return external_id
    except Exception:
        # A concurrent checkout may have created the one account mapping.
        row = await runtime.d1_first(
            "SELECT data FROM polar_customers WHERE account_bi=?", account_bi)
        record = await runtime.open((row or {}).get("data", "")) if row else None
        external_id = str((record or {}).get("externalId") or "")
        return external_id if _EXTERNAL_ID_RE.fullmatch(external_id) else ""


async def _account_status(runtime, config):
    account_bi, _account = await runtime.session({})
    if not account_bi:
        return _response(runtime, {"error": "unauthorized"}, 401)
    row = await runtime.d1_first(
        "SELECT tier,status,current_period_end FROM polar_memberships "
        "WHERE account_bi=? AND status IN ('active','trialing') "
        "AND current_period_end>? ORDER BY "
        "CASE tier WHEN 'pro' THEN 1 ELSE 0 END DESC,"
        "current_period_end DESC LIMIT 1",
        account_bi, runtime.now(),
    )
    now = runtime.now()
    active = bool(row and int(row.get("current_period_end") or 0) > now)
    return _response(runtime, {
        "configured": bool(config["checkoutReady"] and config["webhookReady"]),
        "connected": bool(await _customer_record(runtime, account_bi)),
        "active": active,
        "tier": str((row or {}).get("tier") or "") if active else "",
        "currentPeriodEnd": int((row or {}).get("current_period_end") or 0)
        if active else 0,
    })


async def _checkout(runtime, config):
    data, error = await runtime.json_body(BODY_MAX_BYTES)
    if error:
        return _response(runtime, {"error": error}, 413 if error == "payload_too_large" else 400)
    if set(data) - {"tier", "sessionToken"}:
        return _response(runtime, {"error": "unsupported_checkout_field"}, 400)
    account_bi, account = await runtime.session(data)
    if not account_bi or not account:
        return _response(runtime, {"error": "unauthorized"}, 401)
    # Do not accept payment unless the signed fulfillment path is ready too.
    if not (config["checkoutReady"] and config["webhookReady"]):
        return _response(runtime, {"error": "polar_not_configured"}, 503)
    tier = str(data.get("tier") or "").strip().lower()
    product_id = config.get(tier + "ProductId") if tier in MEMBERSHIP_ROLES else ""
    if not product_id:
        return _response(runtime, {"error": "invalid_tier"}, 400)
    external_id = await _customer_record(runtime, account_bi, create=True)
    if not external_id:
        return _response(runtime, {"error": "customer_mapping_failed"}, 500)
    origin = runtime.public_origin()
    if not origin:
        return _response(runtime, {"error": "public_origin_required"}, 503)
    payload = {
        "products": [product_id],
        "external_customer_id": external_id,
        "metadata": {"forkmesh_tier": tier},
        "success_url": (
            origin + "/dashboard/settings/account?polar=success"
            "&checkout_id={CHECKOUT_ID}"
        ),
        "return_url": origin + "/pricing",
    }
    email = str(account.get("email") or "").strip()
    if email and len(email) <= 254:
        payload["customer_email"] = email
    client_ip = runtime.client_ip()
    if client_ip:
        payload["customer_ip_address"] = client_ip
    provider = await runtime.provider(
        config["apiBase"] + "/v1/checkouts", payload,
        config["accessToken"])
    url = _polar_url((provider.get("data") or {}).get("url"))
    if int(provider.get("status") or 0) != 201 or not url:
        return _response(runtime, {"error": "polar_unavailable"}, 502)
    return _response(runtime, {"ok": True, "url": url}, 201)


async def _portal(runtime, config):
    data, error = await runtime.json_body(BODY_MAX_BYTES)
    if error:
        return _response(runtime, {"error": error}, 413 if error == "payload_too_large" else 400)
    if set(data) - {"sessionToken"}:
        return _response(runtime, {"error": "unsupported_portal_field"}, 400)
    account_bi, _account = await runtime.session(data)
    if not account_bi:
        return _response(runtime, {"error": "unauthorized"}, 401)
    if not config["checkoutReady"]:
        return _response(runtime, {"error": "polar_not_configured"}, 503)
    external_id = await _customer_record(runtime, account_bi)
    if not external_id:
        return _response(runtime, {"error": "polar_customer_not_found"}, 404)
    provider = await runtime.provider(
        config["apiBase"] + "/v1/customer-sessions",
        {
            "external_customer_id": external_id,
            "return_url": runtime.public_origin() + "/dashboard/settings/account",
        },
        config["accessToken"],
    )
    url = _polar_url(
        (provider.get("data") or {}).get("customer_portal_url"))
    if int(provider.get("status") or 0) != 201 or not url:
        return _response(runtime, {"error": "polar_unavailable"}, 502)
    return _response(runtime, {"ok": True, "url": url}, 201)


async def _webhook(runtime, config):
    if not config["webhookReady"]:
        return _response(runtime, {"error": "polar_webhook_not_configured"}, 503)
    raw, error = await runtime.raw_body(BODY_MAX_BYTES)
    if error:
        return _response(runtime, {"error": error}, 413 if error == "payload_too_large" else 400)
    now_seconds = runtime.now() // 1000
    if not verify_webhook(
        raw, runtime.headers(), config["webhookSecret"], now_seconds):
        return _response(runtime, {"error": "invalid_signature"}, 401)
    try:
        payload = json.loads(raw)
    except Exception:
        return _response(runtime, {"error": "invalid_payload"}, 400)
    projection = subscription_projection(payload, config)
    event_id = _header(runtime.headers(), "webhook-id").strip()
    if projection is None:
        return _response(runtime, {"error": "invalid_payload"}, 400)
    await runtime.ensure_schema()
    # Polar's documented retry window is far shorter than this.  Keeping only
    # opaque event ids for 90 days bounds the idempotency table without retaining
    # customer or payload data.
    await runtime.d1_run(
        "DELETE FROM polar_webhook_events WHERE received_at<?",
        runtime.now() - WEBHOOK_EVENT_RETENTION_MS,
    )
    if await runtime.d1_first(
            "SELECT event_id FROM polar_webhook_events WHERE event_id=?",
            event_id):
        return _response(runtime, {"ok": True, "duplicate": True}, 202)
    now = runtime.now()
    if projection.get("ignored"):
        await runtime.d1_run(
            "INSERT INTO polar_webhook_events"
            "(event_id,event_type,received_at) VALUES(?,?,?)",
            event_id, str(projection.get("eventType") or "")[:80], now,
        )
        return _response(runtime, {"ok": True, "ignored": True}, 202)
    external_bi = await runtime.blind(
        "polar-customer:" + projection["externalId"])
    customer = await runtime.d1_first(
        "SELECT account_bi FROM polar_customers WHERE external_id_bi=?",
        external_bi,
    )
    account_bi = str((customer or {}).get("account_bi") or "")
    if not account_bi:
        await runtime.d1_run(
            "INSERT INTO polar_webhook_events"
            "(event_id,event_type,received_at) VALUES(?,?,?)",
            event_id, projection["eventType"], now,
        )
        return _response(runtime, {"ok": True, "ignored": True}, 202)
    subscription_bi = await runtime.blind(
        "polar-subscription:" + projection["subscriptionId"])
    product_bi = await runtime.blind(
        "polar-product:" + projection["productId"])
    stored_status = projection["status"] or "inactive"
    sealed = await runtime.seal({
        "subscriptionId": projection["subscriptionId"],
        "productId": projection["productId"],
        "cancelAtPeriodEnd": projection["cancelAtPeriodEnd"],
    })
    statements = [(
        "INSERT INTO polar_memberships"
        "(account_bi,subscription_bi,product_bi,data,tier,status,"
        "current_period_end,provider_modified_at,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(subscription_bi) DO UPDATE SET "
        "product_bi=excluded.product_bi,data=excluded.data,tier=excluded.tier,"
        "status=excluded.status,current_period_end=excluded.current_period_end,"
        "provider_modified_at=excluded.provider_modified_at,"
        "updated_at=excluded.updated_at "
        "WHERE polar_memberships.account_bi=excluded.account_bi AND "
        "excluded.provider_modified_at>=polar_memberships.provider_modified_at",
        (
            account_bi, subscription_bi, product_bi, sealed,
            projection["tier"], stored_status,
            projection["periodEnd"], projection["providerModifiedAt"], now,
        ),
    )]
    # Recompute the authorization row from all subscriptions *inside the same
    # D1 batch*. This handles tier switches, duplicate subscriptions, and an
    # older webhook racing a newer one without revoking a still-paid account.
    statements.append((
        "INSERT INTO role_grants"
        "(account_bi,role,scope_type,scope_bi,granted_by_bi,granted_at,"
        "expires_at,revoked_at) SELECT ?,\'supporting_member\',\'platform\',"
        "\'\',\'\',?,MAX(current_period_end),0 FROM polar_memberships "
        "WHERE account_bi=? AND status IN (\'active\',\'trialing\') "
        "AND current_period_end>? HAVING MAX(current_period_end)>? "
        "ON CONFLICT(account_bi,role,scope_type,scope_bi) DO UPDATE SET "
        "granted_by_bi=\'\',granted_at=excluded.granted_at,"
        "expires_at=excluded.expires_at,revoked_at=0",
        (account_bi, now, account_bi, now, now),
    ))
    statements.append((
        "UPDATE role_grants SET revoked_at=? WHERE account_bi=? AND "
        "role=\'supporting_member\' AND scope_type=\'platform\' "
        "AND scope_bi=\'\' AND revoked_at=0 AND NOT EXISTS ("
        "SELECT 1 FROM polar_memberships WHERE account_bi=? "
        "AND status IN (\'active\',\'trialing\') AND current_period_end>?)",
        (now, account_bi, account_bi, now),
    ))
    statements.append((
        "INSERT INTO polar_webhook_events"
        "(event_id,event_type,received_at) VALUES(?,?,?)",
        (event_id, projection["eventType"], now),
    ))
    await runtime.batch(statements)
    return _response(runtime, {"ok": True}, 202)


async def handle(runtime, action):
    action = str(action or "").strip().lower()
    method = runtime.method()
    allowed = {
        "account": "GET",
        "checkout": "POST",
        "portal": "POST",
        "webhook": "POST",
    }
    if action not in allowed:
        return _response(runtime, {"error": "not_found"}, 404)
    if method != allowed[action]:
        return _response(
            runtime, {"error": "method_not_allowed"}, 405, allowed[action])
    config = normalize_config(runtime.config())
    if action == "account":
        await runtime.ensure_schema()
        return await _account_status(runtime, config)
    if action == "checkout":
        await runtime.ensure_schema()
        return await _checkout(runtime, config)
    if action == "portal":
        await runtime.ensure_schema()
        return await _portal(runtime, config)
    return await _webhook(runtime, config)
