"""Marketing Worker ownership and compatibility contracts."""

import tomllib
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[2]
WWW = PROJECT / "www"
APP = PROJECT / "app"
WRANGLER = tomllib.loads((WWW / "wrangler.toml").read_text(encoding="utf-8"))
WORKER = (WWW / "worker.js").read_text(encoding="utf-8")
DEPLOY = (APP / "deploy.sh").read_text(encoding="utf-8")
REDIRECTS = (WWW / "public" / "_redirects").read_text(encoding="utf-8")


def test_www_worker_owns_only_official_marketing_domains():
    assert WRANGLER["name"] == "forkmesh-www"
    assert WRANGLER["assets"]["directory"] == "./dist"
    assert WRANGLER["assets"]["run_worker_first"] is True
    assert WRANGLER["vars"]["APP_ORIGIN"] == "https://app.forkmesh.com"
    assert WRANGLER["vars"]["WORLD_ORIGIN"] == "https://world.forkmesh.com"
    assert {route["pattern"] for route in WRANGLER["routes"]} == {
        "forkmesh.com",
        "www.forkmesh.com",
    }
    assert WRANGLER["services"] == [
        {"binding": "APP", "service": "forkmesh-relay"}
    ]


def test_clean_marketing_urls_are_backed_by_www_assets():
    for asset in ("index.html", "pricing.html", "blog.html", "docs.html"):
        assert (WWW / "public" / asset).is_file()
    assert not (WWW / "public" / "world").exists()
    assert not (WWW / "public" / "dashboard").exists()
    assert sorted((WWW / "public" / "blog").glob("*/index.html"))
    assert "/pricing /pricing.html 200" in REDIRECTS
    assert "/blog /blog.html 200" in REDIRECTS


def test_legacy_api_and_protocol_requests_are_proxied_to_app():
    assert 'if (path === "/api")' in WORKER
    assert 'if (path.startsWith("/api/"))' in WORKER
    assert "return proxyToApp(request, env);" in WORKER
    assert "env.APP.fetch(new Request(destination, request))" in WORKER
    assert "isGitProtocol(path) || isBackendProtocol(path)" in WORKER


def test_product_and_world_routes_leave_the_marketing_worker():
    assert 'path === "/world" || path.startsWith("/world/")' in WORKER
    assert "redirect(env.WORLD_ORIGIN" in WORKER
    assert 'path === "/dashboard" || path.startsWith("/dashboard/")' in WORKER
    assert "redirect(env.APP_ORIGIN" in WORKER


def test_www_static_directories_are_reserved_before_repo_shortcuts():
    assert 'const WWW_PREFIXES = ["/assets/", "/favicon/", "/blog/", "/docs/"]' in WORKER
    assert "!WWW_PREFIXES.some((prefix) => path.startsWith(prefix))" in WORKER


def test_deploy_coordinator_checks_www_as_an_independent_target():
    assert 'world|www)' in DEPLOY
    assert '"$0" app' in DEPLOY
    assert '"$0" world' in DEPLOY
    assert '"$0" www' in DEPLOY
    assert 'DEPLOY_VERIFY_WWW_URL:-https://forkmesh.com' in DEPLOY
    usage = next(line for line in DEPLOY.splitlines() if "Usage: $0 [" in line)
    assert "|world|www|" in usage
