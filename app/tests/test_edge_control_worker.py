import os
import subprocess
import tomllib
from pathlib import Path


APP = Path(__file__).resolve().parents[1]
EDGE = APP / "edge-control"
ROUTER_PUBLIC_KEY = "A6EHv_POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg"
ROUTER_SIGNING_SEED = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8"


def test_edge_control_routes_only_the_runtime_independent_endpoints():
    config = tomllib.loads((EDGE / "wrangler.toml").read_text(encoding="utf-8"))
    assert config["name"] == "forkmesh-edge-control"
    assert config["workers_dev"] is False
    assert config["services"] == [{"binding": "APP", "service": "forkmesh-relay"}]
    assert config["triggers"]["crons"] == ["* * * * *"]
    assert config["durable_objects"]["bindings"] == [{
        "name": "FORKMESH_CRON_RUNNER",
        "class_name": "ForkMeshCronRunner",
        "script_name": "forkmesh-relay",
    }]
    assert [route["pattern"] for route in config["routes"]] == [
        "app.forkmesh.com/api/version*",
        "app.forkmesh.com/health*",
        "app.forkmesh.com/api/mirrors/https*",
        "api.forkmesh.com/health*",
        "api.forkmesh.com/api/version*",
        "api.forkmesh.com/api/mirrors/https*",
    ]
    assert all(route["zone_name"] == "forkmesh.com" for route in config["routes"])
    assert not any(route["pattern"].endswith("/api*") for route in config["routes"])


def test_desktop_package_installs_only_the_authored_edge_control_bundle():
    cmake = (APP.parent / "desktop/CMakeLists.txt").read_text(encoding="utf-8")
    install_block = cmake.split("install(DIRECTORY", 1)[1].split(")", 1)[0]
    assert '"${CMAKE_CURRENT_SOURCE_DIR}/../app/edge-control"' in install_block
    for directory in (".wrangler", "node_modules", "dist", "__pycache__", ".pytest_cache"):
        assert f'PATTERN "{directory}" EXCLUDE' in install_block

    excluded = {".wrangler", "node_modules", "dist", "__pycache__", ".pytest_cache"}
    packaged_files = {
        path.relative_to(EDGE).as_posix()
        for path in EDGE.rglob("*")
        if path.is_file() and not excluded.intersection(path.relative_to(EDGE).parts)
    }
    assert packaged_files == {"worker.js", "wrangler.toml"}


