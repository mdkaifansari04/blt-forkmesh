#!/usr/bin/env python3
"""Contracts for the token-in-memory Cloudflare bootstrapper."""

import base64
import json
from pathlib import Path
import re
import sys
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import cloudflare_bootstrap as bootstrap_module  # noqa: E402


PUBLIC_KEY = base64.urlsafe_b64encode(b"P" * 32).decode().rstrip("=")
SIGNATURE = base64.urlsafe_b64encode(b"S" * 64).decode().rstrip("=")


class FakeAPI:
    def __init__(self):
        self.verified = False
        self.calls = []
        self.database_created = True
        self.secret_names = {"DATA_KEY"}

    def verify_token(self):
        self.verified = True

    def resolve_account(self, requested_id=""):
        self.calls.append(("account", requested_id))
        return {"id": requested_id or "account-1", "name": "Test"}

    def resolve_zone(self, account_id, zone_name):
        self.calls.append(("zone", account_id, zone_name))
        return {"id": "zone-1", "name": zone_name}

    def list_zones(self, account_id):
        self.calls.append(("zones", account_id))
        return [
            {
                "id": "zone-1",
                "name": "example.com",
                "status": "active",
            }
        ]

    def ensure_d1(self, account_id, database_name):
        self.calls.append(("d1", account_id, database_name))
        return (
            "11111111-2222-3333-4444-555555555555",
            self.database_created,
        )

    def ensure_kv_namespace(self, account_id, namespace_name):
        self.calls.append(("kv", account_id, namespace_name))
        return "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", True

    def worker_secret_names(self, account_id, worker_name):
        self.calls.append(("secrets", account_id, worker_name))
        return set(self.secret_names)

    def ensure_dns(self, zone_id, hostname, *, replace):
        self.calls.append(("dns", zone_id, hostname, replace))
        return {"id": "dns-1", "proxied": True}, True

    def ensure_worker_route(self, zone_id, pattern, worker_name, *, replace):
        self.calls.append(("route", zone_id, pattern, worker_name, replace))
        return {"id": "route-1", "pattern": pattern, "script": worker_name}, True


class FakeRunner:
    def __init__(self):
        self.calls = []
        self.configs = []
        self.manifests = []
        self.assets_built = False

    def build_assets(self, *, hidden_env_names=()):
        self.assets_built = True
        self.hidden_build_env = tuple(hidden_env_names)

    def build_world_assets(self, *, hidden_env_names=()):
        self.world_assets_built = True
        self.hidden_world_build_env = tuple(hidden_env_names)

    def run(
        self,
        arguments,
        *,
        token,
        account_id,
        stdin_text=None,
        hidden_env_names=(),
    ):
        arguments = list(arguments)
        self.calls.append(
            {
                "arguments": arguments,
                "token": token,
                "account_id": account_id,
                "stdin_text": stdin_text,
                "hidden_env_names": tuple(hidden_env_names),
            }
        )
        config_index = arguments.index("--config") + 1
        config = bootstrap_module.WORKER_DIR / arguments[config_index]
        assert config.exists()
        config_text = config.read_text(encoding="utf-8")
        self.configs.append(config_text)
        if arguments[0] == "deploy":
            match = re.search(r'^directory = "([^"]+)"$', config_text, re.MULTILINE)
            assert match
            manifest = Path(match.group(1)) / bootstrap_module.MIRROR_MANIFEST_ASSET
            if manifest.exists():
                self.manifests.append(json.loads(manifest.read_text(encoding="utf-8")))


def fake_manifest_signer(*, expect_data_hidden=False):
    def run(command, **kwargs):
        request = json.loads(kwargs["input"])
        assert request["publicKey"] == PUBLIC_KEY
        assert request["algorithm"] == "Ed25519"
        assert "CLOUDFLARE_API_TOKEN" not in kwargs["env"]
        if expect_data_hidden:
            assert "DATA_KEY" not in kwargs["env"]
        return SimpleNamespace(
            stdout=json.dumps({"publicKey": PUBLIC_KEY, "signature": SIGNATURE})
        )

    return bootstrap_module.MirrorManifestSigner(["local-signer"], runner=run)


