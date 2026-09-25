import shutil
import subprocess
import sys
import tomllib
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
APP_ROOT = REPOSITORY / "app"


def config(unit):
    return tomllib.loads(
        (REPOSITORY / unit / "wrangler.toml").read_text(encoding="utf-8")
    )


def test_public_workers_and_app_edge_control_have_disjoint_runtime_roles():
    app = config("app")
    edge = tomllib.loads(
        (APP_ROOT / "edge-control/wrangler.toml").read_text(encoding="utf-8")
    )
    world = config("world")

    assert app["name"] == "forkmesh-relay"
    assert app["main"] == "src_build/entry.py"
    assert app["vars"]["WORKER_ROLE"] == "app"
    assert app["vars"]["API_ORIGIN"] == (
        "https://forkmesh-relay.owaspblt.workers.dev"
    )
    assert app["vars"]["PUBLIC_BASE_URL"] == (
        "https://forkmesh-relay.owaspblt.workers.dev"
    )
    assert app["vars"]["SINGLE_WORKER_SITE"] == "true"
    assert app["workers_dev"] is True
    assert "routes" not in app
    assert app["assets"]["directory"] == "./dist"
    assert "triggers" not in app
    assert "durable_objects" in app and "migrations" in app
    assert edge["name"] == "forkmesh-edge-control"
    assert edge["main"] == "worker.js"
    assert edge["services"] == [
        {"binding": "APP", "service": "forkmesh-relay"}
    ]
    assert edge["triggers"]["crons"] == ["* * * * *"]
    assert edge["durable_objects"]["bindings"] == [{
        "name": "FORKMESH_CRON_RUNNER",
        "class_name": "ForkMeshCronRunner",
        "script_name": "forkmesh-relay",
    }]
    assert [route["pattern"] for route in edge["routes"]] == [
        "app.forkmesh.com/api/version*",
        "app.forkmesh.com/health*",
        "app.forkmesh.com/api/mirrors/https*",
        "api.forkmesh.com/health*",
        "api.forkmesh.com/api/version*",
        "api.forkmesh.com/api/mirrors/https*",
    ]
    assert all(route["zone_name"] == "forkmesh.com" for route in edge["routes"])
    for backend_only in (
        "ai",
        "assets",
        "d1_databases",
        "kv_namespaces",
        "migrations",
    ):
        assert backend_only not in edge

    expected = {
        "world": (world, "forkmesh-world", ["world.forkmesh.com"]),
    }
    for unit, (manifest, name, hosts) in expected.items():
        assert manifest["name"] == name
        assert manifest["main"] == "worker.js"
        assert manifest["assets"]["directory"] == "./dist"
        assert manifest["assets"]["run_worker_first"] is True
        assert [route["pattern"] for route in manifest["routes"]] == hosts
        assert all(route["custom_domain"] for route in manifest["routes"])
        for backend_only in (
            "ai",
            "d1_databases",
            "durable_objects",
            "kv_namespaces",
            "migrations",
            "triggers",
        ):
            assert backend_only not in manifest, (unit, backend_only)

    assert "services" not in world


def test_obsolete_api_unit_and_duplicate_manifests_are_gone():
    assert not (REPOSITORY / "api").exists()
    assert not (APP_ROOT / "wrangler.api.toml").exists()
    assert not (APP_ROOT / "wrangler.www.toml").exists()
    assert not (APP_ROOT / "wrangler.world.toml").exists()


def test_source_assets_are_physically_owned_by_their_worker():
    # The marketing site is gone; the app owns the site chrome it serves.
    assert not (REPOSITORY / "www").exists()
    assert (REPOSITORY / "app/public/api-client.js").is_file()
    assert (REPOSITORY / "app/public/site-header.js").is_file()
    assert (REPOSITORY / "app/public/_headers").is_file()
    assert (REPOSITORY / "app/public/dashboard/repo.html").is_file()
    assert (REPOSITORY / "app/public/login.html").is_file()
    assert (REPOSITORY / "app/public/assets/file-icons/file_type_git.svg").is_file()
    assert (REPOSITORY / "world/public/world/world.js").is_file()
    assert (
        REPOSITORY
        / "world/public/assets/songs/ForkMeshForever(IndiePop).mp3"
    ).is_file()
    assert not (APP_ROOT / "docs").exists()


def test_staging_is_self_host_complete_without_shipping_official_blog():
    subprocess.run(
        [sys.executable, str(APP_ROOT / "tools/build_site_assets.py")],
        cwd=REPOSITORY,
        check=True,
        capture_output=True,
    )
    try:
        source = (REPOSITORY / "app/public/api-client.js").read_bytes()
        for unit in ("app", "world"):
            assert (REPOSITORY / unit / "dist/api-client.js").read_bytes() == source
            assert (REPOSITORY / unit / "dist/auth-page-theme.css").is_file()
            assert (REPOSITORY / unit / "dist/mesh-page-theme.css").is_file()

        app_files = {
            path.relative_to(APP_ROOT / "dist").as_posix()
            for path in (APP_ROOT / "dist").rglob("*")
            if path.is_file()
        }
        assert {
            "api-client.js",
            "dashboard/index.html",
            "dashboard/repo.html",
            "index.html",
            "install.sh",
            "uninstall.sh",
            "login.html",
            "signup.html",
        } <= app_files
        assert "status.html" not in app_files
        assert "blog.html" not in app_files
        assert not any(path.startswith("blog/") for path in app_files)
        assert not any(path.startswith("assets/blog/") for path in app_files)
        assert "docs/index.html" not in app_files
    finally:
        for path in (
            REPOSITORY / "app/dist",
            REPOSITORY / "world/dist",
        ):
            shutil.rmtree(path, ignore_errors=True)