def test_official_deploy_updates_python_before_edge_and_verifies_the_overlay():
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    block = deploy.split("deploy_app_target() {", 1)[1].split(
        "\nretire_legacy_api_worker()", 1
    )[0]
    app_deploy = block.index("deploy_app_version")
    secret_publish = block.index("\n        push_secrets\n", app_deploy)
    backend_verify = block.index(
        'verify_deploy "$BUILD_REV" "/api/mainnode"'
    )
    edge_deploy = block.index("\n    deploy_edge_control_version\n", secret_publish)
    edge_verify = block.index(
        'verify_deploy "$BUILD_REV" "/api/version"'
    )
    assert app_deploy < secret_publish < backend_verify < edge_deploy
    assert edge_deploy < edge_verify < block.index("verify_edge_control")
    edge = deploy.split("deploy_edge_control_version() {", 1)[1].split(
        "\nrequire_edge_router_public_key()", 1
    )[0]
    assert 'cd edge-control' in edge
    assert '--var "MIRROR_ROUTER_PUBLIC_KEY:${router_public_key}"' in edge
    assert "MIRROR_ROUTER_SIGNING_SEED" not in edge
    assert "x-forkmesh-edge-control" in deploy
    changed = deploy.split("deploy_target_is_changed() {", 1)[1].split(
        "\nmark_deploy_target()", 1
    )[0]
    assert 'edge_marker="$(printf' in changed
    assert '[ "$edge_marker" = "active" ]' in changed
    assert '"${base%/}/api/mainnode"' in changed
    assert '[ "$backend_fingerprint" = "$current" ]' in changed
    assert '[ "$edge_rev" != "dev" ]' in changed
    assert '[ "$backend_rev" = "$edge_rev" ]' in changed
    assert '[ "$backend_router_public_key" = "$EDGE_ROUTER_PUBLIC_KEY" ]' in changed
    assert '[ "$backend_router_ready" = "true" ]' in changed
    assert '[ "$router_public_key" = "$EDGE_ROUTER_PUBLIC_KEY" ]' in changed
    assert block.index("require_edge_router_public_key") < block.index(
        "push_secrets validate"
    ) < block.index("signal_deploy_status deploying")
    assert block.index("authorize_router_rotation") < block.index(
        "push_secrets validate"
    )
    assert block.index('deploy_edge_control_version ""') < secret_publish
    assert block.index("verify_router_identity_disabled") < secret_publish
    preserve_publish = block.index("push_secrets publish-preserve-router")
    assert block.index('verify_live_router_public_key "$EDGE_ROUTER_PUBLIC_KEY"') < (
        preserve_publish
    ) < backend_verify
    assert '"$DEPLOY_TARGET_FINGERPRINT"' in block[backend_verify:edge_deploy]
    validator = deploy.split("require_edge_router_public_key() {", 1)[1].split(
        "\n}", 1
    )[0]
    assert "^[A-Za-z0-9_-]{43}$" in validator
    assert "_router_key_pair_matches" in validator
    assert "MIRROR_ROUTER_SIGNING_SEED" in deploy
    verifier = deploy.split("verify_deploy() {", 1)[1].split(
        "\nofficial_multi_host_enabled()", 1
    )[0]
    assert 'local endpoint="${2:-/api/version}"' in verifier
    assert 'local base="${3:-${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}}"' in verifier
    assert 'local expected_fingerprint="${4:-}"' in verifier
    assert 'local expected_router_public_key="${5:-}"' in verifier
    assert 'local url="$base$endpoint"' in verifier
    assert '[ "$worker" = "app" ]' in verifier
    edge_url = deploy.split("edge_control_verify_url() {", 1)[1].split(
        "\n}", 1
    )[0]
    assert "DEPLOY_VERIFY_EDGE_URL" in edge_url
    assert "official_multi_host_enabled" in edge_url
    assert 'printf \'%s\' "https://app.forkmesh.com"' in edge_url

    secret_case = deploy.split("\n    secrets)", 1)[1].split("\n        ;;", 1)[0]
    assert "publish_preserving_router_identity" in secret_case
    assert "push_secrets\n" not in secret_case
    preserve = deploy.split("publish_preserving_router_identity() {", 1)[1].split(
        "\n}", 1
    )[0]
    assert "push_secrets publish-preserve-router" in preserve
    assert 'verify_live_router_public_key "$preserved_key"' in preserve
    rotate_case = deploy.split("\n    rotate-mirror-router)", 1)[1].split(
        "\n        ;;", 1
    )[0]
    assert "FORKMESH_ALLOW_ROUTER_KEY_ROTATION=1" in rotate_case
    assert "FORKMESH_FORCE_DEPLOY=1" in rotate_case
    alias = deploy.split("verify_legacy_api_alias() {", 1)[1].split(
        "\n}\n\ndeploy_app_target()", 1
    )[0]
    assert 'verify_deploy "$expected_rev" "/api/version" "$base"' in alias
    assert '"$expected_fingerprint"' in alias
    assert '[ "$marker" != "active" ]' in alias
    assert 'verify_live_router_public_key "$EDGE_ROUTER_PUBLIC_KEY" "$base"' in alias


def test_unoverlaid_backend_identity_includes_the_deploy_fingerprint():
    entry = (APP / "src/entry.py").read_text(encoding="utf-8")
    mainnode = entry.split(
        'if url.path in ("/health", "/api/mainnode"):', 1
    )[1].split('if url.path in ("/api/version", "/api/version/"):', 1)[0]
    assert '"deployFingerprint"' in mainnode
    assert 'getattr(self.env, "DEPLOY_FINGERPRINT", "")' in mainnode
    assert 'payload["routerPublicKey"]' in mainnode
    assert 'payload["routerIdentityReady"]' in mainnode
    route = entry.split("    async def _route(self, request, url):", 1)[1].split(
        "        private_access_match =", 1
    )[0]
    public_route = route.split(
        "return await https_mirror_endpoint_handler", 1
    )[0]
    assert "url.path.rstrip" not in public_route
    assert "HTTPS_MIRROR_ENDPOINT_PATH + \"/\"" in public_route