def test_generated_config_targets_new_d1_without_mutating_template():
    template_path = bootstrap_module.WRANGLER_TEMPLATE
    original = template_path.read_text(encoding="utf-8")
    rendered = bootstrap_module.render_wrangler_config(
        original,
        worker_name="alice-forkmesh",
        database_name="alice-forkmesh",
        database_id="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        namespace_id="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        public_base_url="https://mesh.example.com",
        node_name="alice-node",
        relay_label="Alice relay",
        main_relay_url="https://forkmesh.com",
        world_origin="https://forkmesh-world.example.com",
        assets_directory="/tmp/forkmesh-assets",
    )

    assert template_path.read_text(encoding="utf-8") == original
    assert rendered.startswith('name = "alice-forkmesh"')
    assert 'database_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"' in rendered
    assert 'database_name = "alice-forkmesh"' in rendered
    assert 'id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"' in rendered
    assert 'PUBLIC_BASE_URL = "https://mesh.example.com"' in rendered
    assert 'API_ORIGIN = "https://mesh.example.com"' in rendered
    assert 'APP_ORIGIN = "https://mesh.example.com"' in rendered
    assert 'WWW_ORIGIN = "https://mesh.example.com"' in rendered
    assert 'WORLD_ORIGIN = "https://forkmesh-world.example.com"' in rendered
    assert (
        'CORS_ALLOWED_ORIGINS = '
        '"https://mesh.example.com,https://forkmesh-world.example.com"'
    ) in rendered
    assert 'SINGLE_WORKER_SITE = "true"' in rendered
    assert (
        'DISCORD_OAUTH_REDIRECT_URI = '
        '"https://mesh.example.com/api/integrations/discord/callback"'
    ) in rendered
    assert 'MAIN_RELAY_URL = "https://forkmesh.com"' in rendered
    assert 'directory = "/tmp/forkmesh-assets"' in rendered
    assert "tools/build_worker_python.py" in rendered
    assert "tools/build_site_assets.py app" in rendered
    assert "./migrate.sh" not in next(
        line for line in rendered.splitlines() if line.startswith("command =")
    )
    assert "CLOUDFLARE_API_TOKEN" not in rendered
    assert "app.forkmesh.com" not in rendered
    assert "[[routes]]" not in rendered


def test_self_host_asset_stage_is_operational_and_excludes_official_content():
    bootstrap_module.WranglerRunner().build_assets()
    with bootstrap_module.staged_public_assets(None) as staged:
        for relative in (
            "api-client.js",
            "dashboard/index.html",
            "dashboard/repo.html",
            "index.html",
            "install.sh",
            "uninstall.sh",
            "login.html",
            "signup.html",
        ):
            assert (staged / relative).is_file(), relative
        assert not (staged / "docs").exists()
        assert not (staged / "blog/one-app-one-mesh/index.html").exists()


def test_self_host_asset_copy_rewrites_official_app_and_world_links():
    bootstrap_module.WranglerRunner().build_assets()
    with bootstrap_module.staged_public_assets(None) as staged:
        with bootstrap_module.self_host_asset_copy(
            staged,
            "https://forkmesh.example.com",
            "https://forkmesh-world.example.com",
        ) as copied:
            homepage = (copied / "index.html").read_text(encoding="utf-8")
            header = (copied / "site-header.js").read_text(encoding="utf-8")
            installer = (copied / "install.sh").read_text(encoding="utf-8")
            combined = homepage + header + installer
            assert "https://app.forkmesh.com" not in combined
            assert "https://world.forkmesh.com" not in combined
            assert "https://forkmesh.com/install.sh" not in combined
            assert "https://forkmesh.example.com" in combined
            assert "https://forkmesh-world.example.com" in combined


