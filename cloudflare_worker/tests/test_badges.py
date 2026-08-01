#!/usr/bin/env python3
"""Achievement badges contract checks (adhoc #370).

Similar in shape to org teams (test_orgs_teams.py): a fixed public badge
catalog (src/badges.py) that people can earn automatically at a handful of
real platform events, or that a platform administrator can grant/revoke by
hand. These tests load the real helpers out of src/entry.py (AST extraction)
and pin:

  * the /api/badges route table and the badge_awards table/migration exist;
  * badges.py's catalog + slug validation are pure and total;
  * account_badges_handler: GET is public, POST/DELETE require a platform-
    administrator session and a known slug;
  * _award_badge is an idempotent, catalog-validated INSERT OR IGNORE;
  * the four automatic-award events (first 100 signups, an hour in the World,
    the first referral signup, becoming a mirror operator) each call
    _award_badge with the right slug at the right, single choke-point event.

Run: python3 -m pytest cloudflare_worker/tests/test_badges.py
"""

import ast
import asyncio
import importlib.util
from pathlib import Path

from worker_test_helpers import json_from_request_double

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
SCHEMA_TEXT = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")


def _module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "src" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


urls = _module("urls")
badges = _module("badges")


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


def _run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


class Clock:
    @staticmethod
    def now():
        return 1_700_000_000_000


def _response(data, status=200, **kwargs):
    return {"status": status, "data": data}




def test_routes_cover_the_badges_api():
    assert urls.BADGES_RE.match("/api/badges")
    m = urls.BADGE_ACCOUNT_RE.match("/api/badges/jett")
    assert m and m.group(1) == "jett"

    assert not urls.BADGES_RE.match("/api/badges/jett")


def test_schema_defines_badge_awards_table_and_migration_exists():
    assert "CREATE TABLE IF NOT EXISTS badge_awards" in SCHEMA_TEXT
    assert "idx_badge_awards_account" in SCHEMA_TEXT
    assert (ROOT / "migrations" / "0084_badge_awards.sql").exists()




def test_catalog_is_a_fixed_known_set():
    slugs = {entry["slug"] for entry in badges.public_catalog()}
    assert slugs == {
        "first_100_users", "world_first_hour", "first_referral",
        "mirror_operator",
    }
    for entry in badges.public_catalog():
        assert entry["name"] and entry["description"] and entry["icon"]


def test_normalize_badge_slug_only_accepts_catalog_entries():
    assert badges.normalize_badge_slug("MIRROR_OPERATOR") == "mirror_operator"
    assert badges.normalize_badge_slug("  first_referral  ") == "first_referral"
    for bogus in ("", None, "nope", "first_100_users; DROP TABLE users", "a"):
        assert badges.normalize_badge_slug(bogus) == ""


def test_badge_definition_round_trips_the_catalog():
    info = badges.badge_definition("world_first_hour")
    assert info["name"] == "World Explorer"
    assert badges.badge_definition("bogus") is None




def _award_ns():
    writes = []

    async def ensure_schema(env):
        return None

    async def d1_run(env, sql, *params):
        writes.append((sql, params))

    ns = _load("_award_badge", extra_globals={
        "badge_catalog": badges,
        "ensure_schema": ensure_schema,
        "d1_run": d1_run,
        "Date": Clock,
    })
    return ns["_award_badge"], writes


def test_award_badge_inserts_or_ignores_a_valid_catalog_slug():
    award, writes = _award_ns()
    _run(award(None, "jett", "jett-bi", "mirror_operator"))
    assert len(writes) == 1
    sql, params = writes[0]
    assert "INSERT OR IGNORE INTO badge_awards" in sql
    assert params == (
        "mirror_operator", "jett-bi", "jett", "system", 1_700_000_000_000)


def test_award_badge_records_the_granting_admin():
    award, writes = _award_ns()
    _run(award(None, "jett", "jett-bi", "first_referral", granted_by="alice"))
    assert writes[0][1][3] == "alice"


def test_award_badge_is_a_noop_for_unknown_slug_or_missing_account():
    award, writes = _award_ns()
    _run(award(None, "jett", "jett-bi", "not-a-badge"))
    _run(award(None, "jett", "", "mirror_operator"))
    assert writes == []




def _account_badges_ns(is_admin, account_exists=True, existing_awards=None):
    writes = []
    audits = []

    async def ensure_schema(env):
        return None

    async def account_row(env, name):
        if not account_exists:
            return "", None
        return name + "-bi", {"name": name}

    async def session_record(env, request, data=None):
        return "actor-bi", {"name": "actor"}

    async def has_role(env, name, role):
        assert role == "platform_administrator"
        return is_admin

    async def audit(env, actor, action, target_type, target, outcome,
                    details=None):
        audits.append({"actor": actor, "action": action, "target": target,
                       "outcome": outcome, "details": details or {}})

    async def award_badge(env, name, name_bi, slug, granted_by="system"):
        writes.append(("award", name, name_bi, slug, granted_by))

    async def d1_run(env, sql, *params):
        writes.append(("d1_run", sql, params))

    async def d1_all(env, sql, *params):
        return existing_awards or []

    ns = _load(
        "account_badges_handler", "_account_badges",
        extra_globals={
            "MAX_NODE_NAME": 63,
            "badge_catalog": badges,
            "ensure_schema": ensure_schema,
            "method_name": lambda request: request.method,
            "clean_string": lambda value, size: str(value or "")[:size],
            "valid_node_name": lambda name: bool(name) and name.isalnum(),
            "_account_row": account_row,
            "_account_session_record": session_record,
            "_has_role": has_role,
            "_audit_sensitive_action": audit,
            "_award_badge": award_badge,
            "d1_run": d1_run,
            "d1_all": d1_all,
            "json_response": _response,
        },
    )
    return ns["account_badges_handler"], writes, audits


