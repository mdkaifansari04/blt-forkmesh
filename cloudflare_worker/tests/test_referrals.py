#!/usr/bin/env python3
"""Referral-program contracts: counters, attribution, leaderboard, surfaces."""

import ast
import asyncio
import importlib.util
import re
import sqlite3
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
_OG_SPEC = importlib.util.spec_from_file_location(
    "og_card", ROOT / "src" / "og_card.py")
og_card = importlib.util.module_from_spec(_OG_SPEC)
_OG_SPEC.loader.exec_module(og_card)
ENTRY_PATH = ROOT / "src" / "entry.py"
ENTRY = ENTRY_PATH.read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
MIGRATION = ROOT / "migrations" / "0081_referral_stats.sql"
SIGNUP_JS = (ROOT / "public" / "signup.js").read_text(encoding="utf-8")
REFERRALS_JS = (ROOT / "public" / "referrals.js").read_text(encoding="utf-8")
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (
    ROOT / "public" / "world" / "world-scene.js"
).read_text(encoding="utf-8")

NAME_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")


def _top_level_node(name):
    for node in ast.parse(ENTRY, filename=str(ENTRY_PATH)).body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name == name:
                return node
    raise AssertionError(f"{name} not found")


def _entry_constant(name):
    for node in ast.parse(ENTRY, filename=str(ENTRY_PATH)).body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == name:
                    return ast.literal_eval(node.value)
    raise AssertionError(f"{name} not found")


def _load_function(name, namespace):
    module = ast.fix_missing_locations(
        ast.Module(body=[_top_level_node(name)], type_ignores=[]))
    exec(compile(module, str(ENTRY_PATH), "exec"), namespace)
    return namespace[name]


ACCOUNTS = {
    "alice": ("bi-alice", {"status": "active", "kind": "user"}),
    "bob": ("bi-bob", {"status": "active", "kind": "user"}),
    "nodey": ("bi-nodey", {"status": "active", "kind": "node"}),
    "ghost": ("bi-ghost", {"status": "pending", "kind": "user"}),
}


