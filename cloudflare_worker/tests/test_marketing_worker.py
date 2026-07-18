#!/usr/bin/env python3
"""The marketing Worker owns exactly forkmesh.com/ — the relay owns the rest.

The landing page is split into its own Worker (cloudflare_marketing_worker):
an exact-match Cloudflare route on forkmesh.com/ sends only the root URL there,
while every other path — including the landing page's own CSS/JS subresources —
keeps hitting this relay Worker. These tests pin the deployment contract
(route shape, single source of truth for index.html) and that the marketing
Worker mirrors the relay's / semantics: the forkmesh_session 302 with no-store,
and the no-cache + Vary: cookie landing serve (see test_home_session_redirect).
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


def test_marketing_worker_routes_only_the_exact_root():
    # Route patterns rank by specificity and beat Custom Domains, so a single
    # exact-match "forkmesh.com/" pattern is what makes / this Worker's and
    # /anything the relay's. Any wildcard here would steal relay traffic.
    routes = MARKETING_WRANGLER["routes"]
    assert len(routes) == 1
    assert routes[0]["pattern"] == "forkmesh.com/"
    assert routes[0]["zone_name"] == "forkmesh.com"
    assert MARKETING_WRANGLER["name"] == "forkmesh-marketing"
    # The relay must NOT declare competing routes in its committed config.
    assert "routes" not in RELAY_WRANGLER


def test_landing_document_has_a_single_source_of_truth():
    # index.html stays canonical in the relay's public/; the marketing build
    # copies it in, so the page can never drift between the two Workers.
    build = MARKETING_WRANGLER["build"]["command"]
    assert "../cloudflare_worker/public/index.html" in build
    assert (ROOT / "public" / "index.html").is_file()
    # public/ in the marketing Worker is generated output, never committed.
    gitignore = (MARKETING / ".gitignore").read_text(encoding="utf-8")
    assert "public/" in gitignore


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
