"""Security and lifecycle tests for the headless mirror refresh orchestrator."""

from __future__ import annotations

import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)
import pytest


ROOT = Path(__file__).resolve().parents[2]
REFRESH_PATH = ROOT / "tools" / "headless_mirror_refresh.py"
HELPER_PATH = ROOT / "tools" / "headless_mirror_identity.py"
GATEWAY_PATH = ROOT / "tools" / "mirror_gateway.py"
TUNNEL_PATH = ROOT / "tools" / "cloudflare_tunnel_bootstrap.py"


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


refresh_tool = _load("headless_mirror_refresh_test", REFRESH_PATH)
identity_helper = _load("headless_mirror_refresh_identity_test", HELPER_PATH)
tunnel_helper = _load("headless_mirror_refresh_tunnel_test", TUNNEL_PATH)


def _run(command: list[str], cwd: Path | None = None) -> str:
    return subprocess.run(
        command,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
        env={
            "PATH": os.environ.get("PATH", ""),
            "LANG": "C",
            "LC_ALL": "C",
        },
    ).stdout.strip()


def _b64url(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).decode("ascii").rstrip("=")


@pytest.fixture()
def installation(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    tmp_path.chmod(0o700)
    programs = tmp_path / "programs"
    programs.mkdir(mode=0o700)
    recipient = "age1" + "q" * 58
    age_secret = "AGE-SECRET-KEY-1" + "Q" * 58
    keygen = programs / "age-keygen"
    keygen.write_text(
        f"""#!{sys.executable}
import pathlib
import sys
if "-o" in sys.argv:
    target = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])
    target.write_text("# test identity\\n{age_secret}\\n")
    raise SystemExit(0)
if "-y" in sys.argv:
    print("{recipient}")
    raise SystemExit(0)
raise SystemExit(2)
""",
        encoding="utf-8",
    )
    keygen.chmod(0o755)
    age = programs / "age"
    age.write_text(
        f"""#!{sys.executable}
import pathlib
import sys
HEADER = b"age-encryption.org/v1\\n"
if "--encrypt" in sys.argv:
    target = pathlib.Path(sys.argv[sys.argv.index("--output") + 1])
    target.write_bytes(HEADER + sys.stdin.buffer.read())
    raise SystemExit(0)
if "--decrypt" in sys.argv:
    raw = pathlib.Path(sys.argv[-1]).read_bytes()
    if not raw.startswith(HEADER):
        raise SystemExit(2)
    sys.stdout.buffer.write(raw[len(HEADER):])
    raise SystemExit(0)
raise SystemExit(2)
""",
        encoding="utf-8",
    )
    age.chmod(0o755)
    installed_helper = programs / "headless_mirror_identity.py"
    installed_gateway = programs / "mirror_gateway.py"
    shutil.copyfile(HELPER_PATH, installed_helper)
    shutil.copyfile(GATEWAY_PATH, installed_gateway)
    installed_helper.chmod(0o644)
    installed_gateway.chmod(0o644)
    monkeypatch.setenv(
        "PATH", str(programs) + os.pathsep + os.environ.get("PATH", "")
    )

    work = tmp_path / "work"
    bare = tmp_path / "source.git"
    work.mkdir(mode=0o700)
    _run(["git", "init", "-b", "main"], work)
    _run(["git", "config", "user.name", "ForkMesh test"], work)
    _run(["git", "config", "user.email", "test@example.invalid"], work)
    (work / "README.md").write_text("headless refresh\n", encoding="utf-8")
    _run(["git", "add", "README.md"], work)
    _run(["git", "commit", "-m", "initial"], work)
    _run(["git", "init", "--bare", str(bare)])
    _run(["git", "remote", "add", "mirror", str(bare)], work)
    _run(["git", "push", "mirror", "main"], work)
    _run(["git", "--git-dir", str(bare), "symbolic-ref", "HEAD", "refs/heads/main"])

    state = tmp_path / "identity"
    archive = tmp_path / "archives"
    gateway_state = tmp_path / "gateway-state"
    archive.mkdir(mode=0o700)
    gateway_state.mkdir(mode=0o700)
    router_key = Ed25519PrivateKey.generate()
    from cryptography.hazmat.primitives import serialization

    router_public = _b64url(
        router_key.public_key().public_bytes(
            serialization.Encoding.Raw,
            serialization.PublicFormat.Raw,
        )
    )
    public = identity_helper.initialize_identity(
        state,
        {
            "schemaVersion": 1,
            "type": "forkmesh.headless-mirror-identity-init",
            "nodeName": "mirror-two",
            "routerPublicKey": router_public,
            "allowedOrigins": ["https://mirror-two.example.invalid"],
        },
        age_keygen_program=str(keygen),
    )
    unsigned = tunnel_helper.unsigned_tunnel_manifest(
        hostname="mirror-two.example.invalid",
        node_name="mirror-two",
        public_key=public["nodePublicKey"],
        tunnel_id="11111111-2222-3333-4444-555555555555",
        generated_at="2026-07-23T20:00:00Z",
    )
    payload = identity_helper._canonical_json(unsigned)
    signed = identity_helper.sign_mirror_manifest(
        state,
        {
            "schemaVersion": 1,
            "type": "forkmesh.mirror-endpoint-signing-request",
            "algorithm": "Ed25519",
            "encoding": "base64url-no-padding",
            "canonicalization": "forkmesh-json-sort-v1",
            "publicKey": public["nodePublicKey"],
            "payloadBase64": _b64url(payload),
            "payloadSha256": hashlib.sha256(payload).hexdigest(),
        },
    )
    manifest = dict(unsigned)
    manifest["signature"] = {
        "algorithm": "Ed25519",
        "encoding": "base64url-no-padding",
        "canonicalization": "forkmesh-json-sort-v1",
        "payloadSha256": hashlib.sha256(payload).hexdigest(),
        "value": signed["signature"],
    }
    manifest_path = gateway_state / "forkmesh-mirror.json"
    manifest_path.write_text(
        json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    manifest_path.chmod(0o600)

    python_program = Path(sys.executable).resolve()
    git_program = Path(shutil.which("git") or "/usr/bin/git").resolve()
    config_value = {
        "schemaVersion": 1,
        "type": "forkmesh.headless-mirror-refresh",
        "sourceRepository": str(bare),
        "archiveDirectory": str(archive),
        "gatewayConfigPath": str(gateway_state / "mirror-gateway.json"),
        "identityStateDirectory": str(state),
        "identityHelperPath": str(installed_helper),
        "mirrorGatewayPath": str(installed_gateway),
        "pythonProgram": str(python_program),
        "gitProgram": str(git_program),
        "manifestPath": str(manifest_path),
        "nodeOwner": "mirror-two",
        "repositoryName": "forkmesh",
        "ownerAliases": ["mirror-two", "forkmesh"],
        "workerOrigin": "https://worker.example.invalid",
        "publicOrigin": "https://mirror-two.example.invalid",
        "listen": {"host": "127.0.0.1", "port": 8790},
        "operations": [
            "git-info-refs",
            "git-upload-pack",
            "tree",
            "blob",
            "raw",
            "branches",
        ],
        "catalog": {
            "description": "A living code city",
            "branch": "main",
            "platform": "git",
        },
    }
    config_path = tmp_path / "refresh.json"
    config_path.write_text(
        json.dumps(config_value, sort_keys=True, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )
    config_path.chmod(0o600)
    return {
        "config_path": config_path,
        "config_value": config_value,
        "config": refresh_tool.load_config(config_path),
        "work": work,
        "bare": bare,
        "archive": archive,
        "gateway_state": gateway_state,
        "gateway_config": gateway_state / "mirror-gateway.json",
        "public": public,
        "age_secret": age_secret,
    }


def test_refresh_renders_one_archive_for_all_aliases_and_check_is_dry(
    installation,
    monkeypatch: pytest.MonkeyPatch,
):
    config = installation["config"]
    result = refresh_tool.refresh(config)
    assert result == {
        "ok": True,
        "event": "refresh_complete",
        "aliasCount": 2,
    }
    gateway_path = installation["gateway_config"]
    assert stat.S_IMODE(gateway_path.stat().st_mode) == 0o600
    rendered = json.loads(gateway_path.read_text(encoding="utf-8"))
    repositories = rendered["repositories"]
    assert [item["owner"] for item in repositories] == [
        "mirror-two",
        "forkmesh",
    ]
    assert {item["name"] for item in repositories} == {"forkmesh"}
    archives = {
        json.dumps(item["encryptedArchive"], sort_keys=True)
        for item in repositories
    }
    refs = {
        item["integrity"]["expectedRefsSha256"] for item in repositories
    }
    assert len(archives) == 1
    assert len(refs) == 1
    archive_path = Path(repositories[0]["encryptedArchive"]["ciphertextPath"])
    assert archive_path.parent == installation["archive"]
    assert archive_path.name.startswith("archive-")
    assert stat.S_IMODE(archive_path.stat().st_mode) == 0o600

    before_config = gateway_path.read_bytes()
    before_archives = {
        item.name: hashlib.sha256(item.read_bytes()).hexdigest()
        for item in installation["archive"].iterdir()
    }
    lock_open_modes = []
    real_open = refresh_tool.os.open

    def tracked_open(path, flags, *args, **kwargs):
        if Path(path).name == ".refresh.lock":
            lock_open_modes.append(flags & os.O_ACCMODE)
        return real_open(path, flags, *args, **kwargs)

    monkeypatch.setattr(refresh_tool.os, "open", tracked_open)
    checked = refresh_tool.check(config)
    assert checked == {
        "ok": True,
        "event": "check_complete",
        "aliasCount": 2,
        "networkRequests": 0,
    }
    assert gateway_path.read_bytes() == before_config
    assert {
        item.name: hashlib.sha256(item.read_bytes()).hexdigest()
        for item in installation["archive"].iterdir()
    } == before_archives
    assert lock_open_modes
    assert set(lock_open_modes) == {os.O_RDONLY}
    combined = json.dumps(result) + json.dumps(checked)
    assert "mirror-two" not in combined
    assert "forkmesh" not in combined
    assert str(installation["bare"]) not in combined
    assert installation["age_secret"] not in gateway_path.read_text()


def test_check_never_creates_a_missing_refresh_lock(installation):
    lock = installation["config"].identity_state_directory / ".refresh.lock"
    assert not lock.exists()
    with pytest.raises(refresh_tool.RefreshError, match="lock"):
        refresh_tool.check(installation["config"])
    assert not lock.exists()


def test_failed_staged_gateway_validation_preserves_last_good_pair(
    installation,
    monkeypatch: pytest.MonkeyPatch,
):
    config = installation["config"]
    refresh_tool.refresh(config)
    old_config = installation["gateway_config"].read_bytes()
    old_rendered = json.loads(old_config)
    old_archive = Path(
        old_rendered["repositories"][0]["encryptedArchive"]["ciphertextPath"]
    )
    old_ciphertext = old_archive.read_bytes()

    (installation["work"] / "README.md").write_text(
        "headless refresh changed\n", encoding="utf-8"
    )
    _run(["git", "add", "README.md"], installation["work"])
    _run(["git", "commit", "-m", "change"], installation["work"])
    _run(["git", "push", "mirror", "main"], installation["work"])

    def reject_staged(_config, _path):
        raise refresh_tool.RefreshError("gateway validation rejected the active mirror")

    monkeypatch.setattr(refresh_tool, "_invoke_gateway_check", reject_staged)
    with pytest.raises(refresh_tool.RefreshError, match="gateway validation"):
        refresh_tool.refresh(config)
    assert installation["gateway_config"].read_bytes() == old_config
    assert old_archive.read_bytes() == old_ciphertext


def test_register_posts_endpoint_then_node_owner_catalog(installation):
    config = installation["config"]
    refresh_tool.refresh(config)
    calls: list[tuple[str, dict]] = []

    def post(url: str, payload):
        calls.append((url, dict(payload)))
        if url.endswith("/api/mirrors/https"):
            return 201, {
                "ok": True,
                "node": payload["node"],
                "baseUrl": payload["baseUrl"],
            }
        signed_record = dict(payload)
        catalog_signature = signed_record.pop("catalogSig")
        assert signed_record.pop("catalogSigVersion") == 2
        assert signed_record.pop("signature") == ""
        record_hash = hashlib.sha256(
            json.dumps(
                signed_record,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=True,
            ).encode("utf-8")
        ).hexdigest()
        Ed25519PublicKey.from_public_bytes(
            base64.urlsafe_b64decode(
                installation["public"]["nodePublicKey"] + "="
            )
        ).verify(
            base64.urlsafe_b64decode(catalog_signature + "=="),
            ("forkmesh-catalog-v2\n" + record_hash).encode("ascii"),
        )
        return 201, {
            "ok": True,
            "repository": {
                "owner": payload["owner"],
                "name": payload["name"],
                "stateHash": payload["stateHash"],
            },
        }

    result = refresh_tool.register(config, post_json=post)
    assert [url.rsplit("/", 1)[-1] for url, _ in calls] == [
        "https",
        "repositories",
    ]
    endpoint = calls[0][1]
    assert endpoint["node"] == "mirror-two"
    assert endpoint["baseUrl"] == "https://mirror-two.example.invalid"
    assert endpoint["publicKey"] == installation["public"]["nodePublicKey"]
    assert re_fullmatch_base64_signature(endpoint["signature"])

    catalog = calls[1][1]
    expected_commit = _run(
        ["git", "--git-dir", str(installation["bare"]), "rev-parse", "main"]
    )
    assert catalog["owner"] == "mirror-two"
    assert catalog["name"] == "forkmesh"
    assert catalog["visibility"] == "public"
    assert catalog["commit"] == expected_commit
    assert catalog["catalogSigVersion"] == 2
    assert catalog["maintainer"] == installation["public"]["nodePublicKey"]
    assert re_fullmatch_base64_signature(catalog["catalogSig"])
    assert re_fullmatch_base64_signature(catalog["stateSig"])
    assert result == {
        "ok": True,
        "event": "registration_complete",
        "aliasCount": 2,
    }


def re_fullmatch_base64_signature(value: str) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 86
        and len(base64.urlsafe_b64decode(value + "==")) == 64
    )


def test_register_stops_before_catalog_when_endpoint_is_rejected(installation):
    config = installation["config"]
    refresh_tool.refresh(config)
    calls = []

    def reject(url: str, payload):
        calls.append((url, payload))
        return 401, {"ok": False}

    with pytest.raises(refresh_tool.RefreshError, match="endpoint registration"):
        refresh_tool.register(config, post_json=reject)
    assert len(calls) == 1
    assert calls[0][0].endswith("/api/mirrors/https")


def test_config_rejects_permissions_symlinks_and_secret_fields(
    installation,
    tmp_path: Path,
):
    config_path = installation["config_path"]
    config_path.chmod(0o640)
    with pytest.raises(refresh_tool.RefreshError, match="unsafe"):
        refresh_tool.load_config(config_path)
    config_path.chmod(0o600)

    link = tmp_path / "refresh-link.json"
    link.symlink_to(config_path)
    with pytest.raises(refresh_tool.RefreshError, match="symbolic link"):
        refresh_tool.load_config(link)

    prohibited = dict(installation["config_value"])
    prohibited["apiToken"] = "must-not-be-accepted"
    prohibited_path = tmp_path / "prohibited.json"
    prohibited_path.write_text(json.dumps(prohibited), encoding="utf-8")
    prohibited_path.chmod(0o600)
    with pytest.raises(refresh_tool.RefreshError, match="prohibited secret"):
        refresh_tool.load_config(prohibited_path)


def test_git_fsck_failure_does_not_create_active_state_or_leak_logs(
    installation,
    capsys: pytest.CaptureFixture[str],
):
    corrupt = installation["bare"] / "objects" / "aa" / ("a" * 38)
    corrupt.parent.mkdir(exist_ok=True)
    corrupt.write_bytes(b"not a git object")
    result = refresh_tool.main(
        ["--config", str(installation["config_path"]), "refresh"]
    )
    captured = capsys.readouterr()
    assert result == 2
    assert captured.out == ""
    assert "required local operation failed" in captured.err
    assert str(installation["bare"]) not in captured.err
    assert "mirror-two" not in captured.err
    assert "forkmesh.git" not in captured.err
    assert installation["age_secret"] not in captured.err
    assert not installation["gateway_config"].exists()
    assert not list(installation["archive"].glob("archive-*.age"))