def test_bootstrap_keeps_tokens_out_of_files_and_summary(monkeypatch):
    api = FakeAPI()
    runner = FakeRunner()
    token = "cloudflare-test-token-that-must-not-be-written"
    local_secret = "local-worker-secret-value-that-must-not-enter-config"
    monkeypatch.setenv("DATA_KEY", local_secret)
    before = set(bootstrap_module.WORKER_DIR.glob(".forkmesh-bootstrap-*.toml"))
    output = []

    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="mesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="Alice relay",
            main_relay_url="https://forkmesh.com",
            account_id="account-1",
            secret_env=("DATA_KEY",),
            mirror_public_key=PUBLIC_KEY,
            manifest_signer_command=("unused-when-injected",),
        ),
        token=token,
        api=api,
        runner=runner,
        manifest_signer=fake_manifest_signer(expect_data_hidden=True),
        health_check=lambda hostname: output.append(f"health:{hostname}"),
        readiness_check=lambda hostname, key: output.append(
            f"readiness:{hostname}:{len(key)}"
        ),
        announce=lambda hostname: (
            output.append(f"announce:{hostname}") or "pending"
        ),
        output=output.append,
    )

    assert api.verified
    assert result["ok"] is True
    # The launch-time join ping (adhoc #97) runs only after the instance
    # passed its health check, and its result rides the redacted summary.
    assert output.index("health:mesh.example.com") < output.index(
        "announce:mesh.example.com")
    assert any(message.startswith("readiness:mesh.example.com:") for message in output)
    assert result["joinRequest"] == {"announced": True, "status": "pending"}
    assert result["workerSecretsSet"] == ["DATA_KEY"]
    assert result["repositoryMetadataNamespaceId"] == (
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    )
    assert result["functionalReadinessVerified"] is True
    assert result["mirrorManifest"]["path"] == "/forkmesh-mirror.json"
    assert result["mirrorManifest"]["repositoryBytesInD1"] is False
    assert token not in repr(result)
    assert local_secret not in repr(result)
    assert token not in "\n".join(output)
    assert local_secret not in "\n".join(output)
    assert all(token not in config for config in runner.configs)
    assert all(local_secret not in config for config in runner.configs)
    assert runner.assets_built is True
    assert runner.hidden_build_env == ("DATA_KEY",)
    assert all(call["hidden_env_names"] == ("DATA_KEY",) for call in runner.calls)
    assert runner.manifests[0]["endpoint"]["transport"] == "direct-https"
    assert runner.manifests[0]["endpoint"]["mainProxyMode"] == "masked"
    assert runner.manifests[0]["storage"]["repositoryBytesInD1"] is False
    assert runner.manifests[0]["signature"]["value"] == SIGNATURE
    assert runner.calls[-1]["stdin_text"] == local_secret + "\n"
    assert set(bootstrap_module.WORKER_DIR.glob(".forkmesh-bootstrap-*.toml")) == before
    assert ("dns", "zone-1", "mesh.example.com", False) in api.calls
    assert any(call[0] == "kv" for call in api.calls)
    assert (
        "route",
        "zone-1",
        "mesh.example.com/*",
        "alice-forkmesh",
        False,
    ) in api.calls


def test_dry_run_performs_only_read_only_resolution():
    api = FakeAPI()
    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="mesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="",
            main_relay_url="https://forkmesh.com",
            dry_run=True,
        ),
        token="not-persisted",
        api=api,
        runner=FakeRunner(),
        output=lambda message: None,
    )
    assert result["dryRun"] is True
    assert [call[0] for call in api.calls] == ["account", "zone"]


def test_dry_run_describes_optional_world_without_mutation():
    api = FakeAPI()
    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="forkmesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="",
            main_relay_url="https://app.forkmesh.com",
            dry_run=True,
            deploy_world=True,
        ),
        token="not-persisted",
        api=api,
        runner=FakeRunner(),
        output=lambda message: None,
    )

    assert result["world"] == {
        "enabled": True,
        "hostname": "forkmesh-world.example.com",
        "workerName": "alice-forkmesh-world",
    }
    assert [call[0] for call in api.calls] == ["account", "zone"]


def test_optional_world_deploy_uses_custom_app_origin_and_no_official_routes(
    tmp_path,
):
    api = FakeAPI()
    runner = FakeRunner()
    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="forkmesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="",
            main_relay_url="https://app.forkmesh.com",
            publish_mirror_manifest=False,
            data_key_backup=tmp_path / "forkmesh.data-key",
            deploy_world=True,
        ),
        token="not-persisted",
        api=api,
        runner=runner,
        health_check=lambda hostname: None,
        readiness_check=lambda hostname, key: None,
        world_health_check=lambda hostname: None,
        announce=lambda hostname: "",
        output=lambda message: None,
    )

    assert runner.world_assets_built is True
    app_config = next(
        config for config in runner.configs
        if 'name = "alice-forkmesh"' in config
    )
    assert (
        'CORS_ALLOWED_ORIGINS = '
        '"https://forkmesh.example.com,https://forkmesh-world.example.com"'
    ) in app_config
    world_config = runner.configs[-1]
    assert 'name = "alice-forkmesh-world"' in world_config
    assert 'APP_ORIGIN = "https://forkmesh.example.com"' in world_config
    assert "world.forkmesh.com" not in world_config
    assert "[[routes]]" not in world_config
    assert (
        "route",
        "zone-1",
        "forkmesh-world.example.com/*",
        "alice-forkmesh-world",
        False,
    ) in api.calls
    assert result["world"]["enabled"] is True
    assert result["world"]["healthVerified"] is True


