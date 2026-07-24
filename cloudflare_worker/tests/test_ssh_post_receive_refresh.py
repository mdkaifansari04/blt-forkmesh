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

import pytest
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


def _config(tmp_path, *, health_timeout_seconds=10):
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
        health_timeout_seconds=health_timeout_seconds,
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


def test_signed_health_binds_both_origins_to_nonce_node_key_and_signature(tmp_path):
    private = Ed25519PrivateKey.generate()
    public = private.public_key().public_bytes_raw()
    config = _config(tmp_path)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": _b64url(public)},
                "publicOrigin": "https://mirror2.forkmesh.com",
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
        origins = []

        def open(self, request, timeout):
            assert timeout == 3
            self.origins.append(
                f"{urlsplit(request.full_url).scheme}://"
                f"{urlsplit(request.full_url).netloc}"
            )
            return Response(request)

    opener = Opener()
    bridge.wait_for_signed_health(
        config,
        clock_ms=lambda: 1234567890,
        sleeper=lambda _seconds: None,
        opener=opener,
    )
    assert opener.origins == [
        "http://127.0.0.1:8790",
        "https://mirror2.forkmesh.com",
    ]


def test_signed_health_retries_loopback_then_public_until_tunnel_ready(
    tmp_path, monkeypatch
):
    private = Ed25519PrivateKey.generate()
    public = _b64url(private.public_key().public_bytes_raw())
    config = _config(tmp_path)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": public},
                "publicOrigin": "https://mirror2.forkmesh.com",
                "listen": {"host": "127.0.0.1", "port": 8790},
            }
        ),
        encoding="utf-8",
    )
    config.gateway_config_path.chmod(0o600)
    now = [0.0]
    calls = []
    public_attempts = [0]

    def signed_once(
        node,
        public_key,
        origin,
        *,
        opener,
        clock_ms,
        timeout_seconds,
    ):
        assert node == "mirror2"
        assert public_key == public
        assert opener is marker_opener
        assert clock_ms() == 1234
        assert 0 < timeout_seconds <= 3
        calls.append(origin)
        if origin.startswith("http://"):
            return True
        public_attempts[0] += 1
        return public_attempts[0] == 3

    def sleep(seconds):
        now[0] += seconds

    marker_opener = object()
    monkeypatch.setattr(bridge, "_signed_health_once", signed_once)
    bridge.wait_for_signed_health(
        config,
        clock_ms=lambda: 1234,
        monotonic=lambda: now[0],
        sleeper=sleep,
        opener=marker_opener,
    )
    assert calls == [
        "http://127.0.0.1:8790",
        "https://mirror2.forkmesh.com",
        "http://127.0.0.1:8790",
        "https://mirror2.forkmesh.com",
        "http://127.0.0.1:8790",
        "https://mirror2.forkmesh.com",
    ]
    assert now[0] == 1.0


def test_signed_health_uses_one_bounded_deadline_and_fails_closed(
    tmp_path, monkeypatch
):
    private = Ed25519PrivateKey.generate()
    public = _b64url(private.public_key().public_bytes_raw())
    config = _config(tmp_path, health_timeout_seconds=5)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": public},
                "publicOrigin": "https://mirror2.forkmesh.com",
                "listen": {"host": "127.0.0.1", "port": 8790},
            }
        ),
        encoding="utf-8",
    )
    config.gateway_config_path.chmod(0o600)
    now = [10.0]
    calls = []

    def signed_once(
        _node,
        _public_key,
        origin,
        *,
        opener,
        clock_ms,
        timeout_seconds,
    ):
        del opener, clock_ms
        calls.append((origin, timeout_seconds))
        now[0] += timeout_seconds
        return origin.startswith("http://")

    monkeypatch.setattr(bridge, "_signed_health_once", signed_once)
    with pytest.raises(
        bridge.RefreshBridgeError,
        match="signed gateway health did not become ready",
    ):
        bridge.wait_for_signed_health(
            config,
            monotonic=lambda: now[0],
            sleeper=lambda seconds: now.__setitem__(0, now[0] + seconds),
            opener=object(),
        )
    assert now[0] == 15.0
    assert calls == [
        ("http://127.0.0.1:8790", 3.0),
        ("https://mirror2.forkmesh.com", 2.0),
    ]


@pytest.mark.parametrize(
    "public_origin",
    [
        "http://mirror2.forkmesh.com",
        "https://user@mirror2.forkmesh.com",
        "https://mirror2.forkmesh.com/health",
        "https://mirror2.forkmesh.com?redirect=1",
        "https://localhost",
        "https://127.0.0.1",
    ],
)
def test_gateway_public_origin_is_strict_and_fail_closed(
    tmp_path, public_origin
):
    public = _b64url(Ed25519PrivateKey.generate().public_key().public_bytes_raw())
    config = _config(tmp_path)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": public},
                "publicOrigin": public_origin,
                "listen": {"host": "127.0.0.1", "port": 8790},
            }
        ),
        encoding="utf-8",
    )
    config.gateway_config_path.chmod(0o600)
    with pytest.raises(
        bridge.RefreshBridgeError,
        match="gateway public origin is invalid",
    ):
        bridge._parse_gateway_identity(config.gateway_config_path)


