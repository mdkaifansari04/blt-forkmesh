#!/usr/bin/env python3

import re
import sys
import tomllib
from pathlib import Path


APP = Path(__file__).resolve().parents[1]
REPOSITORY = APP.parent
APP_PUBLIC = APP / "public"
WORLD_PUBLIC = REPOSITORY / "world" / "public"
APP_WRANGLER = tomllib.loads((APP / "wrangler.toml").read_text(encoding="utf-8"))
SRC = APP / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import static_routes  # noqa: E402


def test_source_html_is_physically_partitioned():
    assert not (APP_PUBLIC / "blog").exists()
    assert not (APP_PUBLIC / "docs").exists()

    assert (APP_PUBLIC / "dashboard/index.html").is_file()
    assert (APP_PUBLIC / "login.html").is_file()
    assert not (APP_PUBLIC / "world").exists()


def test_internal_www_links_do_not_publish_html_implementation_paths():
    pattern = re.compile(r'\b(?:href|action)="(/[^"#?]+\.html)(?:[#?][^"]*)?"')
    violations = []
    # 404.html is the only page of the retired marketing site the app ships.
    for path in (APP_PUBLIC / "404.html",):
        for match in pattern.finditer(path.read_text(encoding="utf-8")):
            violations.append(f"{path.relative_to(APP_PUBLIC)} -> {match.group(1)}")
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
