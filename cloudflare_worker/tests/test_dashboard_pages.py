#!/usr/bin/env python3
"""Per-page dashboard contract: one built document per page, real-link nav.

The dashboard is true separate pages (dashboard_shell.PAGES): each page URL
serves its own prebuilt document containing the shared chrome plus exactly one
view, the sidebar marks the active page at build time, and legacy
/dashboard?section= URLs 308 to the per-page paths in the Worker.
"""

import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import dashboard_shell
import static_routes

from _dashboard_shell import assembled_dashboard_page
from _dashboard_bundle import assembled_dashboard_js

WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
REDIRECT_LINES = (PUBLIC / "_redirects").read_text(encoding="utf-8").splitlines()
ENTRY_TEXT = (SRC / "entry.py").read_text(encoding="utf-8")


def test_every_page_document_is_built_and_deterministic():
    for page_id, meta in dashboard_shell.PAGES.items():
        built = (PUBLIC / meta["asset"]).read_text(encoding="utf-8")
        assert built == assembled_dashboard_page(page_id), (
            "%s is stale — run tools/build_dashboard_assets.py" % meta["asset"])


def test_client_bundles_carry_content_hash_cache_busters():




    versions = dashboard_shell.asset_versions(
        lambda rel: (PUBLIC / rel).read_text(encoding="utf-8"),
        {"dashboard.js": assembled_dashboard_js()})
    assert versions["dashboard.js"] != versions["dashboard-chat.js"]
    for meta in dashboard_shell.PAGES.values():
        html = (PUBLIC / meta["asset"]).read_text(encoding="utf-8")
        assert "?v=public-profiles" not in html, meta["asset"]
        for name in ("dashboard.js", "dashboard-chat.js"):
            m = re.search(r'src="/%s\?v=([0-9a-f]{6,})"' % re.escape(name), html)
            assert m is not None, (meta["asset"], name)
            assert m.group(1) == versions[name], (meta["asset"], name)


def test_each_page_document_has_exactly_one_view_and_its_page_marker():
    for page_id, meta in dashboard_shell.PAGES.items():
        html = assembled_dashboard_page(page_id)
        assert html.count("<section data-view=") == 1, page_id


        assert ('<section data-view="%s"' % meta["section"]) in html, page_id
        assert ('data-page="%s"' % page_id) in html, page_id
        assert ('data-dashboard-section="%s"' % meta["section"]) in html, page_id

        assert 'class="view active' in html, page_id

        for marker in ("data-app-header", "data-sidebar-main-menu",
                       "data-profile-sidebar-template"):
            assert marker in html, (page_id, marker)


def test_sidebar_marks_the_active_page_at_build_time():
    for page_id, meta in dashboard_shell.PAGES.items():
        html = assembled_dashboard_page(page_id)
        sidebar = html[html.index("data-sidebar-main-menu"):html.index("data-sidebar-top-repositories")]
        active = sidebar.count('aria-current="page"')
        if meta["nav"]:
            assert active == 1, page_id
            assert ('data-nav="%s" data-nav-link aria-current="page"' % meta["nav"]) in sidebar, page_id
        else:

            assert active == 0, page_id


def test_sidebar_nav_is_real_links_not_section_buttons():
    html = assembled_dashboard_page("home")
    for path in ("/dashboard", "/dashboard/profile", "/dashboard/repos",
                 "/dashboard/network", "/dashboard/chat"):
        assert ('href="%s"' % path) in html, path

    for page_id in dashboard_shell.PAGES:
        assert "data-section=" not in assembled_dashboard_page(page_id), page_id


def test_routing_config_covers_every_page():
    run_worker_first = WRANGLER["assets"]["run_worker_first"]
    assert "/dashboard" in run_worker_first
    assert "/dashboard/*" in run_worker_first
    for route, asset in static_routes.DASHBOARD_PAGE_ASSETS.items():
        assert (PUBLIC / asset).is_file(), asset
        if route == "/dashboard":
            continue


        assert ("!" + route) in run_worker_first, route
        assert ("%s /%s 200" % (route, asset)) in REDIRECT_LINES, route

    for asset in static_routes.DASHBOARD_PAGE_ASSETS.values():
        assert ("/" + asset) in static_routes.BLOCKED_STATIC_HTML_PATHS, asset
    assert ("/" + static_routes.DASHBOARD_REPO_ASSET) in static_routes.BLOCKED_STATIC_HTML_PATHS


def test_page_routes_match_the_pages_table():
    routes = {meta["route"]: meta["asset"]
              for meta in dashboard_shell.PAGES.values() if meta["route"]}
    assert routes == static_routes.DASHBOARD_PAGE_ASSETS


def test_worker_serves_repo_document_and_redirects_legacy_urls():
    assert 'DASHBOARD_REPO_ASSET' in ENTRY_TEXT
    assert "dashboard_section_redirect(url.path, url.query)" in ENTRY_TEXT
    assert "_serve_dashboard_shell" not in ENTRY_TEXT


    legacy = set(static_routes.DASHBOARD_SECTION_PATHS)
    assert legacy == {"home", "repos", "network", "chat", "profile",
                      "profile-overview", "profile-repositories"}
    for target in static_routes.DASHBOARD_SECTION_PATHS.values():
        assert target in static_routes.DASHBOARD_PAGE_ASSETS, target


def test_bundle_has_no_legacy_section_router():
    js = assembled_dashboard_js()
    for gone in ("function setSection(", "function showSection(",
                 "SECTION_ROUTES", "function sectionUrl(",
                 "`/dashboard?section="):
        assert gone not in js, gone

    for kept in ("PAGE_INITS", "function initSharedChrome(",
                 "function initPageHistory(", "settingsSectionFromPath",
                 "function openRepoPage("):
        assert kept in js, kept


def test_dashboard_html_duplicate_is_gone():
    assert not (PUBLIC / "dashboard.html").exists()
    assert "/dashboard.html" not in WRANGLER["assets"]["run_worker_first"]


def test_no_built_asset_still_links_legacy_section_urls():
    for path in sorted(PUBLIC.rglob("*.html")) + sorted(PUBLIC.glob("*.js")):
        text = path.read_text(encoding="utf-8")
        if path.name.endswith(".js") and "SECTION_PATHS" in text:
            continue
        assert 'href="/dashboard?section=' not in text, path