def test_shell_backend_probe_targets_the_unoverlaid_mainnode_endpoint(tmp_path):
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    functions = deploy[deploy.index("_py_http_get() {"):deploy.index(
        "official_multi_host_enabled() {"
    )]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        "#!/usr/bin/env bash\n"
        "url=\"${@: -1}\"\n"
        "printf '{\"ok\":true,\"rev\":\"abc123\",\"worker\":\"app\",\"deployFingerprint\":\"%s\",\"routerPublicKey\":\"%s\",\"routerIdentityReady\":%s}\\n200' \"$PROBE_FP\" \"$PROBE_ROUTER_KEY\" \"$PROBE_ROUTER_READY\"\n"
        "printf '%s\\n' \"$url\" >\"$PROBE_CAPTURE\"\n",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    capture = tmp_path / "url"
    script = functions + (
        '\nverify_deploy "abc123" "/api/mainnode" "" '
        '"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff" '
        f'"{ROUTER_PUBLIC_KEY}"\n'
    )
    completed = subprocess.run(
        ["bash", "-c", script],
        cwd=APP,
        env={
            **os.environ,
            "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
            "PROBE_CAPTURE": str(capture),
            "PROBE_FP": "f" * 64,
            "PROBE_ROUTER_KEY": ROUTER_PUBLIC_KEY,
            "PROBE_ROUTER_READY": "true",
            "DEPLOY_VERIFY_URL": "https://app.example",
        },
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr
    assert capture.read_text(encoding="utf-8").strip() == (
        "https://app.example/api/mainnode"
    )

    stale = subprocess.run(
        ["bash", "-c", script],
        cwd=APP,
        env={
            **os.environ,
            "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
            "PROBE_CAPTURE": str(capture),
            "PROBE_FP": "0" * 64,
            "PROBE_ROUTER_KEY": ROUTER_PUBLIC_KEY,
            "PROBE_ROUTER_READY": "true",
            "DEPLOY_VERIFY_URL": "https://app.example",
            "FORKMESH_DEPLOY_VERIFY_ATTEMPTS": "1",
            "FORKMESH_DEPLOY_VERIFY_BACKOFF": "0",
        },
        text=True,
        capture_output=True,
        check=False,
    )
    assert stale.returncode != 0
    assert "fingerprint" in stale.stderr


def test_app_skip_requires_matching_edge_backend_revision_fingerprint_and_key(
    tmp_path,
):
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    helpers = deploy[deploy.index("_py_http_get() {"):deploy.index(
        "official_multi_host_enabled() {"
    )]
    edge_url = deploy[deploy.index("edge_control_verify_url() {"):deploy.index(
        "\n}\n\nverify_edge_control()"
    ) + 2]
    changed = deploy[deploy.index("deploy_target_is_changed() {"):deploy.index(
        "\n}\n\nmark_deploy_target()"
    ) + 2]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
url="${@: -1}"
for arg in "$@"; do
    if [[ "$arg" == -*I* ]]; then
        printf 'HTTP/2 200\\nx-forkmesh-edge-control: active\\n\\n'
        exit 0
    fi
done
case "$url" in
    */api/version)
        printf '{"rev":"%s","worker":"%s","deployFingerprint":"%s"}\\n200' "$EDGE_REV" "$EDGE_WORKER" "$EDGE_FP"
        ;;
    */api/mainnode)
        printf '{"rev":"%s","worker":"%s","deployFingerprint":"%s","routerPublicKey":"%s","routerIdentityReady":%s}\\n200' "$BACKEND_REV" "$BACKEND_WORKER" "$BACKEND_FP" "$BACKEND_ROUTER_KEY" "$BACKEND_ROUTER_READY"
        ;;
    */api/mirrors/https)
        printf '{"routerPublicKey":"%s"}\\n200' "$LIVE_ROUTER_KEY"
        ;;
    *) exit 2 ;;
