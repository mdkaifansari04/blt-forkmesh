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
    www = config("www")
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
        "www": (www, "forkmesh-www", ["forkmesh.com", "www.forkmesh.com"]),
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

    assert www["services"] == [
        {"binding": "APP", "service": "forkmesh-relay"}
    ]
    assert "services" not in world


def test_obsolete_api_unit_and_duplicate_manifests_are_gone():
    assert not (REPOSITORY / "api").exists()
    assert not (APP_ROOT / "wrangler.api.toml").exists()
    assert not (APP_ROOT / "wrangler.www.toml").exists()
    assert not (APP_ROOT / "wrangler.world.toml").exists()


def test_source_assets_are_physically_owned_by_their_worker():
    assert (REPOSITORY / "www/public/index.html").is_file()
    assert (REPOSITORY / "www/public/blog/one-app-one-mesh/index.html").is_file()
    assert (REPOSITORY / "www/public/docs/index.html").is_file()
    assert (REPOSITORY / "www/docs/operations/worker-split.md").is_file()
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
        source = (REPOSITORY / "www/public/api-client.js").read_bytes()
        for unit in ("app", "www", "world"):
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
            "blog.html",
            "dashboard/index.html",
            "dashboard/repo.html",
            "index.html",
            "install.sh",
            "uninstall.sh",
            "login.html",
            "signup.html",
            "status.html",
        } <= app_files
        assert not any(path.startswith("blog/") for path in app_files)
        assert not any(path.startswith("assets/blog/") for path in app_files)
        assert "docs/index.html" not in app_files
    finally:
        for path in (
            REPOSITORY / "app/dist",
            REPOSITORY / "www/dist",
            REPOSITORY / "world/dist",
        ):
            shutil.rmtree(path, ignore_errors=True)


def test_app_root_and_api_landing_support_official_and_self_host_modes():
    entry = (APP_ROOT / "src/entry.py").read_text(encoding="utf-8")
    manifest = config("app")

    assert 'url.path in ("/api", "/api/", "/developers", "/developers/")' in entry
    assert "_api_metrics.landing_page_response" in entry
    assert 'getattr(self.env, "SINGLE_WORKER_SITE", "")' in entry
    assert "return await self._serve_homepage(url)" in entry
    assert 'url, "dashboard/index.html")' in entry
    assert manifest["vars"]["SINGLE_WORKER_SITE"] == "true"
    assert "/api" in manifest["assets"]["run_worker_first"]
    assert "/api/*" in manifest["assets"]["run_worker_first"]
    for clean_page in (
        "/login",
        "/signup",
        "/status",
    ):
        assert clean_page in manifest["assets"]["run_worker_first"]
    for removed in ("/network", "/leaderboards", "/chat", "/referrals", "/mirror-payouts"):
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
    client = (REPOSITORY / "www/public/api-client.js").read_text(encoding="utf-8")

    assert 'element.prepend(`<meta name="forkmesh-api-origin"' in worker
    assert "env.APP_ORIGIN" in worker
    assert shell.index('src="/api-client.js') < shell.index('src="/world/world.js')
    assert "metadataOrigin()" in client
    assert 'return pathname.startsWith("/api/")' in client
    assert 'path === "/api" || path.startsWith("/api/")' in worker


