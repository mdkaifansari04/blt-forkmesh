#!/usr/bin/env python3
"""The marketing Worker owns the marketing pages — the relay owns the rest.

The public marketing pages are split into their own Worker
(cloudflare_marketing_worker): Cloudflare routes on forkmesh.com/, /pricing,
/blog, and /blog/* send exactly those paths there, while every other path —
including the pages' own CSS/JS/image subresources — keeps hitting this relay
Worker. These tests pin the deployment contract (route shape, single source of
truth for each document) and that the marketing Worker mirrors the relay's
semantics: the forkmesh_session 302 with no-store and the no-cache + Vary:
cookie landing serve (see test_home_session_redirect), plus the /pricing and
/blog clean-URL mapping.
"""

import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKETING = ROOT.parent / "cloudflare_marketing_worker"

RELAY_WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
MARKETING_WRANGLER = tomllib.loads(
    (MARKETING / "wrangler.toml").read_text(encoding="utf-8"))
MARKETING_ENTRY = (MARKETING / "src" / "entry.js").read_text(encoding="utf-8")
MARKETING_DEPLOY = (MARKETING / "deploy.sh").read_text(encoding="utf-8")


def test_marketing_worker_routes_only_the_marketing_pages():
    # Route patterns rank by specificity and beat Custom Domains, so these
    # patterns are what make /, /pricing, and /blog(/*) this Worker's while
    # /anything-else stays the relay's. /blog/* is the only wildcard and it
    # matches only the post directory indexes (blog images live under
    # /assets/blog/*, which the relay still owns).
    patterns = {r["pattern"] for r in MARKETING_WRANGLER["routes"]}
    assert patterns == {
        "forkmesh.com/",
        "forkmesh.com/pricing",
        "forkmesh.com/blog",
        "forkmesh.com/blog/*",
    }
    for route in MARKETING_WRANGLER["routes"]:
        assert route["zone_name"] == "forkmesh.com"
    assert MARKETING_WRANGLER["name"] == "forkmesh-marketing"
    # The relay must NOT declare competing routes in its committed config.
    assert "routes" not in RELAY_WRANGLER


def test_marketing_documents_have_a_single_source_of_truth():
    # The documents stay canonical in the relay's public/; the marketing build
    # copies them in, so no page can drift between the two Workers.
    build = MARKETING_WRANGLER["build"]["command"]
    for src in (
        "../cloudflare_worker/public/index.html",
        "../cloudflare_worker/public/pricing.html",
        "../cloudflare_worker/public/blog.html",
        "../cloudflare_worker/public/blog/.",
    ):
        assert src in build
    for canonical in ("index.html", "pricing.html", "blog.html"):
        assert (ROOT / "public" / canonical).is_file()
    assert (ROOT / "public" / "blog").is_dir()
    # public/ in the marketing Worker is generated output, never committed.
    gitignore = (MARKETING / ".gitignore").read_text(encoding="utf-8")
    assert "public/" in gitignore


def test_marketing_worker_maps_pricing_and_blog_clean_urls():
    # Clean-URL mapping mirrors the relay's _redirects: /pricing -> pricing.html,
    # /blog -> blog.html, and each post index /blog/<slug>/ -> the explicit
    # index.html (html_handling is "none"). Slugs are one [a-z0-9-] segment,
    # which also blocks path traversal.
    assert 'pathname === "/pricing"' in MARKETING_ENTRY
    assert '"/pricing.html"' in MARKETING_ENTRY
    assert 'pathname === "/blog"' in MARKETING_ENTRY
    assert '"/blog.html"' in MARKETING_ENTRY
    assert "/blog/${slug}/index.html" in MARKETING_ENTRY
    assert "/^[a-z0-9-]+$/" in MARKETING_ENTRY


def test_marketing_worker_sees_requests_before_the_asset_store():
    # / varies on the forkmesh_session cookie, which a directly-served static
    # asset could never do.
    assert MARKETING_WRANGLER["assets"]["run_worker_first"] is True
    assert MARKETING_WRANGLER["assets"]["html_handling"] == "none"


def test_marketing_worker_mirrors_the_relay_root_semantics():
    # Cookie check: strict equality with "1", 302 to /dashboard, never cached.
    assert 'cookieValue(request, "forkmesh_session") === "1"' in MARKETING_ENTRY
    assert '"location": "/dashboard"' in MARKETING_ENTRY
    assert "status: 302" in MARKETING_ENTRY
    assert "no-store, max-age=0, must-revalidate" in MARKETING_ENTRY
    # Logged-out serve: revalidate every time and vary on the cookie.
    assert '"cache-control": "no-cache"' in MARKETING_ENTRY
    assert '"vary": "cookie"' in MARKETING_ENTRY
    assert '"/index.html"' in MARKETING_ENTRY


def test_marketing_responses_are_attributable_and_verified():
    # Every landing response stamps x-forkmesh-worker: marketing, and deploy.sh
    # fails loudly if the live root never reports it (route didn't take effect).
    assert '"x-forkmesh-worker": "marketing"' in MARKETING_ENTRY
    assert "x-forkmesh-worker:[[:space:]]*marketing" in MARKETING_DEPLOY


def test_relay_keeps_its_homepage_fallback():
    # The relay still owns / on workers.dev previews and in local dev, where no
    # route splitting exists — its handler and worker-first entry must survive
    # the split (test_home_session_redirect pins the handler's semantics).
    assert "/" in RELAY_WRANGLER["assets"]["run_worker_first"]
