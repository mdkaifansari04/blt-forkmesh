#!/usr/bin/env python3

import re
import sys
import tomllib
from pathlib import Path


APP = Path(__file__).resolve().parents[1]
REPOSITORY = APP.parent
WWW_PUBLIC = REPOSITORY / "www" / "public"
APP_PUBLIC = APP / "public"
WORLD_PUBLIC = REPOSITORY / "world" / "public"
REDIRECTS = WWW_PUBLIC / "_redirects"
APP_WRANGLER = tomllib.loads((APP / "wrangler.toml").read_text(encoding="utf-8"))
SRC = APP / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import static_routes  # noqa: E402


WWW_PAGE_ROUTES = {
    "/homev2": "/homev2.html",
    "/new-home": "/new-home.html",
    "/blog": "/blog.html",
    "/docs": "/docs/index.html",
    "/outreach": "/outreach.html",
    "/security-report": "/security-report.html",
    "/desktop": "/desktop.html",
    "/about": "/about.html",
    "/features": "/features.html",
    "/pricing": "/pricing.html",
    "/press": "/press.html",
    "/careers": "/careers.html",
    "/changelog": "/changelog.html",
    "/privacy": "/privacy.html",
    "/terms": "/terms.html",
    "/favicon.ico": "/favicon/favicon.ico",
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


def test_www_redirects_only_rewrite_www_owned_pages():
    rules = _redirect_rules()
    assert len({source for source, _, _ in rules}) == len(rules)
    assert all(source != "/" for source, _, _ in rules)
    assert (WWW_PUBLIC / "index.html").is_file()
    rewrites = {
        source: target
        for source, target, status in rules
        if status == "200" and ":slug" not in source
    }
    assert rewrites == WWW_PAGE_ROUTES
    for target in rewrites.values():
        assert (WWW_PUBLIC / target.lstrip("/")).is_file()

    for app_route in (
        "/dashboard",
        "/login",
        "/signup",
        "/status",
    ):
        assert app_route not in rewrites


def test_blog_and_docs_directory_routes_are_canonical():
    rules = set(_redirect_rules())
    assert ("/blog/:slug", "/blog/:slug/", "308") in rules
    assert ("/blog/:slug/", "/blog/:slug/index.html", "200") in rules
    assert ("/docs/:slug", "/docs/:slug/", "308") in rules
    assert ("/docs/:slug/", "/docs/:slug/index.html", "200") in rules
    assert ("/blog/rss.xml", "/rss.xml", "308") in rules


def test_source_html_is_physically_partitioned():
    assert (WWW_PUBLIC / "blog.html").is_file()
    assert (WWW_PUBLIC / "docs/index.html").is_file()
    assert not (APP_PUBLIC / "blog").exists()
    assert not (APP_PUBLIC / "docs").exists()

    assert (APP_PUBLIC / "dashboard/index.html").is_file()
    assert (APP_PUBLIC / "login.html").is_file()
    assert not (WWW_PUBLIC / "dashboard/index.html").exists()
    assert not (WWW_PUBLIC / "login.html").exists()
    assert not (APP_PUBLIC / "world").exists()


def test_internal_www_links_do_not_publish_html_implementation_paths():
    pattern = re.compile(r'\b(?:href|action)="(/[^"#?]+\.html)(?:[#?][^"]*)?"')
    violations = []
    for path in sorted(WWW_PUBLIC.rglob("*.html")):
        if "/dashboard/partials/" in path.as_posix():
            continue
        for match in pattern.finditer(path.read_text(encoding="utf-8")):
            violations.append(f"{path.relative_to(WWW_PUBLIC)} -> {match.group(1)}")
    assert violations == []


def test_app_dynamic_routes_run_before_static_assets():
    run_worker_first = APP_WRANGLER["assets"]["run_worker_first"]
    assert "/" in run_worker_first
    assert "/api" in run_worker_first
    assert "/api/*" in run_worker_first
    assert "/developers" in run_worker_first
    assert "/*/*" in run_worker_first
    assert "/@*" in run_worker_first
    assert "/%40*" in run_worker_first

    for exception in (
        "/assets/*",
        "/favicon/*",
    ):
        assert "!" + exception in run_worker_first
    for worker_owned in (
        "/blog/*",
        "/docs/*",
        "/dashboard/*",
        "/notes",
        "/notes/*",
    ):
        assert worker_owned in run_worker_first
    for removed in ("/world/*", "/desktop", "/chat", "/network"):
        assert removed not in run_worker_first
    for asset_page in (
        "/dashboard/repos",
        "/dashboard/tasks",
        "/dashboard/notes",
        "/dashboard/settings",
        "/dashboard/profile",
        "/dashboard/profile/repositories",
    ):
        # Page HTML is Worker-owned; only js/partials/css stay on ASSETS.
        assert "!" + asset_page not in run_worker_first
        assert "!" + asset_page + "/" not in run_worker_first
    assert "!/dashboard/js/*" in run_worker_first
    assert "!/dashboard/partials/*" in run_worker_first
    assert APP_WRANGLER["assets"]["html_handling"] == "drop-trailing-slash"


def test_repo_shortcut_detection_does_not_hijack_assets():
    assert static_routes.looks_like_repo_route("/kaif/forkmesh")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/issues")
    assert static_routes.looks_like_repo_route("/kaif/forkmesh/tree/src/main.py")
    for path in (
        "/assets/logo.png",
        "/favicon/site.webmanifest",
        "/docs/guide",
        "/blog/introducing-forkmesh/index.html",
        "/world/world.js",
        "/dashboard/partials/header.html",
    ):
        assert not static_routes.looks_like_repo_route(path)