esac
""",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    fingerprint = subprocess.run(
        ["python3", "tools/deploy_targets.py", "fingerprint", "app"],
        cwd=APP,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.strip()
    script = "\n".join((
        helpers,
        "official_multi_host_enabled() { return 1; }",
        edge_url,
        changed,
        'deploy_target_is_changed app; exit $?',
    ))
    base_env = {
        **os.environ,
        "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        "DEPLOY_VERIFY_URL": "https://app.example",
        "DEPLOY_VERIFY_EDGE_URL": "https://app.example",
        "EDGE_ROUTER_PUBLIC_KEY": ROUTER_PUBLIC_KEY,
        "EDGE_FP": fingerprint,
        "BACKEND_FP": fingerprint,
        "EDGE_REV": "app-target-rev",
        "BACKEND_REV": "app-target-rev",
        "EDGE_WORKER": "app",
        "BACKEND_WORKER": "app",
        "BACKEND_ROUTER_KEY": ROUTER_PUBLIC_KEY,
        "BACKEND_ROUTER_READY": "true",
        "LIVE_ROUTER_KEY": ROUTER_PUBLIC_KEY,
    }
    current = subprocess.run(
        ["bash", "-c", script], cwd=APP, env=base_env, check=False
    )
    assert current.returncode == 3

    for override in (
        {"BACKEND_FP": "0" * 64},
        {"BACKEND_REV": "stale"},
        {"BACKEND_WORKER": "relay"},
        {"BACKEND_ROUTER_KEY": "B" * 43},
        {"BACKEND_ROUTER_READY": "false"},
        {"EDGE_WORKER": "relay"},
        {"LIVE_ROUTER_KEY": "B" * 43},
    ):
        stale = subprocess.run(
            ["bash", "-c", script],
            cwd=APP,
            env={**base_env, **override},
            check=False,
        )
        assert stale.returncode == 0, override


def test_live_router_key_mismatch_requires_explicit_fail_closed_rotation(tmp_path):
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    helpers = deploy[deploy.index("_py_http_get() {"):deploy.index(
        "official_multi_host_enabled() {"
    )]
    rotation = deploy[deploy.index("ROUTER_ROTATION_ACTIVE=0"):deploy.index(
        "\nedge_control_verify_url()"
    )]
    edge_url = deploy[deploy.index("edge_control_verify_url() {"):deploy.index(
        "\n}\n\nverify_edge_control()"
    ) + 2]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        "#!/usr/bin/env bash\n"
        "printf '{\"routerPublicKey\":\"%s\"}\\n200' \"$LIVE_ROUTER_KEY\"\n",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    prefix = "\n".join((
        helpers,
        "official_multi_host_enabled() { return 1; }",
        edge_url,
        rotation,
        f'EDGE_ROUTER_PUBLIC_KEY="{ROUTER_PUBLIC_KEY}"',
    ))
    env = {
        **os.environ,
        "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        "DEPLOY_VERIFY_EDGE_URL": "https://app.example",
        "LIVE_ROUTER_KEY": "B" * 43,
    }
    normal = subprocess.run(
        ["bash", "-c", prefix + "\nauthorize_router_rotation\n"],
        cwd=APP,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    assert normal.returncode != 0
    assert "differs from the live identity" in normal.stderr

    first = subprocess.run(
        [
            "bash",
            "-c",
            prefix
            + "\nauthorize_router_rotation && "
            + '[ "$ROUTER_ROTATION_ACTIVE" = "1" ]\n',
        ],
        cwd=APP,
        env={
            **env,
            "LIVE_ROUTER_KEY": "",
            "FORKMESH_FIRST_DEPLOY": "1",
        },
        text=True,
        capture_output=True,
        check=False,
    )
    assert first.returncode == 0, first.stderr
    assert "fail-closed state" in first.stdout

    explicit = subprocess.run(
        [
            "bash",
            "-c",
            prefix
            + "\nauthorize_router_rotation && "
            + '[ "$ROUTER_ROTATION_ACTIVE" = "1" ]\n',
        ],
        cwd=APP,
        env={**env, "FORKMESH_ALLOW_ROUTER_KEY_ROTATION": "1"},
        text=True,
        capture_output=True,
        check=False,
    )
    assert explicit.returncode == 0, explicit.stderr
    assert ROUTER_SIGNING_SEED not in explicit.stdout + explicit.stderr


def test_rotation_requires_an_edge_marked_disabled_identity(tmp_path):
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    helpers = deploy[deploy.index("_py_http_get() {"):deploy.index(
        "official_multi_host_enabled() {"
    )]
    disabled = deploy[deploy.index("verify_router_identity_disabled() ("):deploy.index(
        "\n)\n\nverify_legacy_api_alias()"
    ) + 2]
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
header_file=""
while [ "$#" -gt 0 ]; do
    if [ "$1" = "-D" ]; then
        header_file="$2"
        shift 2
        continue
    fi
    shift
done
printf 'HTTP/2 503\\n' >"$header_file"
if [ "${EDGE_MARKER:-0}" = "1" ]; then
    printf 'x-forkmesh-edge-control: active\\n' >>"$header_file"
fi
printf '\\n' >>"$header_file"
printf '%s\\n503' '{"ok":false,"protocol":"forkmesh-masked-proxy-v1","registration":"forkmesh-https-endpoint-v1","routerPublicKey":""}'
""",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    script = "\n".join((
        helpers,
        'edge_control_verify_url() { printf "%s" "https://app.example"; }',
        disabled,
        "verify_router_identity_disabled",
    ))
    env = {
        **os.environ,
        "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        "FORKMESH_ROUTER_VERIFY_ATTEMPTS": "1",
        "FORKMESH_ROUTER_VERIFY_BACKOFF": "0",
    }
    unmarked = subprocess.run(
        ["bash", "-c", script], env=env, text=True, capture_output=True, check=False
    )
    assert unmarked.returncode != 0
    marked = subprocess.run(
        ["bash", "-c", script],
        env={**env, "EDGE_MARKER": "1"},
        text=True,
        capture_output=True,
        check=False,
    )
    assert marked.returncode == 0, marked.stderr