def test_www_proxies_legacy_api_posts_and_websocket_upgrades_to_app():
    harness = r'''
import fs from "node:fs";
import assert from "node:assert";
const source = fs.readFileSync(process.argv[1], "utf8");
const worker = (await import(
  "data:text/javascript;base64," + Buffer.from(source).toString("base64")
)).default;
const captured = [];
const env = {
  APP_ORIGIN: "https://app.forkmesh.com",
  WORLD_ORIGIN: "https://world.forkmesh.com",
  APP: {
    async fetch(request) {
      captured.push(request);
      return new Response("proxied", { status: 200 });
    },
  },
};
const webhook = new Request("https://forkmesh.com/api/webhooks/polar?event=1", {
  method: "POST",
  headers: { "content-type": "application/json", "x-signature": "signed" },
  body: '{"ok":true}',
});
assert.equal((await worker.fetch(webhook, env)).status, 200);
assert.equal(captured[0].url, "https://app.forkmesh.com/api/webhooks/polar?event=1");
assert.equal(captured[0].method, "POST");
assert.equal(captured[0].headers.get("x-signature"), "signed");
assert.equal(await captured[0].text(), '{"ok":true}');
const upgrade = new Request("https://forkmesh.com/api/world/ws", {
  headers: { upgrade: "websocket", connection: "Upgrade" },
});
await worker.fetch(upgrade, env);
assert.equal(captured[1].url, "https://app.forkmesh.com/api/world/ws");
assert.equal(captured[1].headers.get("upgrade"), "websocket");
const landing = await worker.fetch(new Request("https://forkmesh.com/api"), env);
assert.equal(landing.status, 308);
assert.equal(landing.headers.get("location"), "https://app.forkmesh.com/api");
'''
    completed = subprocess.run(
        ["node", "--input-type=module", "-e", harness, str(REPOSITORY / "www/worker.js")],
        cwd=REPOSITORY,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr


def test_www_feed_proxy_terminates_at_app_feed_handler_without_redirect_loop():
    entry = (APP_ROOT / "src/entry.py").read_text(encoding="utf-8")
    route_start = entry.index('if url.path in ("/api", "/api/"')
    feed_dispatch = entry.index("return await blog_rss_handler(self.env, request)", route_start)
    marketing_redirect = entry.index("external_owner = external_site_route", route_start)
    assert feed_dispatch < marketing_redirect

    harness = r'''
import fs from "node:fs";
import assert from "node:assert";
const source = fs.readFileSync(process.argv[1], "utf8");
const worker = (await import(
  "data:text/javascript;base64," + Buffer.from(source).toString("base64")
)).default;
const captured = [];
const env = {
  APP_ORIGIN: "https://app.forkmesh.com",
  WORLD_ORIGIN: "https://world.forkmesh.com",
  APP: {
    async fetch(request) {
      captured.push(request.url);
      return new Response("<rss/>", {
        status: 200,
        headers: { "content-type": "application/rss+xml" },
      });
    },
  },
};
for (const path of ["/blog/rss.xml", "/blog/feed.xml"]) {
  const response = await worker.fetch(new Request("https://forkmesh.com" + path), env);
  assert.equal(response.status, 200);
  assert.equal(response.headers.get("location"), null);
  assert.equal(response.headers.get("content-type"), "application/rss+xml");
}
assert.deepEqual(captured, [
  "https://app.forkmesh.com/blog/rss.xml",
  "https://app.forkmesh.com/blog/feed.xml",
]);
'''
    completed = subprocess.run(
        ["node", "--input-type=module", "-e", harness, str(REPOSITORY / "www/worker.js")],
        cwd=REPOSITORY,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr


def test_www_static_prefixes_are_never_treated_as_repository_routes():
    harness = r'''
import fs from "node:fs";
import assert from "node:assert";
const source = fs.readFileSync(process.argv[1], "utf8");
const worker = (await import(
  "data:text/javascript;base64," + Buffer.from(source).toString("base64")
)).default;
let appCalls = 0;
const env = {
  APP_ORIGIN: "https://app.forkmesh.com",
  WORLD_ORIGIN: "https://world.forkmesh.com",
  APP: { async fetch() { appCalls += 1; return new Response("app"); } },
  ASSETS: { async fetch(request) { return new Response(new URL(request.url).pathname); } },
};
for (const path of [
  "/assets/logo.png",
  "/assets/blog/features/one-app-one-mesh.webp",
  "/favicon/favicon.ico",
  "/blog/one-app-one-mesh/",
  "/docs/protocol/",
]) {
  const response = await worker.fetch(new Request("https://forkmesh.com" + path), env);
  assert.equal(await response.text(), path);
}
assert.equal(appCalls, 0);
'''
    completed = subprocess.run(
        ["node", "--input-type=module", "-e", harness, str(REPOSITORY / "www/worker.js")],
        cwd=REPOSITORY,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr


def test_deploy_script_documents_safe_cutover_order():
    deploy = (APP_ROOT / "deploy.sh").read_text(encoding="utf-8")
    assert "FORKMESH_PRESERVE_LEGACY_HOSTS=1" in deploy
    assert 'DEPLOY_VERIFY_URL="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"' in deploy
    first_app = deploy.index(
        'FORKMESH_FORCE_DEPLOY=1 FORKMESH_PRESERVE_LEGACY_HOSTS=1 "$0" app'
    )
    world = deploy.index('"$0" world', first_app)
    www = deploy.index('"$0" www', world)
    final_app = deploy.index('FORKMESH_FORCE_DEPLOY=1 "$0" app', www)
    retire = deploy.index("retire_legacy_api_worker", final_app)
    assert first_app < world < www < final_app < retire
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
