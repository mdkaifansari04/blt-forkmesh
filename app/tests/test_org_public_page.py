#!/usr/bin/env python3
"""Public organization pages.

Two URLs reach the same built document. ``/orgs/<name>`` is the canonical one
and works for every organization on the instance: it is two segments, so the
existing ``/*/*`` run_worker_first rule already routes it, and "orgs" is a
reserved prefix so it is never read as ``/<owner>/<repo>``. The bare ``/<org>``
form is a vanity alias that needs its own literal run_worker_first entry per
organization (the same pattern as /login and /signup), so it exists only for
names someone listed there.

Both resolve through _org_row and serve a built dashboard document that renders
client-side from GET /api/orgs/<name>. Unknown single-segment paths keep their
existing behavior (static 404 page, whose shipped script bounces to /@name).
"""

import ast
import asyncio
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
WRANGLER = (ROOT / "wrangler.toml").read_text(encoding="utf-8")


def _module(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "src" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


urls = _module("urls")
static_routes = _module("static_routes")


def _run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


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
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def test_org_page_route_shape():
    assert urls.ORG_PAGE_RE.match("/owasp-blt")
    assert urls.ORG_PAGE_RE.match("/owasp-blt/")
    assert not urls.ORG_PAGE_RE.match("/owasp-blt/repo")
    assert not urls.ORG_PAGE_RE.match("/@owasp-blt")
    assert not urls.ORG_PAGE_RE.match("/")


def test_wrangler_owns_the_owasp_blt_path():
    # The bare org URL must reach the Worker; without this entry the ASSETS
    # binding serves the 404 page (whose script bounces to /@owasp-blt).
    assert '"/owasp-blt"' in WRANGLER


def test_org_asset_is_built_and_blocked_from_direct_navigation():
    assert static_routes.DASHBOARD_ORG_ASSET == "dashboard/org/index.html"
    assert "/dashboard/org/index.html" in \
        static_routes.BLOCKED_STATIC_HTML_PATHS
    shell = _module("dashboard_shell")
    assert "org" in shell.PAGES
    assert shell.PAGES["org"]["asset"] == "dashboard/org/index.html"


class _Url:
    scheme = "https"
    netloc = "x.test"
    path = "/owasp-blt"


def _org_page_harness(org_exists):
    calls = {"asset": [], "cache_put": []}

    async def org_row(env, org):
        if org_exists and org == "owasp-blt":
            return "org-bi", {"name": "owasp-blt", "data": "enc"}
        return "", None

    async def noop(*args, **kwargs):
        return None

    async def edge_cache_match(key):
        return None

    async def edge_cache_put(key, page):
        calls["cache_put"].append(key)

    async def read_asset(asset_rel):
        calls["asset"].append(asset_rel)
        return ("<!doctype html><head><title>x</title></head>"
                "<body data-page=\"org\"></body>")

    class _Env:
        pass

    class _Response:
        def __init__(self, body, status=200, headers=None):
            self.body = body
            self.status = status
            self.headers = headers or {}

    async def not_found(url):
        return _Response("not-found", status=404)

    ns = _load(
        "_serve_org_page_response",
        extra_globals={
            "ensure_schema": noop,
            "_org_row": org_row,
            "edge_cache_match": edge_cache_match,
            "edge_cache_put": edge_cache_put,
            "Response": _Response,
            "quote": __import__("urllib.parse", fromlist=["quote"]).quote,
            "re": __import__("re"),
            "_html_escape": lambda value: str(value),
            "DASHBOARD_ORG_ASSET": "dashboard/org/index.html",
        },
    )
    result = _run(ns["_serve_org_page_response"](
        _Env, _Url, "owasp-blt", not_found, read_asset))
    return result, calls


def test_org_page_serves_the_built_document_with_canonical_head():
    result, calls = _org_page_harness(org_exists=True)
    assert result.status == 200
    assert calls["asset"] == ["dashboard/org/index.html"]
    # /orgs/<name> is the canonical form because it resolves for every org on
    # the instance; the bare /<org> alias only works for names that were given
    # their own literal wrangler run_worker_first entry.
    assert '<link rel="canonical" href="https://x.test/orgs/owasp-blt">' \
        in result.body
    assert "owasp-blt" in result.body
    assert calls["cache_put"] == ["https://x.test/orgs/owasp-blt"]
    assert result.headers.get("cache-control") == "public, max-age=300"


def test_unknown_org_falls_through_to_not_found():
    result, calls = _org_page_harness(org_exists=False)
    assert result.status == 404
    assert calls["asset"] == []


def test_org_page_route_never_claims_reserved_or_repo_paths():
    # The bare-org matcher is deliberately broad, so the guards around it do
    # the real work: reserved single-segment prefixes keep their own pages,
    # and anything with two or more segments belongs to the repo catch-all.
    for reserved in sorted(static_routes.RESERVED_ROUTE_PREFIXES):
        match = urls.ORG_PAGE_RE.match("/" + reserved)
        claimed = bool(match) and reserved not in \
            static_routes.RESERVED_ROUTE_PREFIXES
        assert not claimed, reserved
    # Two-segment paths stay with the repo route in both directions.
    assert not urls.ORG_PAGE_RE.match("/owasp-blt/BLT".lower())
    assert static_routes.looks_like_repo_route("/owasp-blt/blt")
    assert not static_routes.looks_like_repo_route("/owasp-blt")


def test_canonical_org_page_route_matches_every_org_name():
    # The bare /<org> URL only reaches the Worker for names listed one by one
    # in wrangler's run_worker_first, so a second organization on the same
    # deployment had no page at all. /orgs/<name> is two segments and is
    # therefore already covered by the existing "/*/*" rule — no per-org
    # entry, no deploy, for any org the instance holds.
    for name in ("owasp-blt", "owasp-labs", "a", "a-b-c", "org123"):
        match = urls.ORG_PUBLIC_PAGE_RE.match("/orgs/" + name)
        assert match, name
        assert match.group(1) == name
    # Trailing slash is the same page.
    assert urls.ORG_PUBLIC_PAGE_RE.match("/orgs/owasp-blt/")
    # It claims exactly one segment under /orgs and nothing else.
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/orgs")
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/orgs/owasp-blt/blt")
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/owasp-blt")
    # Names the org namespace itself rejects must not match either.
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/orgs/Owasp-BLT")
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/orgs/-leading")
    assert not urls.ORG_PUBLIC_PAGE_RE.match("/orgs/trailing-")


def test_orgs_prefix_is_reserved_so_it_cannot_be_read_as_a_repo():
    # Without the reservation, /orgs/<name> is a plain two-segment path and
    # looks_like_repo_route would hand it to the repo document as owner
    # "orgs" / repo "<name>".
    assert "orgs" in static_routes.RESERVED_ROUTE_PREFIXES
    assert not static_routes.looks_like_repo_route("/orgs/owasp-blt")
    # Real repo routes are untouched by the reservation.
    assert static_routes.looks_like_repo_route("/owasp-blt/blt")