def test_legacy_alias_requires_matching_edge_identity_and_router_key(tmp_path):
    deploy = (APP / "deploy.sh").read_text(encoding="utf-8")
    helpers = deploy[deploy.index("_py_http_get() {"):deploy.index(
        "official_multi_host_enabled() {"
    )]
    alias = "verify_legacy_api_alias() {" + deploy.split(
        "verify_legacy_api_alias() {", 1
    )[1].split("\n}\n\ndeploy_app_target()", 1)[0] + "\n}"
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    curl = fake_bin / "curl"
    curl.write_text(
        """#!/usr/bin/env bash
set -euo pipefail
url="${@: -1}"
head_request=0
for arg in "$@"; do
    [[ "$arg" == -*I* ]] && head_request=1
done
if [ "$head_request" = 1 ]; then
    case "$url" in
        */api/version)
            printf 'HTTP/2 200\\n'
            [ "${EDGE_MARKER:-1}" = 1 ] && printf 'x-forkmesh-edge-control: active\\n'
            printf '\\n'
            ;;
        */)
            printf 'HTTP/2 308\\nlocation: https://app.forkmesh.com/\\n\\n'
            ;;
        *) exit 2 ;;
    esac
    exit 0
fi
case "$url" in
    */api/version)
        printf '{"rev":"%s","worker":"app","deployFingerprint":"%s"}\\n200' "$BUILD_REV" "$DEPLOY_TARGET_FINGERPRINT"
        ;;
    */api/mirrors/https)
        printf '{"routerPublicKey":"%s"}\\n200' "$EDGE_ROUTER_PUBLIC_KEY"
        ;;
    *) exit 2 ;;
esac
""",
        encoding="utf-8",
    )
    curl.chmod(0o755)
    script = "\n".join((helpers, alias, "verify_legacy_api_alias"))
    env = {
        **os.environ,
        "PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", ""),
        "BUILD_REV": "app-rev",
        "DEPLOY_TARGET_FINGERPRINT": "f" * 64,
        "EDGE_ROUTER_PUBLIC_KEY": ROUTER_PUBLIC_KEY,
        "DEPLOY_VERIFY_LEGACY_API_URL": "https://api.example",
        "FORKMESH_DEPLOY_VERIFY_ATTEMPTS": "1",
        "FORKMESH_DEPLOY_VERIFY_BACKOFF": "0",
        "FORKMESH_ROUTER_VERIFY_ATTEMPTS": "1",
        "FORKMESH_ROUTER_VERIFY_BACKOFF": "0",
    }
    matching = subprocess.run(
        ["bash", "-c", script], env=env, text=True, capture_output=True, check=False
    )
    assert matching.returncode == 0, matching.stderr
    unmarked = subprocess.run(
        ["bash", "-c", script],
        env={**env, "EDGE_MARKER": "0"},
        text=True,
        capture_output=True,
        check=False,
    )
    assert unmarked.returncode != 0
    assert "did not traverse" in unmarked.stderr