class _Req:
    def __init__(self, method, body=None):
        self.method = method
        self._body = body or {}

    async def json(self):
        return self._body


def test_get_account_badges_is_public_and_lists_catalog_entries():
    handler, writes, audits = _account_badges_ns(
        is_admin=False,
        existing_awards=[{"badge_slug": "mirror_operator",
                          "granted_by": "system", "created_at": 42}])
    response = _run(handler(None, _Req("GET"), "jett"))
    assert response["status"] == 200
    assert response["data"]["account"] == "jett"
    assert response["data"]["badges"] == [{
        "slug": "mirror_operator",
        "name": "Mirror Operator",
        "description": badges.badge_definition("mirror_operator")["description"],
        "icon": badges.badge_definition("mirror_operator")["icon"],
        "grantedBy": "system",
        "awardedAt": 42,
    }]
    assert writes == []
    assert audits == []


def test_get_unknown_account_is_not_found():
    handler, _writes, _audits = _account_badges_ns(
        is_admin=False, account_exists=False)
    response = _run(handler(None, _Req("GET"), "ghost"))
    assert response == {"status": 404, "data": {"error": "not_found"}}


def test_grant_requires_platform_administrator():
    handler, writes, audits = _account_badges_ns(is_admin=False)
    response = _run(handler(
        None, _Req("POST", {"slug": "mirror_operator"}), "jett"))
    assert response["status"] == 403
    assert writes == []
    assert audits[-1]["outcome"] == "denied"


def test_grant_rejects_unknown_slug():
    handler, writes, _audits = _account_badges_ns(is_admin=True)
    response = _run(handler(
        None, _Req("POST", {"slug": "not-a-real-badge"}), "jett"))
    assert response == {"status": 400, "data": {"error": "invalid_badge"}}
    assert writes == []


def test_admin_can_grant_and_revoke_a_catalog_badge():
    handler, writes, audits = _account_badges_ns(is_admin=True)
    granted = _run(handler(
        None, _Req("POST", {"slug": "mirror_operator"}), "jett"))
    assert granted["status"] == 201
    assert granted["data"] == {
        "ok": True, "account": "jett", "slug": "mirror_operator",
        "granted": True}
    assert writes == [("award", "jett", "jett-bi", "mirror_operator", "actor")]
    assert audits[-1] == {
        "actor": "actor", "action": "badge.grant",
        "target": "jett/mirror_operator", "outcome": "success", "details": {}}

    handler, writes, audits = _account_badges_ns(is_admin=True)
    revoked = _run(handler(
        None, _Req("DELETE", {"slug": "mirror_operator"}), "jett"))
    assert revoked["data"] == {
        "ok": True, "account": "jett", "slug": "mirror_operator",
        "revoked": True}
    assert writes == [(
        "d1_run", "DELETE FROM badge_awards WHERE badge_slug=? AND account_bi=?",
        ("mirror_operator", "jett-bi"))]
    assert audits[-1]["action"] == "badge.revoke"




def _function_source(name):
    fn = next(
        node for node in ast.parse(ENTRY_TEXT).body
        if isinstance(node, ast.AsyncFunctionDef) and node.name == name
    )
    return ast.unparse(fn)


def test_first_100_users_badge_is_awarded_from_the_signup_path():
    source = _function_source("_account_signup")
    assert "'first_100_users'" in source
    assert "FIRST_100_USERS_LIMIT" in source
    assert "_award_badge(env, name, name_bi, 'first_100_users')" in source


def test_first_referral_badge_is_awarded_once_the_referrer_converts_one_signup():
    source = _function_source("_record_referral_signup")
    assert "_award_badge(env, ref_name, ref_bi, 'first_referral')" in source
    assert "if signups == 1:" in source


def test_world_first_hour_badge_is_awarded_from_the_world_ticket_handler():
    source = _function_source("world_ticket_handler")
    assert "WORLD_FIRST_HOUR_BADGE_MS" in source
    assert (
        "_award_badge(env, claim['name'], account_bi, 'world_first_hour')"
        in source)


def test_mirror_operator_badge_is_awarded_the_first_time_a_node_is_linked():
    source = _function_source("_link_node_to_user")
    assert "was_operator = bool(nodes)" in source
    assert "if not was_operator:" in source
    assert "_award_badge(env, user_name, user_bi, 'mirror_operator')" in source
