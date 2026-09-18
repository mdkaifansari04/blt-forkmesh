#!/usr/bin/env python3
"""Organization email invitations: send/list/revoke + accept contracts.

The handlers are AST-extracted from src/entry.py (same harness as
test_orgs_teams.py) and driven with stubbed I/O, so every assertion is about
behavior: which SQL runs with which params, which error code surfaces, what
the caller may and may not do.
"""

import ast
import asyncio
import hashlib
import hmac as hmac_mod
import importlib.util
from pathlib import Path

from worker_test_helpers import json_from_request_double

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _module(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "src" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


urls = _module("urls")


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {n.name for n in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    namespace.setdefault("bounded_json_request", json_from_request_double)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _constant(name):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    for node in tree.body:
        if (isinstance(node, ast.Assign) and
                any(getattr(t, "id", "") == name for t in node.targets)):
            try:
                return ast.literal_eval(node.value)
            except ValueError:
                # Constant arithmetic like 7 * 24 * 60 * 60 * 1000.
                return eval(  # noqa: S307 - source-pinned constant expr
                    compile(ast.Expression(node.value), str(ENTRY), "eval"),
                    {"__builtins__": {}}, {})
    raise AssertionError(name + " not found")


def _run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


class _Response:
    def __init__(self, body, status):
        self.body = body
        self.status = status


def _json_response(payload, status=200, **kwargs):
    return _Response(payload, status)


class _Request:
    def __init__(self, method="POST", body=None, url="https://x.test/api"):
        self.method = method
        self._body = body or {}
        self.url = url
        self.headers = {}

    async def json(self):
        return self._body


class _Clock:
    NOW = 10_000_000_000

    @staticmethod
    def now():
        return _Clock.NOW


DAY_MS = 24 * 60 * 60 * 1000


def _harness(
    *,
    org_name="owasp-blt",
    caller="kaif",
    caller_role="owner",
    body=None,
    method="POST",
    existing_account_bi=None,
    pending_count=0,
    daily_count=0,
    member_count=1,
    send_result=True,
    insert_raises=None,
    invite_rows=None,
):
    """Load org_invitations_handler with a scripted environment."""
    writes = []
    audits = []
    notifications = []
    emails = []

    async def noop(*args, **kwargs):
        return None

    async def org_row(env, org):
        if org == "owasp-blt":
            return "org-bi", {"name": "owasp-blt", "data": "enc"}
        return "", None

    async def session_record(env, request, data=None):
        if caller:
            return "bi:" + caller, {"name": caller}
        return "", None

    async def org_role(env, org_bi, account):
        assert org_bi == "org-bi"
        return caller_role if account == caller else ""

    async def blind_index(env, value):
        return "bi:" + value

    async def d1_first(env, sql, *params):
        if "FROM users WHERE email_bi=?" in sql:
            if existing_account_bi:
                return {"user_bi": existing_account_bi,
                        "username": "resident"}
            return None
        if "COUNT(*)" in sql and "org_invitations" in sql:
            if "inviter_bi" in sql:
                return {"n": daily_count}
            return {"n": pending_count}
        if "COUNT(*)" in sql and "org_members" in sql:
            return {"n": member_count}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_all(env, sql, *params):
        if "FROM org_invitations" in sql:
            return list(invite_rows or [])
        raise AssertionError("unexpected d1_all: " + sql)

    async def d1_run(env, sql, *params):
        writes.append((" ".join(sql.split()), params))
        if insert_raises and "INSERT INTO org_invitations" in sql:
            raise insert_raises

    async def send_email(env, to_email, subject, text, html=None,
                         from_email=None, from_name=None,
                         email_kind="account"):
        emails.append({"to": to_email, "subject": subject,
                       "from_name": from_name, "html": html})
        return send_result

    async def audit(env, actor, action, target_type, target, outcome,
                    details=None):
        audits.append((action, outcome))

    async def notify(env, recipient, kind, title, **kwargs):
        notifications.append((recipient, kind))
        return True

    async def encrypt_row(env, payload):
        return "enc:" + str(sorted(payload))

    async def decrypt_row(env, blob):
        return {"email": "resident@owasp.org",
                "maskedEmail": "r**@owasp.org", "role": "member"}

    ns = _load(
        "org_invitations_handler",
        "_org_invitation_token",
        extra_globals={
            "ensure_schema": noop,
            "method_name": lambda request: request.method,
            "json_response": _json_response,
            "_org_row": org_row,
            "_account_session_record": session_record,
            "_org_role": org_role,
            "blind_index": blind_index,
            "d1_first": d1_first,
            "d1_all": d1_all,
            "d1_run": d1_run,
            "_send_email": send_email,
            "_audit_sensitive_action": audit,
            "enqueue_notification": notify,
            "encrypt_row": encrypt_row,
            "decrypt_row": decrypt_row,
            "clean_string": lambda v, n: str(v or "")[:n],
            "MAX_NODE_NAME": 63,
            "Date": _Clock,
            "hmac": hmac_mod,
            "hashlib": hashlib,
            "_account_session_secret": lambda env: b"secret",
            "_public_base_url": lambda env, request=None: "https://x.test",
            "_forkmesh_email_card_html":
                lambda heading, intro, body, footer="", **k:
                "".join((heading, intro, body, footer)),
            "_forkmesh_email_action_html":
                lambda label, url: "<a href='%s'>%s</a>" % (url, label),
            "_org_invite_normalize_email": lambda value: (
                str(value or "").strip().lower()
                if "@" in str(value or "") else ""),
            "_org_invite_mask_email": lambda value: "masked@" + str(
                value or "").partition("@")[2],
            "ORG_ROLES": ("owner", "admin", "member"),
            "MAX_ORG_MEMBERS": 200,
            "MAX_ORG_PENDING_INVITES": _constant("MAX_ORG_PENDING_INVITES"),
            "ORG_INVITE_TTL_MS": _constant("ORG_INVITE_TTL_MS"),
            "ORG_INVITE_DAILY_LIMIT": _constant("ORG_INVITE_DAILY_LIMIT"),
        },
    )
    request = _Request(method=method, body=body or {})
    result = _run(ns["org_invitations_handler"](None, request, org_name))
    return {
        "response": result,
        "writes": writes,
        "audits": audits,
        "notifications": notifications,
        "emails": emails,
        "ns": ns,
    }


# --- Routes ------------------------------------------------------------------


def test_routes_cover_the_org_invitations_api():
    assert urls.ORG_INVITES_RE.match("/api/orgs/owasp-blt/invitations")
    action = urls.ORG_INVITE_ACTION_RE.match(
        "/api/orgs/owasp-blt/invitations/orginv_abc123/accept")
    assert action and action.group(1) == "owasp-blt"
    assert action.group(2) == "orginv_abc123"
    assert action.group(3) == "accept"
    assert urls.ORG_INVITE_ACTION_RE.match(
        "/api/orgs/owasp-blt/invitations/orginv_abc123/decline")
    assert not urls.ORG_INVITES_RE.match("/api/orgs/owasp-blt/members")
    assert not urls.ORG_INVITE_ACTION_RE.match(
        "/api/orgs/owasp-blt/invitations/orginv_abc123/delete")


# --- Token -------------------------------------------------------------------


def test_org_invitation_token_binds_org_id_email_and_expiry():
    ns = _harness()["ns"]

    def token(org="owasp-blt", invite="inv1", email="a@b.c", expires=123):
        return _run(ns["_org_invitation_token"](
            None, org, invite, email, expires))

    base = token()
    assert base == token()  # deterministic
    assert base != token(org="other-org")
    assert base != token(invite="inv2")
    assert base != token(email="x@b.c")
    assert base != token(expires=999)
    # Domain separation from the contributor-invitation token family.
    contributor_style = hmac_mod.new(
        b"secret",
        ("forkmesh-contributor-invitation-v1\nowasp-blt\ninv1\na@b.c"
         ).encode(),
        "sha256").hexdigest()
    assert base != contributor_style


# --- Authorization -----------------------------------------------------------


def test_unknown_org_is_404():
    out = _harness(org_name="no-such-org",
                   body={"email": "new@owasp.org", "role": "member"})
    assert out["response"].status == 404


def test_invite_send_requires_owner_or_admin():
    for role, status in (("owner", 200), ("admin", 200), ("member", 403),
                         ("", 403)):
        out = _harness(caller_role=role,
                       body={"email": "new@owasp.org", "role": "member"})
        assert out["response"].status == status, role


def test_only_owners_hand_out_administrative_roles_via_invites():
    denied = _harness(caller_role="admin",
                      body={"email": "new@owasp.org", "role": "admin"})
    assert denied["response"].status == 403
    for elevated in ("admin", "owner"):
        allowed = _harness(caller_role="owner",
                           body={"email": "new@owasp.org", "role": elevated})
        assert allowed["response"].status == 200, elevated


def test_unknown_role_and_bad_email_are_rejected():
    assert _harness(body={"email": "new@owasp.org", "role": "emperor"})[
        "response"].status == 400
    assert _harness(body={"email": "not-an-email", "role": "member"})[
        "response"].status == 400


# --- Caps --------------------------------------------------------------------


def test_invite_caps_enforced_at_each_boundary():
    pending = _harness(pending_count=50,
                       body={"email": "new@owasp.org", "role": "member"})
    assert pending["response"].status == 429
    assert pending["response"].body["error"] == "too_many_invitations"

    daily = _harness(daily_count=25,
                     body={"email": "new@owasp.org", "role": "member"})
    assert daily["response"].status == 429
    assert daily["response"].body["error"] == "daily_invitation_limit"

    full = _harness(member_count=200,
                    body={"email": "new@owasp.org", "role": "member"})
    assert full["response"].status == 429
    assert full["response"].body["error"] == "too_many_members"


# --- Send paths --------------------------------------------------------------


def test_existing_account_email_short_circuits_to_direct_add():
    out = _harness(existing_account_bi="bi:resident",
                   body={"email": "resident@owasp.org", "role": "member"})
    assert out["response"].status == 200
    assert out["response"].body.get("added") is True
    member_writes = [w for w in out["writes"] if "org_members" in w[0]]
    assert member_writes, "existing account must land in org_members"
    assert not any("org_invitations" in w[0] for w in out["writes"])
    assert out["notifications"] == [("resident", "org_invite")]
    assert out["emails"] == []


def test_new_email_inserts_pending_row_then_sends_branded_email():
    out = _harness(body={"email": "new@owasp.org", "role": "member"})
    assert out["response"].status == 200
    sqls = [sql for sql, _ in out["writes"]]
    assert any("INSERT INTO org_invitations" in sql for sql in sqls)
    assert any("UPDATE org_invitations SET status='sent'" in sql or
               "SET sent_at" in sql for sql in sqls)
    assert len(out["emails"]) == 1
    email = out["emails"][0]
    assert email["from_name"] == "OWASP BLT"
    assert "owasp-blt" in email["subject"]
    assert "/signup?invite=" in email["html"]
    # The insert happens before the send (phantom-free ordering).
    insert_index = next(i for i, sql in enumerate(sqls)
                        if "INSERT INTO org_invitations" in sql)
    assert insert_index == 0


def test_send_failure_rolls_back_the_pending_row():
    out = _harness(send_result=False,
                   body={"email": "new@owasp.org", "role": "member"})
    assert out["response"].status == 202
    assert out["response"].body.get("deliveryConfigured") is False
    sqls = [sql for sql, _ in out["writes"]]
    assert any("DELETE FROM org_invitations" in sql and "status='pending'"
               in sql for sql in sqls)


def test_duplicate_pending_invite_maps_to_conflict():
    class Boom(Exception):
        pass

    out = _harness(insert_raises=Boom("UNIQUE constraint failed"),
                   body={"email": "new@owasp.org", "role": "member"})
    assert out["response"].status == 409
    assert out["response"].body["error"] == "invitation_pending"


# --- List / revoke -----------------------------------------------------------


def test_invite_list_is_role_gated_and_masks_emails():
    row = {"id": "orginv_1", "role": "member", "status": "pending",
           "created_at": 1, "expires_at": 2, "data": "enc"}
    out = _harness(method="GET", invite_rows=[row])
    assert out["response"].status == 200
    listed = out["response"].body["invitations"][0]
    assert listed["maskedEmail"] == "r**@owasp.org"
    assert "email" not in listed
    assert _harness(method="GET", caller_role="member")[
        "response"].status == 403


def test_revoke_marks_the_row_revoked():
    out = _harness(method="DELETE", body={"id": "orginv_1"})
    assert out["response"].status == 200
    assert any("UPDATE org_invitations SET status='revoked'" in sql
               for sql, _ in out["writes"])


def test_email_normalizer_rejects_whitespace_variants_and_bad_addresses():
    # Two addresses differing only by whitespace must not become two distinct
    # blind indexes, or the one-pending-invite-per-address index is bypassed.
    ns = _load(
        "_org_invite_normalize_email",
        extra_globals={"_WAITLIST_EMAIL_RE": __import__("re").compile(
            r"^[A-Za-z0-9.!#$%&\'*+/=?^_`{|}~-]+@"
            r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$")},
    )["_org_invite_normalize_email"]
    assert ns("Alice@Owasp.org") == "alice@owasp.org"
    assert ns("  alice@owasp.org\n") == "alice@owasp.org"
    assert ns("alice@owasp.org") == ns(" ALICE@owasp.org ")
    for bad in ("", "not-an-email", "a@b.com\nBcc: x@y.com", "a" * 250 + "@b.com"):
        assert ns(bad) == "", bad


# --- Accept / decline --------------------------------------------------------


def _action_harness(
    *,
    action="accept",
    token="tok",
    caller="alice",
    invite_status="pending",
    invite_role="member",
    expires_delta=1000,
    member_count=1,
    existing_role=None,
    method="POST",
):
    """Load org_invitation_action_handler with a scripted invite row."""
    import hashlib as _hashlib

    writes = []
    notifications = []
    audits = []
    good_digest = _hashlib.sha256(b"tok").hexdigest()

    async def noop(*args, **kwargs):
        return None

    async def org_row(env, org):
        if org == "owasp-blt":
            return "org-bi", {"name": "owasp-blt", "data": "enc"}
        return "", None

    async def session_record(env, request, data=None):
        if caller:
            return "bi:" + caller, {"name": caller}
        return "", None

    async def blind_index(env, value):
        return "bi:" + value

    async def d1_first(env, sql, *params):
        if "FROM org_invitations WHERE id=?" in sql:
            return {
                "id": "orginv_1", "org_bi": "org-bi", "role": invite_role,
                "status": invite_status, "data": "enc",
                "created_at": 1,
                "expires_at": _Clock.NOW + expires_delta,
            }
        if "COUNT(*)" in sql and "org_members" in sql:
            return {"n": member_count}
        if "SELECT role FROM org_members" in sql:
            if existing_role:
                return {"role": existing_role}
            return None
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(env, sql, *params):
        writes.append((" ".join(sql.split()), params))

    async def decrypt_row(env, blob):
        return {"email": "alice@owasp.org", "maskedEmail": "a**@owasp.org",
                "org": "owasp-blt", "role": invite_role, "inviter": "kaif",
                "actionTokenDigest": good_digest}

    async def notify(env, recipient, kind, title, **kwargs):
        notifications.append((recipient, kind))
        return True

    async def audit(env, actor, action_name, target_type, target, outcome,
                    details=None):
        audits.append((action_name, outcome))

    ns = _load(
        "org_invitation_action_handler",
        extra_globals={
            "ensure_schema": noop,
            "method_name": lambda request: request.method,
            "json_response": _json_response,
            "_org_row": org_row,
            "_account_session_record": session_record,
            "blind_index": blind_index,
            "d1_first": d1_first,
            "d1_run": d1_run,
            "decrypt_row": decrypt_row,
            "enqueue_notification": notify,
            "_audit_sensitive_action": audit,
            "clean_string": lambda v, n: str(v or "")[:n],
            "Date": _Clock,
            "hmac": hmac_mod,
            "hashlib": hashlib,
            "urlparse": __import__("urllib.parse", fromlist=["urlparse"]).urlparse,
            "parse_qs": __import__("urllib.parse", fromlist=["parse_qs"]).parse_qs,
            "MAX_ORG_MEMBERS": 200,
        },
    )
    request = _Request(
        method=method,
        url="https://x.test/api/orgs/owasp-blt/invitations/orginv_1/"
            + action + "?token=" + token)
    result = _run(ns["org_invitation_action_handler"](
        None, request, "owasp-blt", "orginv_1", action))
    return {"response": result, "writes": writes,
            "notifications": notifications, "audits": audits}


def test_accept_get_previews_without_mutating():
    out = _action_harness(method="GET")
    assert out["response"].status == 200
    body = out["response"].body
    assert body["confirmationRequired"] is True
    assert body["org"] == "owasp-blt"
    assert body["role"] == "member"
    assert body["email"] == "alice@owasp.org"  # signup prefill; token-gated
    assert body["inviter"] == "kaif"
    assert out["writes"] == []


def test_accept_rejects_a_bad_token():
    out = _action_harness(token="forged")
    assert out["response"].status == 403
    assert out["response"].body["error"] == "invalid_invitation_token"
    assert out["writes"] == []


def test_accept_requires_a_session():
    out = _action_harness(caller="")
    assert out["response"].status == 401
    assert out["response"].body["error"] == "invite_signin_required"


def test_accept_is_single_use_and_expiry_bound():
    used = _action_harness(invite_status="accepted")
    assert used["response"].status == 409
    assert used["response"].body["error"] == "invitation_used"

    expired = _action_harness(expires_delta=-1)
    assert expired["response"].status == 410
    assert expired["response"].body["error"] == "invitation_expired"
    assert any("SET status='expired'" in sql for sql, _ in expired["writes"])


def test_accept_joins_the_member_and_notifies_the_inviter():
    out = _action_harness()
    assert out["response"].status == 200
    assert out["response"].body["ok"] is True
    sqls = [sql for sql, _ in out["writes"]]
    member_insert = next(
        (i for i, sql in enumerate(sqls) if "INSERT INTO org_members" in sql),
        None)
    assert member_insert is not None
    accepted = next(
        (i for i, sql in enumerate(sqls)
         if "SET status='accepted'" in sql), None)
    assert accepted is not None
    assert ("kaif", "org_invite") in out["notifications"]


def test_accept_never_lowers_an_existing_higher_role():
    # An invite may ADD a missing member but must never demote someone who is
    # already an owner/admin to the invite's role, so the membership upsert
    # has to be DO NOTHING - a DO UPDATE SET role would overwrite it.
    out = _action_harness(existing_role="owner")
    assert out["response"].status == 200
    member_inserts = [sql for sql, _ in out["writes"]
                      if "INSERT INTO org_members" in sql]
    assert len(member_inserts) == 1
    assert "DO NOTHING" in member_inserts[0]
    assert "role=excluded.role" not in member_inserts[0]


def test_accept_recheck_of_the_member_cap():
    out = _action_harness(member_count=200)
    assert out["response"].status == 429
    assert out["response"].body["error"] == "too_many_members"


def test_decline_marks_declined_without_joining():
    out = _action_harness(action="decline")
    assert out["response"].status == 200
    sqls = [sql for sql, _ in out["writes"]]
    assert any("SET status='declined'" in sql for sql in sqls)
    assert not any("INSERT INTO org_members" in sql for sql in sqls)


if __name__ == "__main__":
    for test in (
        test_routes_cover_the_org_invitations_api,
        test_org_invitation_token_binds_org_id_email_and_expiry,
        test_unknown_org_is_404,
        test_invite_send_requires_owner_or_admin,
        test_only_owners_hand_out_administrative_roles_via_invites,
        test_unknown_role_and_bad_email_are_rejected,
        test_invite_caps_enforced_at_each_boundary,
        test_existing_account_email_short_circuits_to_direct_add,
        test_new_email_inserts_pending_row_then_sends_branded_email,
        test_send_failure_rolls_back_the_pending_row,
        test_duplicate_pending_invite_maps_to_conflict,
        test_invite_list_is_role_gated_and_masks_emails,
        test_revoke_marks_the_row_revoked,
        test_accept_get_previews_without_mutating,
        test_accept_rejects_a_bad_token,
        test_accept_requires_a_session,
        test_accept_is_single_use_and_expiry_bound,
        test_accept_joins_the_member_and_notifies_the_inviter,
        test_accept_never_lowers_an_existing_higher_role,
        test_accept_recheck_of_the_member_cap,
        test_decline_marks_declined_without_joining,
    ):
        test()
        print("PASS", test.__name__)
