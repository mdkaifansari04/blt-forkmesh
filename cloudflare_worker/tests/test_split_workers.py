"""Three-Worker site split contracts (relay + forkmesh-www + forkmesh-world).

The relay (wrangler.toml) keeps the complete public/ tree and the Python
application; the two assets-only Workers own the marketing documents and the
World module graph via more-specific zone routes. These tests pin the parts
of that layout that would fail silently in production if drifted: route
capture boundaries, staging parity for _headers/_redirects, and the deploy
ordering that keeps the World freshness check honest.
"""

import re
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELAY = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
WWW = tomllib.loads((ROOT / "wrangler.www.toml").read_text(encoding="utf-8"))
WORLD = tomllib.loads(
    (ROOT / "wrangler.world.toml").read_text(encoding="utf-8")
)
DEPLOY = (ROOT / "deploy.sh").read_text(encoding="utf-8")
REDIRECTS = (ROOT / "public" / "_redirects").read_text(encoding="utf-8")

SPLIT_HOSTS = ("forkmesh.com", "www.forkmesh.com")


def _paths(config):
    """Route patterns as host-stripped paths, asserting host coverage."""
    routes = config["routes"]
    by_host = {host: set() for host in SPLIT_HOSTS}
    for route in routes:
        assert route["zone_name"] == "forkmesh.com"
        host, _, path = route["pattern"].partition("/")
        assert host in SPLIT_HOSTS, route["pattern"]
        by_host[host].add("/" + path)
    # Both public hostnames must carve identically or one host would fall
    # back to the relay while the other serves the split Worker.
    assert by_host["forkmesh.com"] == by_host["www.forkmesh.com"]
    return by_host["forkmesh.com"]


def test_split_workers_are_assets_only_and_mirror_relay_asset_semantics():
    for config in (WWW, WORLD):
        assert "main" not in config
        assert config["assets"]["not_found_handling"] == "404-page"
        # No application config may leak onto the assets-only Workers.
        for key in ("durable_objects", "d1_databases", "kv_namespaces",
                    "ai", "triggers", "vars"):
            assert key not in config, key
    # The world Worker mirrors the relay's assets semantics exactly; the www
    # Worker instead uses the platform's native clean-URL handling because
    # the relay's _redirects table cannot be shipped on it (100-rule dynamic
    # cap, and placeholder rules beat the exact feed safety net live).
    assert WORLD["assets"]["html_handling"] == "none"
    assert WWW["assets"]["html_handling"] == "auto-trailing-slash"
    assert WWW["name"] == "forkmesh-www"
    assert WWW["assets"]["directory"] == "./public_www"
    assert WORLD["name"] == "forkmesh-world"
    assert WORLD["assets"]["directory"] == "./public_world"
    # The relay template must stay route-free: cloudflare_bootstrap.py copies
    # it verbatim for self-hosters, and routes there would claim our zone.
    assert "routes" not in RELAY


def test_world_routes_own_exactly_the_world_prefix():
    assert _paths(WORLD) == {"/world", "/world/*"}


def test_www_routes_never_capture_application_or_feed_paths():
    paths = _paths(WWW)
    # Spot-check the marketing surface is present.
    for expected in ("/pricing", "/blog", "/blog/*", "/docs", "/docs/*",
                     "/about", "/status", "/features"):
        assert expected in paths, expected
    # Paths that MUST keep resolving on the relay. "/" records
    # site_referrers; the feeds are Worker-rendered; auth/chat/dashboard are
    # application pages; *.html spellings must reach the canonical 404s.
    forbidden = {
        "/", "/*", "/rss.xml", "/feed.xml", "/blog/rss.xml",
        "/blog/feed.xml", "/login", "/signup", "/forgot-password",
        "/reset-password", "/chat", "/world", "/world/*",
    }
    assert not (paths & forbidden)
    for path in paths:
        assert not path.endswith(".html"), path
        assert not path.startswith(("/api", "/ap/", "/dashboard", "/@",
                                    "/assets", "/favicon")), path
        # Wildcards are only allowed under a dedicated marketing directory
        # prefix; a bare-segment wildcard could swallow the two-segment
        # repository/git namespace.
        if path.endswith("*"):
            assert path in ("/blog/*", "/docs/*"), path


def test_blog_feed_bounce_keeps_the_worker_built_rss_reachable():
    # forkmesh-www owns /blog/*, so the verbatim-copied _redirects safety net
    # is what keeps feed readers working: /blog/rss.xml must bounce to
    # /rss.xml, and /rss.xml must NOT be routed to forkmesh-www.
    assert "/blog/rss.xml /rss.xml 308" in REDIRECTS
    assert "/blog/feed.xml /rss.xml 308" in REDIRECTS
    assert "/rss.xml" not in _paths(WWW)


