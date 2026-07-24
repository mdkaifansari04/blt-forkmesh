#!/usr/bin/env python3
"""Fixed-input SSH push-to-mirror publication orchestration."""

import base64
from io import BytesIO
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
from urllib.parse import parse_qs, urlsplit

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


PROJECT_ROOT = Path(__file__).resolve().parents[2]


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


bridge = _module(
    PROJECT_ROOT / "tools" / "ssh_post_receive_refresh.py",
    "forkmesh_ssh_post_receive_refresh",
)


def _config(tmp_path):
    return bridge.BridgeConfig(
        trigger_path=tmp_path / "pending",
        notify_user="git",
        mirror_user="forkmesh-mirror",
        python_program=Path("/usr/bin/python3.13"),
        refresh_program=Path("/opt/forkmesh-mirror/headless_mirror_refresh.py"),
        refresh_config_path=Path(
            "/var/lib/forkmesh-mirror/gateway/mirror-refresh.json"
        ),
        gateway_config_path=tmp_path / "mirror-gateway.json",
        mirror_service="forkmesh-mirror.service",
        health_timeout_seconds=10,
    )


def _b64url(value):
    return base64.urlsafe_b64encode(value).decode().rstrip("=")


def test_notify_discards_ref_input_and_creates_only_fixed_marker(tmp_path, monkeypatch):
    config = _config(tmp_path)
    monkeypatch.setattr(
        bridge.pwd,
        "getpwuid",
        lambda _uid: SimpleNamespace(pw_name="git"),
    )
    monkeypatch.setattr(bridge, "_require_trigger_directory", lambda _config: tmp_path)
    bridge.notify(
        config,
        input_stream=BytesIO(
            b"0" * 40 + b" " + b"1" * 40 + b" refs/heads/main;touch-pwned\n"
        ),
    )
    assert config.trigger_path.read_bytes() == b"forkmesh-refresh-v1\n"
    assert sorted(path.name for path in tmp_path.iterdir()) == ["pending"]


def test_run_uses_only_fixed_refresh_restart_health_register_order(
    tmp_path, monkeypatch
):
    config = _config(tmp_path)
    config.trigger_path.write_text("forkmesh-refresh-v1\n", encoding="ascii")
    monkeypatch.setattr(bridge.os, "geteuid", lambda: 0)
    events = []

    def runner(command, **_kwargs):
        events.append(command)
        return SimpleNamespace(returncode=0)

    result = bridge.run(
        config,
        runner=runner,
        health_waiter=lambda _config: events.append(["signed-health"]),
    )
    assert result["ok"] is True
    assert events[0][-1] == "refresh"
    assert events[1] == [
        "/usr/bin/systemctl",
        "restart",
        "forkmesh-mirror.service",
    ]
    assert events[2] == ["signed-health"]
    assert events[3][-1] == "register"
    assert not config.trigger_path.exists()


def test_signed_health_binds_nonce_node_key_and_signature(tmp_path):
    private = Ed25519PrivateKey.generate()
    public = private.public_key().public_bytes_raw()
    config = _config(tmp_path)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": _b64url(public)},
                "listen": {"host": "127.0.0.1", "port": 8790},
            }
        ),
        encoding="utf-8",
    )
    config.gateway_config_path.chmod(0o600)

    class Response:
        status = 200

        def __init__(self, request):
            query = parse_qs(urlsplit(request.full_url).query)
            self.nonce = query["nonce"][0]
            self.issued = int(query["issuedAt"][0])

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return False

        def read(self, _maximum):
            message = (
                f"forkmesh-https-health-v1\nmirror2\n{self.nonce}\n{self.issued}"
            ).encode()
            import hashlib

            return json.dumps(
                {
                    "ok": True,
                    "integrity": "ok",
                    "node": "mirror2",
                    "publicKey": _b64url(public),
                    "publicRepositoryCount": 2,
                    "challenge": {
                        "messageType": "forkmesh-https-health-v1",
                        "nonce": self.nonce,
                        "issuedAt": self.issued,
                        "algorithm": "Ed25519",
                        "encoding": "base64url-no-padding",
                        "messageSha256": hashlib.sha256(message).hexdigest(),
                        "signature": _b64url(private.sign(message)),
                    },
                }
            ).encode()

    class Opener:
        def open(self, request, timeout):
            assert timeout == 3
            return Response(request)

    bridge.wait_for_signed_health(
        config,
        clock_ms=lambda: 1234567890,
        sleeper=lambda _seconds: None,
        opener=Opener(),
    )


def test_refresh_packaging_has_no_user_derived_commands():
    service = (
        PROJECT_ROOT / "packaging" / "systemd" / "forkmesh-mirror-refresh.service"
    ).read_text(encoding="utf-8")
    path_unit = (
        PROJECT_ROOT / "packaging" / "systemd" / "forkmesh-mirror-refresh.path"
    ).read_text(encoding="utf-8")
    notifier = (PROJECT_ROOT / "packaging" / "ssh" / "ssh-refresh-notify").read_text(
        encoding="utf-8"
    )
    assert "PathExists=/run/forkmesh-mirror-refresh/pending" in path_unit
    assert "forkmesh-mirror.service" not in notifier
    assert "$@" not in notifier
    assert "SSH_ORIGINAL_COMMAND" not in notifier
    assert "ExecStart=/usr/bin/python3 -I " in service