def test_new_instance_generates_recovery_copy_and_verifies_data_key(tmp_path):
    api = FakeAPI()
    runner = FakeRunner()
    readiness = []
    recovery_file = tmp_path / "secrets" / "forkmesh.data-key"
    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="forkmesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="Alice relay",
            main_relay_url="https://app.forkmesh.com",
            publish_mirror_manifest=False,
            data_key_backup=recovery_file,
        ),
        token="not-persisted",
        api=api,
        runner=runner,
        health_check=lambda hostname: None,
        readiness_check=lambda hostname, key: readiness.append((hostname, key)),
        announce=lambda hostname: "",
        output=lambda message: None,
    )

    data_key_calls = [
        call for call in runner.calls
        if call["arguments"][0:3] == ["secret", "put", "--config"]
        and call["arguments"][-1] == "DATA_KEY"
    ]
    assert len(data_key_calls) == 1
    generated = data_key_calls[0]["stdin_text"].strip()
    assert len(generated) >= 40
    assert readiness == [("forkmesh.example.com", generated)]
    assert result["dataKeyGenerated"] is True
    assert result["dataKeyBackupCreated"] is True
    assert result["dataKeyRecoveryAvailable"] is True
    assert result["functionalReadinessVerified"] is True
    assert generated not in repr(result)
    assert recovery_file.read_text(encoding="utf-8").strip() == generated
    assert recovery_file.stat().st_mode & 0o777 == 0o600


def test_existing_remote_data_key_requires_local_recovery_copy():
    api = FakeAPI()
    api.database_created = False
    with pytest.raises(
        bootstrap_module.BootstrapError,
        match="no local recovery copy",
    ):
        bootstrap_module.bootstrap(
            bootstrap_module.BootstrapOptions(
                hostname="forkmesh.example.com",
                zone_name="example.com",
                worker_name="alice-forkmesh",
                database_name="alice-forkmesh",
                node_name="alice-node",
                relay_label="Alice relay",
                main_relay_url="https://app.forkmesh.com",
                publish_mirror_manifest=False,
            ),
            token="not-persisted",
            api=api,
            runner=FakeRunner(),
            output=lambda message: None,
        )


def test_existing_remote_data_key_is_proved_without_replacing_it(tmp_path):
    api = FakeAPI()
    api.database_created = False
    runner = FakeRunner()
    recovery_file = tmp_path / "forkmesh.data-key"
    recovery_file.write_text("operator-recovery-value\n", encoding="utf-8")
    recovery_file.chmod(0o600)
    readiness = []

    result = bootstrap_module.bootstrap(
        bootstrap_module.BootstrapOptions(
            hostname="forkmesh.example.com",
            zone_name="example.com",
            worker_name="alice-forkmesh",
            database_name="alice-forkmesh",
            node_name="alice-node",
            relay_label="Alice relay",
            main_relay_url="https://app.forkmesh.com",
            publish_mirror_manifest=False,
            data_key_backup=recovery_file,
        ),
        token="not-persisted",
        api=api,
        runner=runner,
        health_check=lambda hostname: None,
        readiness_check=lambda hostname, key: readiness.append((hostname, key)),
        announce=lambda hostname: "",
        output=lambda message: None,
    )

    assert readiness == [("forkmesh.example.com", "operator-recovery-value")]
    assert result["dataKeyGenerated"] is False
    assert result["dataKeyBackupCreated"] is False
    assert result["functionalReadinessVerified"] is True
    assert not any(
        call["arguments"][0:2] == ["secret", "put"]
        and call["arguments"][-1] == "DATA_KEY"
        for call in runner.calls
    )


def test_cli_without_local_mirror_identity_uses_safe_relay_only_mode(monkeypatch):
    parser = bootstrap_module.build_parser()
    args = parser.parse_args(["--auto-configure"])
    assert not args.skip_mirror_manifest
    assert args.mirror_public_key == ""
    assert args.manifest_signer_command == ""