def test_staging_copies_control_files_verbatim_plus_worker_marker(tmp_path):
    spec = ROOT / "tools" / "build_split_assets.py"
    subprocess.run(
        [sys.executable, str(spec)], cwd=ROOT, check=True,
        capture_output=True,
    )
    try:
        source_redirects = (ROOT / "public" / "_redirects").read_bytes()
        source_headers = (ROOT / "public" / "_headers").read_text(
            encoding="utf-8"
        )
        for staged, marker in (("public_www", "www"),
                               ("public_world", "world")):
            staged_dir = ROOT / staged
            headers = (staged_dir / "_headers").read_text(encoding="utf-8")
            assert headers.startswith(source_headers)
            assert f"x-forkmesh-worker: {marker}" in headers
            assert (staged_dir / "404.html").is_file()
        # The world copy of _redirects is verbatim; the www Worker gets the
        # tiny generated file instead — clean URLs come from
        # auto-trailing-slash, and only the feed bounces plus the /blogs
        # alias need rules. No placeholders allowed: live Cloudflare
        # resolved /blog/rss.xml through a placeholder rule (-> 404) in
        # preference to the exact safety-net rule above it.
        assert (ROOT / "public_world" / "_redirects").read_bytes() == (
            source_redirects
        )
        www_redirects = (ROOT / "public_www" / "_redirects").read_text(
            encoding="utf-8"
        )
        assert ":" not in www_redirects
        assert "/blog/rss.xml /rss.xml 308" in www_redirects
        assert "/blog/feed.xml /rss.xml 308" in www_redirects
        assert "/blog/rss.xml/ /rss.xml 308" in www_redirects
        assert "/blogs /blog 308" in www_redirects
        # docs.html must not be staged: with auto-trailing-slash it would
        # shadow docs/index.html for /docs (live /docs serves the tree
        # index). /docs.html stays a relay-owned canonical 404.
        assert not (ROOT / "public_www" / "docs.html").exists()
        # The www tree serves the marketing documents and their clean-URL
        # rewrite targets; the homepage must NOT be staged (it stays on the
        # relay for site_referrers).
        assert (ROOT / "public_www" / "pricing.html").is_file()
        assert (ROOT / "public_www" / "blog.html").is_file()
        assert (ROOT / "public_www" / "docs" / "index.html").is_file()
        assert (ROOT / "public_www" / "blog" / "introducing-forkmesh"
                / "index.html").is_file()
        assert not (ROOT / "public_www" / "index.html").exists()
        # The world tree is the module graph the freshness check hashes.
        world_shell = ROOT / "public_world" / "world" / "index.html"
        assert world_shell.read_bytes() == (
            ROOT / "public" / "world" / "index.html"
        ).read_bytes()
        assert (ROOT / "public_world" / "world" / "world-scene.js"
                ).read_bytes() == (
            ROOT / "public" / "world" / "world-scene.js"
        ).read_bytes()
    finally:
        shutil.rmtree(ROOT / "public_www", ignore_errors=True)
        shutil.rmtree(ROOT / "public_world", ignore_errors=True)


def test_deploy_ships_split_workers_between_relay_verify_and_asset_hashes():
    # Once the zone routes exist, /world/*.js is answered by forkmesh-world,
    # so the split deploy must land BEFORE verify_public_assets hash-compares
    # the live World modules — and the end-to-end route verification runs
    # after the marketing checks.
    arm = DEPLOY.split('case "${1:-deploy}" in', 1)[1]
    order = [
        arm.index('verify_deploy "$BUILD_REV"'),
        arm.index("deploy_split_site_workers"),
        arm.index("verify_public_assets"),
        arm.index("retire_legacy_marketing_worker"),
        arm.index("verify_marketing_routes"),
        arm.index("verify_split_site_workers"),
    ]
    assert order == sorted(order)
    # Both split configs deploy through the pinned wrangler version.
    assert "wrangler deploy --config \"$config\"" in DEPLOY
    assert 'for config in wrangler.www.toml wrangler.world.toml; do' in DEPLOY


def test_split_deploy_is_gated_to_the_canonical_origin():
    # Forks rewrite PUBLIC_BASE_URL via cloudflare_bootstrap.py; their
    # deploys must skip the split instead of trying to claim forkmesh.com
    # zone routes.
    assert 'PUBLIC_BASE_URL = "https://forkmesh.com"' in DEPLOY
    gated = re.findall(
        r"^(deploy_split_site_workers|verify_split_site_workers)\(\) \{\n"
        r"    if ! split_site_workers_enabled; then",
        DEPLOY,
        re.MULTILINE,
    )
    assert sorted(gated) == [
        "deploy_split_site_workers", "verify_split_site_workers",
    ]


def test_route_verification_proves_ownership_with_worker_markers():
    # The staged _headers append x-forkmesh-worker so verification can prove
    # which Worker answered; the relay carries no marker.
    for probe in ('_split_check "$base/pricing" www 200',
                  '_split_check "$base/world" world 200',
                  '_split_check "$base/world/world-scene.js" world 200',
                  '_split_check "$base/" "" 200',
                  '_split_check "$base/api/version" "" 200',
                  '_split_check "$base/login" "" 200',
                  '_split_check "$base/pricing.html" "" 404'):
        assert probe in DEPLOY, probe