def test_edge_control_responses_cors_alias_and_fallback_contracts():
    harness = r'''
import fs from "node:fs";
import assert from "node:assert/strict";
const source = fs.readFileSync(process.argv[1], "utf8");
const worker = (await import(
  "data:text/javascript;base64," + Buffer.from(source).toString("base64")
)).default;
const baseEnv = {
  BUILD_REV: "abc123",
  APP_VERSION: "1.2.3",
  DEPLOY_FINGERPRINT: "f".repeat(64),
  WORKER_ROLE: "app",
  NODE_NAME: "forkmesh-mainnode",
  NODE_SOLANA_ADDRESS: "",
  PUBLIC_BASE_URL: "https://forkmesh.com",
  API_ORIGIN: "https://app.forkmesh.com",
  APP_ORIGIN: "https://app.forkmesh.com",
  WWW_ORIGIN: "https://www.forkmesh.com",
  WORLD_ORIGIN: "https://world.forkmesh.com",
  CORS_ALLOWED_ORIGINS: "https://forkmesh.com,https://www.forkmesh.com,https://app.forkmesh.com,https://api.forkmesh.com,https://world.forkmesh.com",
  SINGLE_WORKER_SITE: "false",
  MIRROR_ROUTER_PUBLIC_KEY: "A".repeat(43),
  APP: { async fetch() { throw new Error("unexpected service fallback"); } },
};

const health = await worker.fetch(new Request("https://app.forkmesh.com/health"), baseEnv);
assert.equal(health.status, 200);
assert.equal(health.headers.get("x-forkmesh-edge-control"), "active");
assert.deepEqual(await health.json(), {
  ok: true,
  service: "forkmesh-mainnode",
  rev: "abc123",
  worker: "app",
  node: "forkmesh-mainnode",
  nodeSolanaAddress: "",
  websocket: "/api/repo/{owner}/{repo}/rooms/{room}/ws",
  compatWebsocket: "/api/room/{room}/ws",
  capabilities: ["encrypted-relay-rooms", "repo-scoped-rooms", "ephemeral-ciphertext-broadcast"],
  runtime: "javascript-edge-control",
});
const crossOriginHealth = await worker.fetch(new Request("https://app.forkmesh.com/health", {
  headers: { origin: "https://world.forkmesh.com" },
}), baseEnv);
assert.equal(crossOriginHealth.headers.get("access-control-allow-origin"), "https://world.forkmesh.com");
const sameOriginHealth = await worker.fetch(new Request("https://app.forkmesh.com/health", {
  headers: { origin: "https://app.forkmesh.com" },
}), baseEnv);
assert.equal(sameOriginHealth.headers.get("access-control-allow-origin"), "https://app.forkmesh.com");
const healthHead = await worker.fetch(new Request("https://app.forkmesh.com/health", {
  method: "HEAD",
}), baseEnv);
assert.equal(healthHead.status, 200);
assert.equal(await healthHead.text(), "");

for (const host of ["app.forkmesh.com", "api.forkmesh.com"]) {
  const response = await worker.fetch(new Request(`https://${host}/api/version`, {
    headers: { origin: "https://world.forkmesh.com" },
  }), baseEnv);
  const body = await response.json();
  assert.equal(response.status, 200);
  assert.equal(body.worker, "app");
  assert.equal(body.rev, "abc123");
  assert.equal(body.deployFingerprint, "f".repeat(64));
  assert.equal(response.headers.get("access-control-allow-origin"), "https://world.forkmesh.com");
  assert.equal(response.headers.get("access-control-allow-credentials"), "true");
}
const versionHead = await worker.fetch(new Request("https://api.forkmesh.com/api/version", {
  method: "HEAD",
}), baseEnv);
assert.equal(versionHead.status, 200);
assert.equal(await versionHead.text(), "");

const identity = await worker.fetch(new Request("https://app.forkmesh.com/api/mirrors/https"), baseEnv);
assert.equal(identity.status, 200);
assert.equal(identity.headers.get("cache-control"), "no-store");
assert.deepEqual(await identity.json(), {
  ok: true,
  protocol: "forkmesh-masked-proxy-v1",
  registration: "forkmesh-https-endpoint-v1",
  routerPublicKey: "A".repeat(43),
  nonCustodial: true,
  repositoryBytesInD1: false,
});
const identitySlash = await worker.fetch(
  new Request("https://app.forkmesh.com/api/mirrors/https/"), baseEnv,
);
assert.equal(identitySlash.status, 200);
for (const suffix of ["//", "///"]) {
  const alternate = await worker.fetch(
    new Request(`https://app.forkmesh.com/api/mirrors/https${suffix}`),
    { ...baseEnv, MIRROR_ROUTER_PUBLIC_KEY: "" },
  );
  assert.equal(alternate.status, 404);
  assert.equal(alternate.headers.get("cache-control"), "no-store");
  assert.equal(Object.hasOwn(await alternate.json(), "routerPublicKey"), false);
}
const unavailable = await worker.fetch(
  new Request("https://app.forkmesh.com/api/mirrors/https"),
  { ...baseEnv, MIRROR_ROUTER_PUBLIC_KEY: "" },
);
assert.equal(unavailable.status, 503);
assert.equal(unavailable.headers.get("cache-control"), "no-store");
assert.equal((await unavailable.json()).ok, false);
const malformedKey = await worker.fetch(
  new Request("https://app.forkmesh.com/api/mirrors/https"),
  { ...baseEnv, MIRROR_ROUTER_PUBLIC_KEY: "not-a-public-key" },
);
assert.equal(malformedKey.status, 503);
assert.equal(malformedKey.headers.get("cache-control"), "no-store");
assert.equal((await malformedKey.json()).routerPublicKey, "");

const allowed = await worker.fetch(new Request("https://app.forkmesh.com/api/version", {
  method: "OPTIONS",
  headers: {
    origin: "https://world.forkmesh.com",
    "access-control-request-method": "POST",
    "access-control-request-headers": "authorization, content-type, x-forkmesh-signature",
    "access-control-request-private-network": "true",
  },
}), baseEnv);
assert.equal(allowed.status, 204);
assert.equal(allowed.headers.get("access-control-allow-origin"), "https://world.forkmesh.com");
assert.match(allowed.headers.get("access-control-allow-headers"), /x-forkmesh-signature/);
assert.equal(allowed.headers.get("access-control-allow-private-network"), "true");
assert.match(allowed.headers.get("vary"), /Access-Control-Request-Headers/);

const denied = await worker.fetch(new Request("https://app.forkmesh.com/api/version", {
  method: "OPTIONS",
  headers: {
    origin: "https://untrusted.invalid",
    "access-control-request-method": "GET",
  },
}), baseEnv);
assert.equal(denied.status, 403);
assert.equal(denied.headers.get("access-control-allow-origin"), null);

const selfHostCalls = [];
const selfHost = {
  ...baseEnv,
  PUBLIC_BASE_URL: "https://forkmesh.example.com",
  API_ORIGIN: "https://forkmesh.example.com",
  APP_ORIGIN: "https://forkmesh.example.com",
  WWW_ORIGIN: "https://forkmesh.example.com",
  WORLD_ORIGIN: "https://forkmesh-world.example.com",
  CORS_ALLOWED_ORIGINS: "https://forkmesh.example.com,https://forkmesh-world.example.com",
  SINGLE_WORKER_SITE: "true",
  APP: { async fetch(request) { selfHostCalls.push(request); return new Response("python", { status: 201 }); } },
};
const selfPreflight = await worker.fetch(new Request("https://forkmesh.example.com/api/version", {
  method: "OPTIONS",
  headers: {
    origin: "https://forkmesh-world.example.com",
    "access-control-request-method": "GET",
  },
}), selfHost);
assert.equal(selfPreflight.status, 204);
const centralDenied = await worker.fetch(new Request("https://forkmesh.example.com/api/version", {
  method: "OPTIONS",
  headers: {
    origin: "https://app.forkmesh.com",
    "access-control-request-method": "GET",
  },
}), selfHost);
assert.equal(centralDenied.status, 403);
const registration = await worker.fetch(new Request("https://forkmesh.example.com/api/mirrors/https", {
  method: "POST",
  body: "{}",
  headers: { "content-type": "application/json" },
}), selfHost);
assert.equal(registration.status, 201);
assert.equal(selfHostCalls.length, 1);
assert.equal(await selfHostCalls[0].text(), "{}");

const fallbackCalls = [];
const fallbackEnv = {
  ...baseEnv,
  APP: { async fetch(request) { fallbackCalls.push(request); return new Response("python", { status: 207 }); } },
};
for (const request of [
  new Request("https://app.forkmesh.com/api/mirrors/https", { method: "POST", body: "signed" }),
  new Request("https://app.forkmesh.com/api/mirrors/https", { method: "HEAD" }),
  new Request("https://app.forkmesh.com/api/version-malformed"),
  new Request("https://app.forkmesh.com/health-malformed"),
]) {
  const response = await worker.fetch(request, fallbackEnv);
  assert.equal(response.status, 207);
}
assert.equal(fallbackCalls.length, 4);
assert.equal(await fallbackCalls[0].text(), "signed");
assert.equal(fallbackCalls[1].method, "HEAD");
assert.equal(fallbackCalls[2].url, "https://app.forkmesh.com/api/version-malformed");
assert.equal(fallbackCalls[3].url, "https://app.forkmesh.com/health-malformed");

const cronCalls = [];
const cronEnv = {
  FORKMESH_CRON_RUNNER: {
    idFromName(name) { cronCalls.push(["id", name]); return "runner-id"; },
    get(id) {
      cronCalls.push(["get", id]);
      return {
        async fetch(url) {
          cronCalls.push(["fetch", url]);
          return new Response(null, { status: 204 });
        },
      };
    },
  },
};
await worker.scheduled({}, cronEnv);
assert.deepEqual(cronCalls, [
  ["id", "scheduled-runner-v1"],
  ["get", "runner-id"],
  ["fetch", "https://forkmesh.internal/cron-runner/kick"],
]);
await assert.rejects(
  worker.scheduled({}, {
    FORKMESH_CRON_RUNNER: {
      idFromName() { return "runner-id"; },
      get() { return { async fetch() { return new Response(null, { status: 503 }); } }; },
    },
  }),
  /cron runner rejected trigger kick \(503\)/,
);
'''
    completed = subprocess.run(
        ["node", "--input-type=module", "-e", harness, str(EDGE / "worker.js")],
        cwd=APP.parent,
        text=True,
        capture_output=True,
        check=False,
    )
    assert completed.returncode == 0, completed.stderr
