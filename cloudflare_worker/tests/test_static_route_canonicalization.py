#!/usr/bin/env python3
"""Canonical route contracts for public static pages."""

import re
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
REDIRECTS = PUBLIC / "_redirects"
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import static_routes  # noqa: E402

CANONICAL_PAGE_ROUTES = {
    "/": "/index.html",
    "/dashboard": "/dashboard/index.html",
    "/dashboard/repos": "/dashboard/repos/index.html",
    "/dashboard/network": "/dashboard/network/index.html",
    "/dashboard/chat": "/dashboard/chat/index.html",
    "/dashboard/settings": "/dashboard/settings/index.html",
    "/dashboard/profile": "/dashboard/profile/index.html",
    "/dashboard/profile/repositories": "/dashboard/profile/repositories/index.html",
    "/blog": "/blog.html",
    "/login": "/login.html",
    "/signup": "/signup.html",
    "/forgot-password": "/forgot-password.html",
    "/reset-password": "/reset-password.html",
    "/mirror-payouts": "/mirror-payouts.html",
    "/outreach": "/outreach.html",
    "/security-report": "/security-report.html",
    "/docs": "/docs/index.html",
    "/network": "/network.html",
    "/chat": "/chat.html",
    "/desktop": "/desktop.html",
    "/about": "/about.html",
    "/features": "/features.html",
    "/pricing": "/pricing.html",
    "/press": "/press.html",
    "/careers": "/careers.html",
    "/changelog": "/changelog.html",
    "/privacy": "/privacy.html",
    "/terms": "/terms.html",
    "/status": "/status.html",
}

BLOCKED_HTML_ALIASES = {
    "/index.html",
    "/dashboard.html",
    "/blog.html",
    "/login.html",
    "/signup.html",
    "/forgot-password.html",
    "/reset-password.html",
    "/mirror-payouts.html",
    "/outreach.html",
    "/security-report.html",
    "/docs.html",
    "/network.html",
    "/chat.html",
    "/desktop.html",
    "/about.html",
    "/features.html",
    "/pricing.html",
    "/press.html",
    "/careers.html",
    "/changelog.html",
    "/privacy.html",
    "/terms.html",
    "/status.html",
}

BLOG_POST_REDIRECT_RULES = {
    ("/blog/:slug", "/blog/:slug/", "308"),
    ("/blog/:slug/", "/blog/:slug/index.html", "200"),
}

DOCS_PAGE_REDIRECT_RULES = {
    ("/docs/:slug", "/docs/:slug/", "308"),
    ("/docs/:slug/", "/docs/:slug/index.html", "200"),
}

NON_ROUTED_HTML_ASSETS = {
    # Served by Cloudflare only for misses, not a navigable product route.
    "404.html",
    # Authored dashboard source; the generated dashboard/index.html asset is the
    # public route target.
    "dashboard/shell.html",
    # The repo-detail document: fetched by the Worker for every /owner/repo
    # route, never a navigable asset path of its own.
    "dashboard/repo.html",
    # Duplicate static copy kept for source parity; its public route is
    # canonicalized to the index-backed route above.
    "docs.html",
} | {
    # Blog posts live under /blog/<slug>/ and are served as static
    # directory indexes, while /blog itself is the canonical listing route.
    path.relative_to(PUBLIC).as_posix()
    for path in (PUBLIC / "blog").glob("*/index.html")
} | {
    # Docs pages live under /docs/<slug>/ and are served as static
    # directory indexes, while /docs itself is the canonical listing route.
    path.relative_to(PUBLIC).as_posix()
    for path in (PUBLIC / "docs").glob("*/index.html")
}


def _redirect_rules():
    rules = []
    for raw_line in REDIRECTS.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        source, target, status = line.split()[:3]
        rules.append((source, target, status))
    return rules


def test_public_redirects_have_one_canonical_route_per_public_html_page():
    rules = _redirect_rules()
    assert len({source for source, _, _ in rules}) == len(rules)
    assert BLOG_POST_REDIRECT_RULES <= set(rules)
    assert DOCS_PAGE_REDIRECT_RULES <= set(rules)

    html_200_rules = {
        source: target
        for source, target, status in rules
        if (
            status == "200"
            and target.endswith(".html")
            and (source, target, status) not in BLOG_POST_REDIRECT_RULES
            and (source, target, status) not in DOCS_PAGE_REDIRECT_RULES
        )
    }

    assert html_200_rules == CANONICAL_PAGE_ROUTES


def test_every_public_html_file_is_accounted_for_by_routes_or_explicit_exclusions():
    routed_assets = {target.lstrip("/") for target in CANONICAL_PAGE_ROUTES.values()}
    html_assets = {
        path.relative_to(PUBLIC).as_posix()
        for path in PUBLIC.rglob("*.html")
        if "/dashboard/partials/" not in path.as_posix()
    }

    assert html_assets == routed_assets | NON_ROUTED_HTML_ASSETS


