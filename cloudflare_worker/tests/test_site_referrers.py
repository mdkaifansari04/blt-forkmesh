#!/usr/bin/env python3
"""Inbound website-referral board: what counts, what never does, what shows."""

import ast
import asyncio
import re
import sqlite3
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY_PATH = ROOT / "src" / "entry.py"
ENTRY = ENTRY_PATH.read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
MIGRATION = ROOT / "migrations" / "0084_site_referrers.sql"
REFERRALS_JS = (ROOT / "public" / "referrals.js").read_text(encoding="utf-8")
REFERRALS_HTML = (
    ROOT / "public" / "referrals.html"
).read_text(encoding="utf-8")
PRIVACY = (ROOT / "public" / "privacy.html").read_text(encoding="utf-8")

BROWSER_UA = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126 Safari/537.36")
NAV_HEADERS = {
    "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
    "sec-fetch-dest": "document",
    "sec-fetch-mode": "navigate",
    "user-agent": BROWSER_UA,
}


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
                    value = node.value
                    # frozenset({...}) / re.compile("...") wrap their literal.
                    if isinstance(value, ast.Call) and value.args:
                        literal = ast.literal_eval(value.args[0])
                        return (frozenset(literal)
                                if isinstance(value.func, ast.Name)
                                and value.func.id == "frozenset"
                                else literal)
                    return ast.literal_eval(value)
    raise AssertionError(f"{name} not found")


def _load_function(name, namespace):
    module = ast.fix_missing_locations(
        ast.Module(body=[_top_level_node(name)], type_ignores=[]))
    exec(compile(module, str(ENTRY_PATH), "exec"), namespace)
    return namespace[name]


def _db():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.executescript(MIGRATION.read_text(encoding="utf-8"))
    return db


def _namespace(db):
    async def d1_run(_env, sql, *args):
        db.execute(sql, args)
        db.commit()

    async def d1_all(_env, sql, *args):
        return [dict(row) for row in db.execute(sql, args).fetchall()]

    async def d1_first(_env, sql, *args):
        row = db.execute(sql, args).fetchone()
        return dict(row) if row else None

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

    namespace = {
        "d1_run": d1_run,
        "d1_all": d1_all,
        "d1_first": d1_first,
        "ensure_schema": ensure_schema,
        "edge_cache_match": edge_cache_match,
        "edge_cache_put": edge_cache_put,
        "json_response": json_response,
        "urlparse": urlparse,
        "re": re,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "method_name": lambda request: request.method,
        "Date": SimpleNamespace(now=lambda: 1_720_000_000_000),
        "REFERRAL_PREVIEW_AGENTS": _entry_constant("REFERRAL_PREVIEW_AGENTS"),
    }
    for name in ("MAX_SITE_REFERRER_HOST", "SITE_REFERRER_SELF_HOSTS",
                 "SITE_REFERRER_LEADERBOARD_CACHE_KEY",
                 "SITE_REFERRER_LEADERBOARD_TTL",
                 "SITE_REFERRER_LEADERBOARD_LIMIT",
                 "SITE_REFERRER_RETAIN_HOSTS"):
        namespace[name] = _entry_constant(name)
    namespace["_SITE_REFERRER_HOST_RE"] = re.compile(
        _entry_constant("_SITE_REFERRER_HOST_RE"))
    for name in ("_site_referrer_host", "_is_page_navigation",
                 "_is_link_preview_agent", "record_site_referral",
                 "site_referrer_leaderboard", "prune_site_referrers"):
        _load_function(name, namespace)
    namespace["_cache_puts"] = cache_puts
    return namespace


def _request(referer, headers=None, method="GET"):
    merged = dict(NAV_HEADERS)
    merged.update(headers or {})
    if referer is not None:
        merged["referer"] = referer
    return SimpleNamespace(
        method=method,
        headers=SimpleNamespace(get=lambda key: merged.get(key)))


def _url(host="forkmesh.com"):
    return urlparse("https://%s/pricing" % host)