def _namespace(db):
    async def d1_run(_env, sql, *args):
        db.execute(sql, args)
        db.commit()

    async def d1_all(_env, sql, *args):
        return [dict(row) for row in db.execute(sql, args).fetchall()]

    async def _account_row(_env, name):
        return ACCOUNTS.get(name, ("", None))

    async def ensure_schema(_env):
        return None

    cache_puts = []

    async def edge_cache_match(_key):
        return None

    async def edge_cache_put(key, resp):
        cache_puts.append((key, resp))

    def json_response(data, status=200, cache_seconds=None,
                      cache_control=None, extra_headers=None):
        return SimpleNamespace(
            data=data, status=status, cache_seconds=cache_seconds)

    class Response:
        def __init__(self, body, status=200, headers=None):
            self.body = body
            self.status = status
            self.headers = headers or {}

    async def d1_first(_env, sql, *args):
        row = db.execute(sql, args).fetchone()
        return dict(row) if row else None

    namespace = {
        "d1_run": d1_run,
        "d1_all": d1_all,
        "d1_first": d1_first,
        "_account_row": _account_row,
        "_account_kind": lambda rec: rec.get("kind", "user"),
        "ensure_schema": ensure_schema,
        "edge_cache_match": edge_cache_match,
        "edge_cache_put": edge_cache_put,
        "json_response": json_response,
        "Response": Response,
        "quote": quote,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda name: bool(NAME_RE.match(name or "")),
        "MAX_NODE_NAME": 63,
        "Date": SimpleNamespace(now=lambda: 1_720_000_000_000),
        "REFERRAL_LEADERBOARD_CACHE_KEY": "https://forkmesh.internal/test",
        "REFERRAL_LEADERBOARD_TTL": 30,
        "REFERRAL_LEADERBOARD_LIMIT": 10,
        "REFERRAL_PREVIEW_AGENTS": _entry_constant("REFERRAL_PREVIEW_AGENTS"),
        "og_card": og_card,
        "_ap_origin": lambda _env, _request=None: "https://forkmesh.com",
        "_html_escape": lambda text: (
            str(text or "").replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;")),
    }
    for name in ("_referral_account", "_referral_bump", "_referral_counts",
                 "_is_link_preview_agent", "_referral_preview_page",
                 "referral_click", "_record_referral_signup",
                 "referral_leaderboard"):
        _load_function(name, namespace)
    namespace["_cache_puts"] = cache_puts
    return namespace


def _stats_db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.executescript(MIGRATION.read_text(encoding="utf-8"))
    return db


def test_referral_migration_is_counters_only_and_matches_lazy_schema():
    db = _stats_db()
    columns = {
        row[1] for row in db.execute(
            "PRAGMA table_info(referral_stats)").fetchall()
    }
    assert columns == {"referrer_bi", "name", "clicks", "signups", "last_ts"}
    assert "CREATE TABLE IF NOT EXISTS referral_stats" in SCHEMA
    # Privacy contract: only per-referrer aggregates, never who was referred.
    assert "referred_bi" not in columns
    assert "ip_address" not in columns


def test_click_counts_active_users_and_redirects_to_signup():
    db = _stats_db()
    ns = _namespace(db)
    resp = asyncio.run(ns["referral_click"](object(), "alice"))
    assert resp.status == 302
    assert resp.headers["location"] == "/signup?ref=alice"
    assert "no-store" in resp.headers["cache-control"]
    resp = asyncio.run(ns["referral_click"](object(), "alice"))
    assert resp.headers["location"] == "/signup?ref=alice"
    row = db.execute(
        "SELECT clicks, signups FROM referral_stats "
        "WHERE referrer_bi='bi-alice'").fetchone()
    assert (row["clicks"], row["signups"]) == (2, 0)


def test_click_ignores_unknown_node_and_inactive_referrers():
    db = _stats_db()
    ns = _namespace(db)
    for raw in ("nobody", "nodey", "ghost", "Not A Name!", ""):
        resp = asyncio.run(ns["referral_click"](object(), raw))
        assert resp.status == 302
        assert resp.headers["location"] == "/signup"
    assert db.execute("SELECT COUNT(*) FROM referral_stats").fetchone()[0] == 0


def test_signup_attribution_credits_referrer_but_never_self():
    db = _stats_db()
    ns = _namespace(db)
    asyncio.run(ns["_record_referral_signup"](object(), "carol", "alice"))
    asyncio.run(ns["_record_referral_signup"](object(), "alice", "alice"))
    asyncio.run(ns["_record_referral_signup"](object(), "carol", "nodey"))
    asyncio.run(ns["_record_referral_signup"](object(), "carol", "nobody"))
    rows = db.execute(
        "SELECT referrer_bi, clicks, signups FROM referral_stats").fetchall()
    assert [(r["referrer_bi"], r["clicks"], r["signups"]) for r in rows] == [
        ("bi-alice", 0, 1),
    ]


def test_signup_attribution_failures_never_raise():
    db = _stats_db()
    ns = _namespace(db)

    async def broken_d1_run(_env, sql, *args):
        raise RuntimeError("d1 down")

    ns["d1_run"] = broken_d1_run
    asyncio.run(ns["_record_referral_signup"](object(), "carol", "alice"))


def test_leaderboard_orders_by_signups_then_clicks():
    db = _stats_db()
    ns = _namespace(db)
    seed = [
        ("bi-alice", "alice", 5, 2),
        ("bi-bob", "bob", 90, 1),
        ("bi-carol", "carol", 4, 2),
        ("bi-quiet", "quiet", 0, 0),
    ]
    for referrer_bi, name, clicks, signups in seed:
        db.execute(
            "INSERT INTO referral_stats "
            "(referrer_bi, name, clicks, signups, last_ts) VALUES (?,?,?,?,0)",
            (referrer_bi, name, clicks, signups))
    db.commit()
    resp = asyncio.run(ns["referral_leaderboard"](object()))
    assert resp.data["ok"] is True
    assert [(r["name"], r["clicks"], r["signups"])
            for r in resp.data["board"]] == [
        ("alice", 5, 2),
        ("carol", 4, 2),
        ("bob", 90, 1),
    ]
    assert resp.cache_seconds == 30
    assert len(ns["_cache_puts"]) == 1


def _request(user_agent):
    return SimpleNamespace(headers=SimpleNamespace(
        get=lambda key: user_agent if key == "user-agent" else None))


BROWSER_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126 Safari/537.36")


def _seed(db, referrer_bi, name, clicks, signups):
    db.execute(
        "INSERT INTO referral_stats "
        "(referrer_bi, name, clicks, signups, last_ts) VALUES (?,?,?,?,0)",
        (referrer_bi, name, clicks, signups))
    db.commit()


def test_link_preview_crawlers_get_a_card_with_the_live_counters():
    db = _stats_db()
    ns = _namespace(db)
    _seed(db, "bi-alice", "alice", 7, 2)
    resp = asyncio.run(ns["referral_click"](
        object(), "alice",
        _request("http.rb/5.1.1 (Mastodon/4.2.1; +https://mastodon.social/)")))
    assert resp.status == 200
    assert resp.headers["content-type"].startswith("text/html")
    body = resp.body
    assert ('<meta property="og:image" '
            'content="https://forkmesh.com/api/referrals/alice/card.png">'
            in body)
    assert 'content="summary_large_image"' in body
    assert "7 clicks and 2 signups so far." in body
    assert "Join ForkMesh with @alice" in body
    # A human who trips the heuristic still reaches the signup funnel.
    assert 'url=/signup?ref=alice' in body
    # Preview fetches never move the counters, and are never cached stale.
    assert "no-store" in resp.headers["cache-control"]
    row = db.execute(
        "SELECT clicks FROM referral_stats WHERE referrer_bi='bi-alice'"
    ).fetchone()
    assert row["clicks"] == 7


def test_browsers_still_bounce_to_signup_and_count_the_click():
    db = _stats_db()
    ns = _namespace(db)
    resp = asyncio.run(
        ns["referral_click"](object(), "alice", _request(BROWSER_UA)))
    assert resp.status == 302
    assert resp.headers["location"] == "/signup?ref=alice"
    row = db.execute(
        "SELECT clicks FROM referral_stats WHERE referrer_bi='bi-alice'"
    ).fetchone()
    assert row["clicks"] == 1


def test_unknown_referrers_never_render_a_preview():
    db = _stats_db()
    ns = _namespace(db)
    resp = asyncio.run(ns["referral_click"](
        object(), "nobody", _request("Twitterbot/1.0")))
    assert resp.status == 302
    assert resp.headers["location"] == "/signup"


def test_preview_agent_matching_covers_the_common_unfurlers():
    db = _stats_db()
    ns = _namespace(db)
    is_preview = ns["_is_link_preview_agent"]
    for agent in ("Mastodon/4.2.1", "Twitterbot/1.0", "facebookexternalhit/1.1",
                  "Slackbot-LinkExpanding 1.0", "Discordbot/2.0",
                  "TelegramBot (like TwitterBot)", "WhatsApp/2.23",
                  "Iframely/1.3", "Misskey/2024.2", "curl/8.4.0"):
        assert is_preview(_request(agent)), agent
    for agent in (BROWSER_UA, "", None,
                  "Mozilla/5.0 (X11; Linux x86_64) Firefox/126.0"):
        assert not is_preview(_request(agent)), agent
    assert not is_preview(object())  # header access failures fall back to human


def test_preview_counts_read_straight_from_the_counter_row():
    db = _stats_db()
    ns = _namespace(db)
    assert asyncio.run(ns["_referral_counts"](object(), "bi-alice")) == (
        0, 0, 0)
    _seed(db, "bi-alice", "alice", 5, 3)
    assert asyncio.run(ns["_referral_counts"](object(), "bi-alice")) == (
        5, 3, 0)


def test_signup_handler_and_page_carry_the_referral_code():
    assert 'await _record_referral_signup(env, name, data.get("ref", ""))' in ENTRY
    assert "forkmesh.referral" in SIGNUP_JS
    assert "ref: referralCode()" in SIGNUP_JS


def test_web_and_world_surfaces_show_the_board_and_share_link():
    assert '"/api/referrals/leaderboard"' in REFERRALS_JS
    assert "/r/" in REFERRALS_JS
    assert '"/api/leaderboards"' in WORLD
    assert "copyReferralLink" in WORLD
    assert 'title: "REFERRALS"' in SCENE
    assert "leaderboardGridState.referralRows" in SCENE
    assert 'board?.id === "referrals"' in SCENE
    assert "updateReferralLeaderboard" in SCENE


if __name__ == "__main__":
    test_referral_migration_is_counters_only_and_matches_lazy_schema()
    test_click_counts_active_users_and_redirects_to_signup()
    test_click_ignores_unknown_node_and_inactive_referrers()
    test_signup_attribution_credits_referrer_but_never_self()
    test_signup_attribution_failures_never_raise()
    test_leaderboard_orders_by_signups_then_clicks()
    test_link_preview_crawlers_get_a_card_with_the_live_counters()
    test_browsers_still_bounce_to_signup_and_count_the_click()
    test_unknown_referrers_never_render_a_preview()
    test_preview_agent_matching_covers_the_common_unfurlers()
    test_preview_counts_read_straight_from_the_counter_row()
    test_signup_handler_and_page_carry_the_referral_code()
    test_web_and_world_surfaces_show_the_board_and_share_link()