def test_html_asset_aliases_are_not_public_redirects():
    for source, target, status in _redirect_rules():
        if source.endswith(".html"):
            raise AssertionError(
                "%s should be a 404-only implementation file, not %s %s"
                % (source, target, status)
            )

    assert static_routes.BLOCKED_STATIC_HTML_PATHS >= BLOCKED_HTML_ALIASES


def test_root_favicon_alias_routes_to_brand_icon():
    assert ("/favicon.ico", "/favicon/favicon.ico", "200") in _redirect_rules()
    assert (PUBLIC / "favicon" / "favicon.ico").is_file()


def test_internal_links_and_redirects_do_not_point_at_html_routes():
    href_pattern = re.compile(r'\b(?:href|action)="(/[^"#?]+\.html)(?:[#?][^"]*)?"')
    js_route_pattern = re.compile(
        r'location\.(?:href|replace)\s*(?:=|\()\s*"(/[^"#?]+\.html)(?:[#?][^"]*)?"'
    )
    violations = []

    migrated_pages = {
        "404.html",
        "about.html",
        "blog.html",
        "careers.html",
        "changelog.html",
        "desktop.html",
        "docs.html",
        "docs/index.html",
        "features.html",
        "forgot-password.html",
        "login.html",
        "network.html",
        "pricing.html",
        "press.html",
        "privacy.html",
        "reset-password.html",
        "status.html",
        "terms.html",
    }

    for path in sorted(PUBLIC.rglob("*.html")):
        if "/dashboard/partials/" in path.as_posix():
            continue
        if path.relative_to(PUBLIC).as_posix() not in migrated_pages:
            continue
        for match in href_pattern.finditer(path.read_text(encoding="utf-8")):
            violations.append(f"{path.relative_to(ROOT)} -> {match.group(1)}")

    for path in sorted(PUBLIC.rglob("*.js")):
        for match in js_route_pattern.finditer(path.read_text(encoding="utf-8")):
            violations.append(f"{path.relative_to(ROOT)} -> {match.group(1)}")

    assert violations == []


def test_worker_does_not_own_static_page_alias_routes():
    run_worker_first = WRANGLER["assets"]["run_worker_first"]

    for route in ("/blog", "/blogs", "/blog.html", "/dashboard.js", "/docs", "/docs.html", "/network", "/network.html"):
        if route.endswith(".html"):
            assert route in run_worker_first
        else:
            assert route not in run_worker_first

    # / and /dashboard ARE worker-owned: / must vary on the login presence
    # cookie (302 vs homepage) and /dashboard must see the query string to 308
    # legacy ?section= URLs — _redirects cannot match either.
    assert "/" in run_worker_first
    assert "/dashboard" in run_worker_first

    assert 'if url.path == "/blog.html":' not in ENTRY_TEXT
    assert 'headers={"location": "/docs/"}' not in ENTRY_TEXT
    assert 'headers={"location": "/network/"}' not in ENTRY_TEXT


def test_repo_shortcuts_are_worker_owned_without_hijacking_static_assets():
    run_worker_first = WRANGLER["assets"]["run_worker_first"]

    assert "/*/*" in run_worker_first
    for exception in ("/assets/*", "/favicon/*", "/blog/*", "/docs/*"):
        assert "!" + exception in run_worker_first

    assert "!/network/*" not in run_worker_first

    assert static_routes.looks_like_repo_route("/kaif/forkmesh")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/issues")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/insights")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/tree/src/main.py")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/blob/README.md")

    for path in (
        "/assets/logo.png",
        "/favicon/site.webmanifest",
        "/docs/guide",
        "/blog/introducing-forkmesh/index.html",
        "/dashboard/partials/header.html",
    ):
        assert not static_routes.looks_like_repo_route(path)


def test_public_profile_routes_are_worker_owned():
    # /@name is single-segment, so it does not match /*/*; without explicit
    # entries Static Assets shadows it, encodes @ -> %40, and 404s before the
    # worker's public_profile_handler runs. Both literal and pre-encoded forms
    # must be routed to the worker.
    run_worker_first = WRANGLER["assets"]["run_worker_first"]

    assert "/@*" in run_worker_first
    assert "/%40*" in run_worker_first


if __name__ == "__main__":
    test_public_redirects_have_one_canonical_route_per_public_html_page()
    test_every_public_html_file_is_accounted_for_by_routes_or_explicit_exclusions()
    test_html_asset_aliases_are_not_public_redirects()
    test_root_favicon_alias_routes_to_brand_icon()
    test_internal_links_and_redirects_do_not_point_at_html_routes()
    test_worker_does_not_own_static_page_alias_routes()
    test_repo_shortcuts_are_worker_owned_without_hijacking_static_assets()
    test_public_profile_routes_are_worker_owned()
