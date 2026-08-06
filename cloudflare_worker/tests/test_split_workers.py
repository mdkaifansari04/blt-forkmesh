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
API = tomllib.loads(
    (ROOT / "wrangler.api.toml").read_text(encoding="utf-8")
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


def test_api_worker_is_the_same_application_behind_api_routes():
    # forkmesh-api is a second deployment of the relay's Python application,
    # differing ONLY in ownership: /api/* zone routes, a WORKER_ROLE marker,
    # no cron (the relay's schedule must not double-run), and no Durable
    # Object migrations (the classes live in the relay script and are
    # reached cross-script, so both Workers share the same live rooms).
    assert API["name"] == "forkmesh-api"
    assert API["main"] == RELAY["main"] == "src/entry.py"
    assert API["compatibility_flags"] == RELAY["compatibility_flags"]
    assert API["compatibility_date"] == RELAY["compatibility_date"]
    assert _paths(API) == {"/api/*"}
    assert API["assets"]["directory"] == "./public_api"
    assert API["assets"]["binding"] == "ASSETS"
    assert API["assets"]["run_worker_first"] == ["/api/*"]
    assert "triggers" not in API
    assert "migrations" not in API
    assert API["build"]["command"] == (
        "python3 tools/build_split_assets.py api"
    )
    for binding in API["durable_objects"]["bindings"]:
        assert binding["script_name"] == "forkmesh-relay", binding
    assert {
        (b["name"], b["class_name"])
        for b in API["durable_objects"]["bindings"]
    } == {
        (b["name"], b["class_name"])
        for b in RELAY["durable_objects"]["bindings"]
    }
    assert API["d1_databases"] == RELAY["d1_databases"]
    assert API["kv_namespaces"] == RELAY["kv_namespaces"]
    assert API["ai"] == RELAY["ai"]
    assert API["observability"] == RELAY["observability"]
    # Value-identical vars keep the two deployments of the same code from
    # diverging in configuration; WORKER_ROLE is the one deliberate marker.
    api_vars = dict(API["vars"])
    assert api_vars.pop("WORKER_ROLE") == "api"
    assert api_vars == RELAY["vars"]


def test_version_and_health_endpoints_identify_their_worker():
    entry = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
    # /api/version rides the forkmesh-api routes; /health stays relay-routed
    # so each Worker exposes its own build stamp and role for verification.
    assert entry.count(
        '"worker": str(\n'
        '                        getattr(self.env, "WORKER_ROLE", "") or "relay"),'
    ) == 2


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
        # The api Worker carries ONLY what its Python reads through ASSETS
        # while answering /api/*: the blog board's sources, the homepage
        # origin probe, the install/repo-shell readers, and its 404 page.
        api_files = sorted(
            str(p.relative_to(ROOT / "public_api"))
            for p in (ROOT / "public_api").rglob("*")
            if p.is_file() and not str(p).count("/blog/")
        )
        assert api_files == [
            "404.html", "blog.html", "dashboard/repo.html",
            "index.html", "install.sh",
        ]
        assert (ROOT / "public_api" / "blog").is_dir()
        # The slimmed relay drops exactly what the split Workers own —
        # /world, /docs, and the www marketing documents — while keeping
        # everything its own routes and ASSETS reads still serve.
        relay = ROOT / "public_relay"
        assert not (relay / "world").exists()
        assert not (relay / "docs").exists()
        assert not (relay / "pricing.html").exists()
        assert not (relay / "docs.html").exists()
        assert not (relay / "status.html").exists()
        for kept in ("index.html", "blog.html", "404.html", "login.html",
                     "signup.html", "chat.html", "install.sh", "_redirects",
                     "_headers"):
            assert (relay / kept).is_file(), kept
        assert (relay / "blog").is_dir()
        assert (relay / "dashboard" / "repo.html").is_file()
        assert (relay / "assets").is_dir()
        assert (relay / "notes" / "view.html").is_file()
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
        for staged in ("public_www", "public_world", "public_api",
                       "public_relay"):
            shutil.rmtree(ROOT / staged, ignore_errors=True)


def test_deploy_ships_split_workers_between_relay_deploy_and_verification():
    # Once the zone routes exist, /api/version is answered by forkmesh-api
    # and /world/*.js by forkmesh-world, so the split deploys must land
    # BEFORE the build-stamp poll and the byte-for-byte World freshness
    # check — and the end-to-end route verification runs after the
    # marketing checks, proving the relay's own build via /health.
    arm = DEPLOY.split('case "${1:-deploy}" in', 1)[1]
    order = [
        arm.index("deploy_split_site_workers"),
        arm.index('verify_deploy "$BUILD_REV"'),
        arm.index("verify_public_assets"),
        arm.index("retire_legacy_marketing_worker"),
        arm.index("verify_marketing_routes"),
        arm.index('verify_split_site_workers "$BUILD_REV"'),
    ]
    assert order == sorted(order)
    # The assets-only configs deploy through the pinned wrangler version;
    # the API split deploys the Python application through pywrangler with
    # the same build stamps and then refreshes its secret set.
    assert "wrangler deploy --config \"$config\"" in DEPLOY
    assert 'for config in wrangler.www.toml wrangler.world.toml; do' in DEPLOY
    assert "pywrangler deploy --config wrangler.api.toml --env \"\"" in DEPLOY
    assert "SPLIT_SECRET_WORKER=forkmesh-api push_secrets" in DEPLOY
    split_body = DEPLOY[DEPLOY.index("deploy_split_site_workers() {"):]
    split_body = split_body[:split_body.index("\n}")]
    for stamp in ('--var "BUILD_REV:${BUILD_REV}"',
                  '--var "APP_VERSION:${APP_VERSION}"',
                  '--var "DEPLOYED_AT_MS:${DEPLOYED_AT_MS}"'):
        assert stamp in split_body, stamp


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
    # The Python Workers identify themselves in their payloads rather than
    # via the assets-layer marker header: /api/version must come back from
    # the api role and the relay-routed /health from the relay, carrying the
    # relay's build stamp.
    assert '"worker"[[:space:]]*:[[:space:]]*"api"' in DEPLOY
    assert '"worker"[[:space:]]*:[[:space:]]*"relay"' in DEPLOY
    assert 'relay /health reports rev' in DEPLOY