def test_app_root_opens_dashboard_and_api_landing_stays_worker_owned():
    entry = (APP_ROOT / "src/entry.py").read_text(encoding="utf-8")
    manifest = config("app")

    assert 'url.path in ("/api", "/api/", "/developers", "/developers/")' in entry
    assert "_api_metrics.landing_page_response" in entry
    # No landing page in any mode: `/` always redirects to the dashboard.
    assert 'location = "/dashboard"' in entry
    assert "_serve_homepage" not in entry
    # Still set: CORS reads it to drop ForkMesh's canonical frontend origins.
    assert manifest["vars"]["SINGLE_WORKER_SITE"] == "true"
    assert "/api" in manifest["assets"]["run_worker_first"]
    assert "/api/*" in manifest["assets"]["run_worker_first"]
    for clean_page in (
        "/login",
        "/signup",
    ):
        assert clean_page in manifest["assets"]["run_worker_first"]
    for removed in ("/status", "/network", "/leaderboards", "/chat", "/referrals", "/mirror-payouts"):
        assert removed not in manifest["assets"]["run_worker_first"]


def test_app_keeps_blog_docs_worker_owned_without_forkmesh_marketing_routes():
    entry = (APP_ROOT / "src/entry.py").read_text(encoding="utf-8")
    routes = config("app")["assets"]["run_worker_first"]
    static_routes = (APP_ROOT / "src/static_routes.py").read_text(encoding="utf-8")

    assert 'origin_name = (' in entry
    assert '"WORLD_ORIGIN" if external_owner == "world" else "WWW_ORIGIN"' in entry
    assert 'return Response("", status=308, headers={"location": destination})' in entry
    for path in ("/docs", "/docs/*", "/blog", "/blog/*"):
        assert path in routes
    for path in ("/desktop", "/pricing", "/world", "/world/*"):
        assert path not in routes
    assert '"docs"' in static_routes
    assert '"blog"' in static_routes


def test_world_shell_injects_configured_app_origin_before_api_client():
    worker = (REPOSITORY / "world/worker.js").read_text(encoding="utf-8")
    shell = (REPOSITORY / "world/public/world/index.html").read_text(
        encoding="utf-8"
    )
    client = (REPOSITORY / "app/public/api-client.js").read_text(encoding="utf-8")

    assert 'element.prepend(`<meta name="forkmesh-api-origin"' in worker
    assert "env.APP_ORIGIN" in worker
    assert shell.index('src="/api-client.js') < shell.index('src="/world/world.js')
    assert "metadataOrigin()" in client
    assert 'return pathname.startsWith("/api/")' in client
    assert 'path === "/api" || path.startsWith("/api/")' in worker


def test_feed_routes_dispatch_before_the_marketing_redirect():
    entry = (APP_ROOT / "src/entry.py").read_text(encoding="utf-8")
    route_start = entry.index('if url.path in ("/api", "/api/"')
    feed_dispatch = entry.index("return await blog_rss_handler(self.env, request)", route_start)
    marketing_redirect = entry.index("external_owner = external_site_route", route_start)
    assert feed_dispatch < marketing_redirect


def test_deploy_script_documents_safe_deploy_order():
    deploy = (APP_ROOT / "deploy.sh").read_text(encoding="utf-8")
    # No www Worker means no forkmesh.com hand-off: the App never re-attaches
    # the marketing hosts or stages a cutover bundle.
    assert "FORKMESH_PRESERVE_LEGACY_HOSTS" not in deploy
    assert "build_site_assets.py cutover" not in deploy
    assert '"$0" www' not in deploy
    changed = deploy[deploy.index("deploy_changed_workers() {"):]
    changed = changed[:changed.index("\n}\n")]
    assert changed.endswith(
        '    "$0" app\n    "$0" world\n    retire_legacy_api_worker'
    )
    assert "deploy_api_target" not in deploy
    assert "deploy_static_target app" not in deploy
    assert "verify_legacy_api_alias" in deploy
    assert 'tolower($1) == "location:"' in deploy
    assert 'sub(/^[^:]*:[[:space:]]*/, "", value)' in deploy
    retire_function = deploy.index("retire_legacy_api_worker()")
    assert deploy.index("verify_legacy_api_alias", retire_function) < deploy.index(
        "pywrangler delete --name forkmesh-api", retire_function
    )


def test_post_deploy_verifier_uses_an_explicit_operational_user_agent():
    verifier = (APP_ROOT / "tools/verify_split_deployment.py").read_text(
        encoding="utf-8"
    )
    assert '"User-Agent": "forkmesh-deploy-verify/1.0"' in verifier