def test_token_only_discovery_is_read_only_and_derives_distinct_topology():
    api = FakeAPI()
    result = bootstrap_module.discover_deployment_defaults(
        token="session-only-token",
        api=api,
    )

    assert api.verified is True
    assert result == {
        "accountId": "account-1",
        "zoneId": "zone-1",
        "zoneName": "example.com",
        "hostname": "forkmesh.example.com",
        "directMirrorHostname": "mirror.example.com",
        "nodeName": bootstrap_module._derived_resource_name(
            "forkmesh.example.com"
        ),
    }
    assert [call[0] for call in api.calls] == ["account", "zones"]


def test_token_only_discovery_fails_closed_for_multiple_zones():
    api = FakeAPI()
    api.list_zones = lambda account_id: [
        {"id": "zone-1", "name": "one.example", "status": "active"},
        {"id": "zone-2", "name": "two.example", "status": "active"},
    ]
    with pytest.raises(
        bootstrap_module.BootstrapError,
        match="multiple or no active zones",
    ):
        bootstrap_module.discover_deployment_defaults(
            token="session-only-token",
            api=api,
        )


def test_auto_configure_parser_does_not_require_hostname_or_zone():
    args = bootstrap_module.build_parser().parse_args(
        ["--auto-configure", "--json-stdout"]
    )
    assert args.hostname == ""
    assert args.zone_name == ""
    assert args.auto_configure is True
    assert args.json_stdout is True


@pytest.mark.parametrize(
    "hostname",
    ["https://example.com", "*.example.com", "example.com/path", "-bad.example.com"],
)
def test_hostname_validation_rejects_urls_wildcards_paths_and_bad_labels(hostname):
    with pytest.raises(bootstrap_module.BootstrapError):
        bootstrap_module.normalize_hostname(hostname)


def test_secret_env_cannot_turn_cloudflare_token_into_worker_secret():
    with pytest.raises(bootstrap_module.BootstrapError):
        bootstrap_module._validate_secret_names(["CLOUDFLARE_API_TOKEN"])


def test_mirror_manifest_signer_covers_public_key_urls_and_storage_claim():
    unsigned = bootstrap_module.unsigned_mirror_manifest(
        hostname="mesh.example.com",
        node_name="alice-node",
        public_key=PUBLIC_KEY,
        generated_at="2026-07-23T10:00:00Z",
    )
    signed = fake_manifest_signer().sign(unsigned)
    payload = bootstrap_module.mirror_manifest_payload(signed)

    assert signed["node"]["publicKey"] == PUBLIC_KEY
    assert signed["endpoint"]["healthUrl"] == "https://mesh.example.com/health"
    assert (
        signed["endpoint"]["repositoryUrlTemplate"]
        == "https://mesh.example.com/{owner}/{repository}"
    )
    assert signed["dns"] == {
        "recordName": "mesh.example.com",
        "proxied": True,
        "workerRoute": "mesh.example.com/*",
    }
    assert signed["storage"]["repositoryBytesInD1"] is False
    assert b'"signature"' not in payload
    assert signed["signature"]["payloadSha256"] == __import__(
        "hashlib"
    ).sha256(payload).hexdigest()


def test_published_mirror_manifest_schema_is_strict_about_d1_and_https():
    schema = json.loads(
        (ROOT / "www" / "docs" / "mirror-endpoint.schema.json").read_text(encoding="utf-8")
    )
    storage = schema["properties"]["storage"]["properties"]
    endpoint = schema["properties"]["endpoint"]["properties"]
    assert storage["repositoryBytesInD1"]["const"] is False
    assert endpoint["transport"]["const"] == "direct-https"
    assert endpoint["mainProxyMode"]["const"] == "masked"


def test_bootstrap_docs_include_kv_data_key_and_app_origin_requirements():
    docs = (
        ROOT / "www/docs/operations/automation-and-cloudflare-bootstrap.md"
    ).read_text(encoding="utf-8")
    assert "Workers KV Storage edit" in docs
    assert "generates `DATA_KEY`" in docs
    assert "--data-key-backup" in docs
    assert "mode-`0600` recovery copy" in docs
    assert "authenticated database/encryption probe" in docs
    assert "--main-relay-url https://app.forkmesh.com" in docs