def test_direct_health_transport_disables_proxies_and_redirects(monkeypatch):
    handlers = []
    marker_opener = object()

    def build_opener(*values):
        handlers.extend(values)
        return marker_opener

    monkeypatch.setattr(bridge, "build_opener", build_opener)
    assert bridge._direct_opener() is marker_opener
    assert len(handlers) == 2
    assert isinstance(handlers[0], bridge.ProxyHandler)
    assert handlers[0].proxies == {}
    assert isinstance(handlers[1], bridge._NoRedirectHandler)
    assert (
        handlers[1].redirect_request(
            object(), None, 302, "found", {}, "https://other.invalid/health"
        )
        is None
    )


def test_invalid_public_health_signature_never_becomes_ready(tmp_path):
    private = Ed25519PrivateKey.generate()
    wrong_private = Ed25519PrivateKey.generate()
    public = private.public_key().public_bytes_raw()
    config = _config(tmp_path, health_timeout_seconds=5)
    config.gateway_config_path.write_text(
        json.dumps(
            {
                "node": {"name": "mirror2", "publicKey": _b64url(public)},
                "publicOrigin": "https://mirror2.forkmesh.com",
                "listen": {"host": "127.0.0.1", "port": 8790},
            }
        ),
        encoding="utf-8",
    )
    config.gateway_config_path.chmod(0o600)

    class Response:
        status = 200

        def __init__(self, request, *, signer):
            query = parse_qs(urlsplit(request.full_url).query)
            self.nonce = query["nonce"][0]
            self.issued = int(query["issuedAt"][0])
            self.signer = signer

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            return False

        def read(self, _maximum):
            message = (
                f"forkmesh-https-health-v1\nmirror2\n{self.nonce}\n{self.issued}"
            ).encode()
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
                        "messageSha256": bridge.hashlib.sha256(message).hexdigest(),
                        "signature": _b64url(self.signer.sign(message)),
                    },
                }
            ).encode()

    class Opener:
        def open(self, request, timeout):
            del timeout
            signer = (
                private
                if urlsplit(request.full_url).scheme == "http"
                else wrong_private
            )
            return Response(request, signer=signer)

    now = [0.0]
    with pytest.raises(
        bridge.RefreshBridgeError,
        match="signed gateway health did not become ready",
    ):
        bridge.wait_for_signed_health(
            config,
            monotonic=lambda: now[0],
            sleeper=lambda seconds: now.__setitem__(0, now[0] + seconds),
            opener=Opener(),
        )
    assert now[0] == 5.0


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


def test_refresh_health_timeout_default_and_example_allow_tunnel_startup():
    example = json.loads(
        (
            PROJECT_ROOT / "packaging" / "ssh" / "ssh-refresh.json.example"
        ).read_text(encoding="utf-8")
    )
    schema = json.loads(
        (
            PROJECT_ROOT / "docs" / "ssh-post-receive-refresh.schema.json"
        ).read_text(encoding="utf-8")
    )
    assert bridge.DEFAULT_HEALTH_TIMEOUT_SECONDS == 180
    assert example["healthTimeoutSeconds"] == 180
    assert schema["properties"]["healthTimeoutSeconds"]["default"] == 180


def test_tunnel_restarts_with_the_mirror_gateway():
    tunnel_service = (
        PROJECT_ROOT
        / "packaging"
        / "systemd"
        / "cloudflared-forkmesh.service"
    ).read_text(encoding="utf-8")
    assert "Requires=forkmesh-mirror.service" in tunnel_service
    assert "PartOf=forkmesh-mirror.service" in tunnel_service


def test_signed_health_renewal_timer_is_bounded_and_gateway_coupled():
    service = (
        PROJECT_ROOT
        / "packaging"
        / "systemd"
        / "forkmesh-mirror-renew.service"
    ).read_text(encoding="utf-8")
    timer = (
        PROJECT_ROOT
        / "packaging"
        / "systemd"
        / "forkmesh-mirror-renew.timer"
    ).read_text(encoding="utf-8")

    assert "User=forkmesh-mirror" in service
    assert "Requires=forkmesh-mirror.service" in service
    assert "PartOf=forkmesh-mirror.service" in service
    assert "headless_mirror_refresh.py" in service
    assert " renew" in service
    assert "ProtectSystem=strict" in service
    assert (
        "ReadWritePaths=/var/lib/forkmesh-mirror/gateway "
        "/var/lib/forkmesh-mirror/identity"
    ) in service
    assert "OnUnitActiveSec=4min" in timer
    assert "RandomizedDelaySec=30s" in timer
    assert "Persistent=true" in timer