def _seed(db, host, visits, last_ts=0):
    db.execute(
        "INSERT INTO site_referrers (host, visits, first_ts, last_ts) "
        "VALUES (?,?,?,?)", (host, visits, last_ts, last_ts))
    db.commit()


def test_migration_is_host_counters_only_and_matches_lazy_schema():
    db = _db()
    columns = {
        row[1] for row in db.execute(
            "PRAGMA table_info(site_referrers)").fetchall()
    }
    assert columns == {"host", "visits", "first_ts", "last_ts"}
    assert "CREATE TABLE IF NOT EXISTS site_referrers" in SCHEMA
    # Privacy contract: a hostname and a count, nothing that is a visitor.
    for forbidden in ("ip", "url", "path", "user_agent", "session"):
        assert forbidden not in columns


def test_referrer_hosts_are_normalized_and_junk_is_dropped():
    host = _namespace(_db())["_site_referrer_host"]
    assert host("https://news.ycombinator.com/item?id=1", "forkmesh.com") == (
        "news.ycombinator.com")
    assert host("https://WWW.Reddit.com./r/git", "forkmesh.com") == "reddit.com"
    assert host("http://lobste.rs/s/abc", "forkmesh.com") == "lobste.rs"
    for junk in (
        "", None, "not a url", "/relative/path",
        "javascript:alert(1)", "data:text/html,x", "android-app://com.slack",
        "https://192.168.1.5/x", "https://[::1]/x", "https://localhost/x",
        "https://box.local/x", "https://runner.internal/x",
        "https://" + "a" * 120 + ".com/x",
    ):
        assert host(junk, "forkmesh.com") == "", junk


def test_own_deployment_never_counts_as_a_referring_website():
    host = _namespace(_db())["_site_referrer_host"]
    for own in ("forkmesh.com", "www.forkmesh.com"):
        assert host("https://forkmesh.com/pricing", own) == ""
        assert host("https://www.forkmesh.com/pricing", own) == ""
    # Sibling subdomains and the parent zone of a preview deployment.
    assert host("https://b.forkmesh.com/x", "forkmesh.com") == ""
    assert host("https://forkmesh.com/x", "preview.forkmesh.com") == ""
    assert host("https://forkmesh.internal/x", "forkmesh.com") == ""
    # A genuinely different site with a similar tail still counts.
    assert host("https://mesh.com/x", "forkmesh.com") == "mesh.com"


def test_only_served_page_navigations_are_counted():
    db = _db()
    ns = _namespace(db)
    run = lambda request, status=200, url=None: asyncio.run(  # noqa: E731
        ns["record_site_referral"](object(), request, url or _url(), status))

    run(_request("https://news.ycombinator.com/item?id=1"))
    assert db.execute(
        "SELECT visits FROM site_referrers WHERE host='news.ycombinator.com'"
    ).fetchone()["visits"] == 1

    ignored = [
        # Subresources and API calls: a page load is one visit, not thirty.
        (_request("https://news.ycombinator.com/x",
                  {"sec-fetch-dest": "image", "accept": "image/*"}), 200),
        (_request("https://news.ycombinator.com/x",
                  {"accept": "application/json", "sec-fetch-dest": "empty",
                   "sec-fetch-mode": "cors"}), 200),
        # Not a served page.
        (_request("https://news.ycombinator.com/x"), 404),
        (_request("https://news.ycombinator.com/x"), 302),
        # Not a browser visit.
        (_request("https://news.ycombinator.com/x", method="POST"), 200),
        (_request("https://news.ycombinator.com/x",
                  {"user-agent": "Mastodon/4.2.1"}), 200),
        # Internal navigation, and no referrer at all.
        (_request("https://forkmesh.com/pricing"), 200),
        (_request(None), 200),
    ]
    for request, status in ignored:
        run(request, status)
    assert db.execute(
        "SELECT SUM(visits) AS n FROM site_referrers").fetchone()["n"] == 1


def test_repeat_visits_bump_the_same_row_and_keep_first_seen():
    db = _db()
    ns = _namespace(db)
    for _ in range(3):
        asyncio.run(ns["record_site_referral"](
            object(), _request("https://lobste.rs/s/abc"), _url(), 200))
    row = db.execute("SELECT * FROM site_referrers").fetchone()
    assert (row["host"], row["visits"]) == ("lobste.rs", 3)
    assert row["first_ts"] == row["last_ts"] == 1_720_000_000_000
    assert db.execute("SELECT COUNT(*) FROM site_referrers").fetchone()[0] == 1


def test_counter_failures_never_break_the_page_load():
    ns = _namespace(_db())

    async def broken_d1_run(_env, sql, *args):
        raise RuntimeError("d1 down")

    ns["d1_run"] = broken_d1_run
    asyncio.run(ns["record_site_referral"](
        object(), _request("https://lobste.rs/s/abc"), _url(), 200))
    asyncio.run(ns["record_site_referral"](object(), object(), None, 200))


def test_leaderboard_ranks_by_visits_and_reports_totals():
    db = _db()
    ns = _namespace(db)
    _seed(db, "news.ycombinator.com", 42, 500)
    _seed(db, "lobste.rs", 9, 400)
    _seed(db, "mastodon.social", 9, 900)
    _seed(db, "quiet.example", 0, 100)
    resp = asyncio.run(ns["site_referrer_leaderboard"](object()))
    assert resp.data["ok"] is True
    assert [(r["host"], r["visits"]) for r in resp.data["board"]] == [
        ("news.ycombinator.com", 42),
        ("mastodon.social", 9),
        ("lobste.rs", 9),
    ]
    assert (resp.data["sites"], resp.data["visits"]) == (3, 60)
    assert resp.cache_seconds == ns["SITE_REFERRER_LEADERBOARD_TTL"]
    assert len(ns["_cache_puts"]) == 1


def test_prune_keeps_the_busiest_hosts_so_referrer_spam_stays_bounded():
    db = _db()
    ns = _namespace(db)
    ns["SITE_REFERRER_RETAIN_HOSTS"] = 3
    _seed(db, "news.ycombinator.com", 42)
    for i in range(20):
        _seed(db, "spam%02d.example" % i, 1, i)
    asyncio.run(ns["prune_site_referrers"](object()))
    hosts = [r["host"] for r in db.execute(
        "SELECT host FROM site_referrers ORDER BY visits DESC").fetchall()]
    assert len(hosts) == 3
    assert hosts[0] == "news.ycombinator.com"


def test_surfaces_expose_the_board_and_the_privacy_contract():
    assert '"/api/referrals/sites"' in REFERRALS_JS
    assert "site-referral-board" in REFERRALS_JS
    assert 'id="site-referral-board"' in REFERRALS_HTML
    assert "Top referring websites" in REFERRALS_HTML
    # The host is rendered as text, never as a link the board hands out.
    assert "createElement(\"a\")" not in REFERRALS_JS.split(
        "function renderSiteBoard")[1].split("function renderSiteSummary")[0]
    assert "Referring websites" in PRIVACY
    assert '"/api/referrals/sites"' in ENTRY
    assert "await record_site_referral(self.env, request, url, status)" in ENTRY


if __name__ == "__main__":
    test_migration_is_host_counters_only_and_matches_lazy_schema()
    test_referrer_hosts_are_normalized_and_junk_is_dropped()
    test_own_deployment_never_counts_as_a_referring_website()
    test_only_served_page_navigations_are_counted()
    test_repeat_visits_bump_the_same_row_and_keep_first_seen()
    test_counter_failures_never_break_the_page_load()
    test_leaderboard_ranks_by_visits_and_reports_totals()
    test_prune_keeps_the_busiest_hosts_so_referrer_spam_stays_bounded()
    test_surfaces_expose_the_board_and_the_privacy_contract()
