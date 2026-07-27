"""Local read-only mirror gateway and capability boundary contracts."""

import base64
import gzip
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading
from types import SimpleNamespace
from urllib.parse import urlencode

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import mirror_gateway as gateway  # noqa: E402


PUBLIC_KEY = base64.urlsafe_b64encode(b"N" * 32).decode().rstrip("=")
ROUTER_KEY = base64.urlsafe_b64encode(b"R" * 32).decode().rstrip("=")
SIGNATURE = base64.urlsafe_b64encode(b"S" * 64).decode().rstrip("=")
NOW = 2_000_000_000_000


def run(command, cwd):
    return subprocess.run(
        command,
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def make_bare_repository(tmp_path):
    bare = tmp_path / "mirror.git"
    source = tmp_path / "source"
    run(["git", "init", "--bare", str(bare)], tmp_path)
    run(["git", "init", "-b", "main", str(source)], tmp_path)
    run(["git", "config", "user.email", "alice@example.test"], source)
    run(["git", "config", "user.name", "Alice"], source)
    (source / "README.md").write_text(
        "# Example\n\nsearchable mirror gateway\n", encoding="utf-8"
    )
    (source / "src").mkdir()
    (source / "src" / "main.py").write_text(
        "def greet():\n    return 'hello'\n", encoding="utf-8"
    )
    (source / "src" / "util.js").write_text(
        "export const answer = 42;\n", encoding="utf-8"
    )
    (source / "src" / "main.js").write_text(
        "import { answer } from './util.js';\n"
        "export function value() { return answer; }\n",
        encoding="utf-8",
    )
    (source / "coverage").mkdir()
    (source / "coverage" / "lcov.info").write_text(
        "SF:src/main.js\nLF:4\nLH:3\nend_of_record\n"
        "SF:src/util.js\nDA:1,1\nend_of_record\n"
        "SF:../../outside.py\nLF:1\nLH:1\nend_of_record\n",
        encoding="utf-8",
    )
    (source / "image.bin").write_bytes(b"\x00\x01\x02")
    (source / "logo.png").write_bytes(
        b"\x89PNG\r\n\x1a\n" + b"repository-owned-logo"
    )
    run(["git", "add", "."], source)
    run(["git", "commit", "-m", "Initial files"], source)
    (source / "src" / "main.py").write_text(
        "def greet(name='world'):\n    return f'hello {name}'\n",
        encoding="utf-8",
    )
    run(["git", "add", "."], source)
    run(["git", "commit", "-m", "Improve greeting"], source)
    run(["git", "branch", "release-preview"], source)
    run(["git", "switch", "-c", "forkmesh/pulls"], source)
    (source / "pulls" / "42").mkdir(parents=True)
    (source / "pulls" / "42" / "pull.md").write_text(
        "---\nnumber: 42\nstatus: open\n"
        "title: Metadata branch truth\n---\n\nPinned review.\n",
        encoding="utf-8",
    )
    run(["git", "add", "pulls/42/pull.md"], source)
    run(["git", "commit", "-m", "Add pull metadata branch"], source)
    run(["git", "switch", "main"], source)
    run(["git", "remote", "add", "origin", str(bare)], source)
    run([
        "git", "push", "origin",
        "main", "release-preview", "forkmesh/pulls",
    ], source)
    run(["git", "symbolic-ref", "HEAD", "refs/heads/main"], bare)
    commit = run(["git", "rev-parse", "HEAD"], source)
    return bare, commit


def test_runtime_cleanup_removes_only_gateway_materializations(tmp_path):
    runtime = tmp_path / "runtime"
    runtime.mkdir(mode=0o700)
    stale_one = runtime / "forkmesh-mirror-runtime-a1_b2"
    stale_two = runtime / "forkmesh-mirror-runtime-z9"
    unrelated = runtime / "operator-data"
    stale_one.mkdir()
    stale_two.mkdir()
    unrelated.mkdir()
    (stale_one / "pack").write_bytes(b"stale")
    (unrelated / "keep").write_bytes(b"owned elsewhere")

    assert gateway.cleanup_stale_runtime(runtime) == 2
    assert not stale_one.exists()
    assert not stale_two.exists()
    assert (unrelated / "keep").read_bytes() == b"owned elsewhere"


def signed_manifest(origin="https://mirror.example.test", node="mirror-a"):
    manifest = {
        "schemaVersion": 1,
        "type": "forkmesh.mirror-endpoint",
        "generatedAt": "2026-07-23T10:00:00Z",
        "node": {"name": node, "publicKey": PUBLIC_KEY},
        "endpoint": {
            "origin": origin,
            "healthUrl": origin + "/health",
            "manifestUrl": origin + "/forkmesh-mirror.json",
            "repositoryUrlTemplate": origin + "/{owner}/{repository}",
            "transport": "direct-https",
            "mainProxyMode": "masked",
        },
        "dns": {
            "recordName": "mirror.example.test",
            "proxied": True,
            "workerRoute": "mirror.example.test/*",
        },
        "storage": {
            "repositoryBytesInD1": False,
            "repositoryByteOwner": "independent-mirror-host",
            "d1Purpose": ["service-discovery"],
        },
    }
    payload = gateway._canonical_json(manifest).encode()
    manifest["signature"] = {
        "algorithm": "Ed25519",
        "encoding": "base64url-no-padding",
        "canonicalization": "forkmesh-json-sort-v1",
        "payloadSha256": hashlib.sha256(payload).hexdigest(),
        "value": SIGNATURE,
    }
    return manifest


def write_config(
    tmp_path,
    bare,
    *,
    repositories=None,
    listen_host="127.0.0.1",
):
    manifest_path = tmp_path / "manifest.json"
    manifest_path.write_text(
        json.dumps(signed_manifest()), encoding="utf-8"
    )
    ciphertext = tmp_path / "project.tar.age"
    ciphertext.write_bytes(b"test-only-encrypted-public-repository")
    if repositories is None:
        repositories = [
            {
                "owner": "alice",
                "name": "project",
                "visibility": "public",
                "enabled": True,
                "encryptedArchive": {
                    "scheme": "age-encrypted-tar-v1",
                    "ciphertextPath": str(ciphertext),
                    "ciphertextSha256": hashlib.sha256(
                        ciphertext.read_bytes()).hexdigest(),
                    "keyReference": "keychain:test/alice-project",
                    "materializeCommand": ["materialize-test-archive"],
                },
                "releaseStore": str(tmp_path / "releases"),
                "integrity": {
                    "expectedRefsSha256": gateway.refs_sha256(bare)
                },
            },
            {
                "owner": "alice",
                "name": "private-project",
                "visibility": "private",
                "enabled": False,
            },
        ]
    config = {
        "schemaVersion": 1,
        "node": {"name": "mirror-a", "publicKey": PUBLIC_KEY},
        "routerPublicKey": ROUTER_KEY,
        "publicOrigin": "https://mirror.example.test",
        "listen": {"host": listen_host, "port": 8790},
        "manifestPath": str(manifest_path),
        "requestVerifierCommand": ["verify-capability"],
        "healthSignerCommand": ["sign-health"],
        "repositories": repositories,
    }
    config_path = tmp_path / "gateway.json"
    config_path.write_text(json.dumps(config), encoding="utf-8")
    return config_path


def write_alias_config(tmp_path, bare):
    config_path = write_config(tmp_path, bare)
    value = json.loads(config_path.read_text(encoding="utf-8"))
    source = value["repositories"][0]
    value["repositories"] = []
    for owner in ("mirror-two", "forkmesh"):
        repository = json.loads(json.dumps(source))
        repository["owner"] = owner
        value["repositories"].append(repository)
    config_path.write_text(json.dumps(value), encoding="utf-8")
    return config_path


class AcceptVerifier:
    def __init__(self):
        self.messages = []

    def verify(self, message, signature):
        self.messages.append((message, signature))
        return signature == SIGNATURE


class HealthSigner:
    def __init__(self):
        self.messages = []

    def sign(self, message):
        self.messages.append(message)
        return SIGNATURE


class FakeArchiveMaterializer:
    def __init__(self, repository):
        self.repository = repository
        self.archives = []
        self.destinations = []

    def materialize(self, archive, destination):
        self.archives.append(archive)
        self.destinations.append(destination)
        return self.repository


def capability_headers(target, method="GET", body=b"", request_id="request_123456"):
    digest = hashlib.sha256(body).hexdigest()
    return {
        "X-ForkMesh-Node": "mirror-a",
        "X-ForkMesh-Request-Id": request_id,
        "X-ForkMesh-Issued-At": str(NOW),
        "X-ForkMesh-Body-Sha256": digest,
        "X-ForkMesh-Signature": SIGNATURE,
    }


@pytest.fixture
def application(tmp_path):
    bare, commit = make_bare_repository(tmp_path)
    releases = tmp_path / "releases"
    release_bytes = b"release-content"
    release_hash = hashlib.sha256(release_bytes).hexdigest()
    release_path = releases / "sha256" / release_hash[:2] / release_hash
    release_path.mkdir(parents=True)
    (release_path / "data").write_bytes(release_bytes)
    config = gateway.load_config(write_config(tmp_path, bare))
    logs = []
    app = gateway.GatewayApplication(
        config,
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=FakeArchiveMaterializer(bare),
        clock_ms=lambda: NOW,
        log=logs.append,
    )
    yield app, commit, release_hash, logs
    app.close()


def dispatch(app, operation, query=None, *, request_id, method="GET", body=b""):
    target = f"/v1/repositories/alice/project/{operation}"
    if query:
        target += "?" + urlencode(query)
    headers = capability_headers(
        target, method=method, body=body, request_id=request_id
    )
    if method == "POST":
        headers["Content-Type"] = "application/x-git-upload-pack-request"
    return app.dispatch(method, target, headers, body)


def decode_json(response):
    return json.loads(response.body)


def private_replica_bytes(opaque_id):
    envelope = {
        "kind": "forkmesh.mirror",
        "v": 1,
        "alg": "x25519+mlkem768/aes256gcm",
        "nonce": base64.b64encode(b"N" * 12).decode(),
        "tag": base64.b64encode(b"T" * 16).decode(),
        "body": base64.b64encode(b"opaque ciphertext bytes").decode(),
        "recipients": [{
            "kid": hashlib.sha256(b"recipient-public-bundle").hexdigest(),
            "x25519": base64.b64encode(b"X" * 32).decode(),
            "mlkem768": base64.b64encode(b"M" * 64).decode(),
            "nonce": base64.b64encode(b"R" * 12).decode(),
            "tag": base64.b64encode(b"G" * 16).decode(),
            "key": base64.b64encode(b"K" * 48).decode(),
        }],
    }
    stored = {
        "schemaVersion": 1,
        "kind": "forkmesh.private-replica",
        "opaqueId": opaque_id,
        "keyEpoch": 1,
        "createdAt": 1_900_000_000_000,
        "updatedAt": 1_900_000_000_000,
        "ciphertextSha256": hashlib.sha256(
            gateway._canonical_json(envelope).encode()).hexdigest(),
        "envelope": envelope,
    }
    return gateway._canonical_json(stored).encode()


def test_config_requires_loopback_public_integrity_and_no_secret_fields(tmp_path):
    bare, _commit = make_bare_repository(tmp_path)
    with pytest.raises(gateway.GatewayError, match="loopback"):
        gateway.load_config(
            write_config(tmp_path, bare, listen_host="0.0.0.0")
        )

    path = write_config(tmp_path, bare)
    value = json.loads(path.read_text())
    value["privateKey"] = "must-never-be-here"
    path.write_text(json.dumps(value))
    with pytest.raises(gateway.GatewayError, match="secret field"):
        gateway.load_config(path)

    private_enabled = [
        {
            "owner": "alice",
            "name": "private-project",
            "visibility": "private",
            "enabled": True,
            "gitDir": str(bare),
        }
    ]
    with pytest.raises(gateway.GatewayError, match="cannot be enabled"):
        gateway.load_config(
            write_config(tmp_path, bare, repositories=private_enabled)
        )

    plaintext_public = [
        {
            "owner": "alice",
            "name": "project",
            "visibility": "public",
            "enabled": True,
            "gitDir": str(bare),
            "integrity": {
                "expectedRefsSha256": gateway.refs_sha256(bare),
            },
        }
    ]
    with pytest.raises(gateway.GatewayError, match="plaintext gitDir"):
        gateway.load_config(
            write_config(tmp_path, bare, repositories=plaintext_public)
        )

    ambiguous_path = write_config(tmp_path, bare)
    ambiguous = json.loads(ambiguous_path.read_text())
    ambiguous["repositories"][0]["encryptedArchive"]["scheme"] = (
        "operator-envelope-v1")
    ambiguous_path.write_text(json.dumps(ambiguous))
    with pytest.raises(
        gateway.GatewayError, match="age-encrypted-tar-v1"
    ):
        gateway.load_config(ambiguous_path)


def test_config_allows_read_only_git_dir_only_in_hosted_import_root(
    tmp_path, monkeypatch
):
    hosted_root = tmp_path / "imports"
    node_root = hosted_root / "mirror-a"
    node_root.mkdir(parents=True)
    bare, _commit = make_bare_repository(node_root)
    repository = node_root / "project.git"
    bare.rename(repository)
    monkeypatch.setattr(gateway, "HOSTED_PUBLIC_GIT_ROOT", hosted_root)
    config = gateway.load_config(
        write_config(
            tmp_path,
            repository,
            repositories=[
                {
                    "owner": "mirror-a",
                    "name": "project",
                    "visibility": "public",
                    "enabled": True,
                    "gitDir": str(repository),
                    "integrity": {
                        "expectedRefsSha256": gateway.refs_sha256(repository),
                    },
                    "operations": ["git-info-refs", "git-upload-pack", "sizes"],
                }
            ],
        )
    )
    assert config.repositories[0].git_dir == repository
    assert config.repositories[0].encrypted_archive is None


def test_identical_alias_archives_materialize_once_with_distinct_configs(
    tmp_path,
):
    bare, _commit = make_bare_repository(tmp_path)
    config = gateway.load_config(write_alias_config(tmp_path, bare))
    materializer = FakeArchiveMaterializer(bare)
    app = gateway.GatewayApplication(
        config,
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=materializer,
    )
    try:
        mirror = app.repositories[("mirror-two", "project")]
        organization = app.repositories[("forkmesh", "project")]
        assert len(materializer.archives) == 1
        assert len(materializer.destinations) == 1
        assert mirror is not organization
        assert mirror.git_dir == organization.git_dir == bare
        assert mirror.config is config.repositories[0]
        assert organization.config is config.repositories[1]
        assert app.quarantined_count == 0
    finally:
        app.close()


def test_alias_archive_metadata_difference_materializes_separately(tmp_path):
    bare, _commit = make_bare_repository(tmp_path)
    config_path = write_alias_config(tmp_path, bare)
    value = json.loads(config_path.read_text(encoding="utf-8"))
    value["repositories"][1]["encryptedArchive"]["keyReference"] = (
        "keychain:test/forkmesh-project"
    )
    config_path.write_text(json.dumps(value), encoding="utf-8")
    materializer = FakeArchiveMaterializer(bare)
    app = gateway.GatewayApplication(
        gateway.load_config(config_path),
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=materializer,
    )
    try:
        assert len(materializer.archives) == 2
        assert materializer.archives[0] != materializer.archives[1]
        assert len(set(materializer.destinations)) == 2
        assert len(app.repositories) == 2
        assert app.quarantined_count == 0
    finally:
        app.close()


def test_bad_alias_integrity_is_quarantined_without_poisoning_shared_archive(
    tmp_path,
):
    bare, _commit = make_bare_repository(tmp_path)
    config_path = write_alias_config(tmp_path, bare)
    value = json.loads(config_path.read_text(encoding="utf-8"))
    value["repositories"][0]["integrity"]["expectedRefsSha256"] = "0" * 64
    config_path.write_text(json.dumps(value), encoding="utf-8")
    materializer = FakeArchiveMaterializer(bare)
    app = gateway.GatewayApplication(
        gateway.load_config(config_path),
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=materializer,
        clock_ms=lambda: NOW,
    )
    try:
        assert len(materializer.archives) == 1
        assert ("mirror-two", "project") not in app.repositories
        assert ("forkmesh", "project") in app.repositories
        assert app.quarantined_count == 1

        responses = []
        for index, name in enumerate(("project", "missing")):
            target = f"/v1/repositories/mirror-two/{name}/tree"
            response = app.dispatch(
                "GET",
                target,
                capability_headers(
                    target, request_id=f"quarantined_req_{index:02d}"
                ),
                b"",
            )
            responses.append((response.status, response.body))
        assert responses[0] == responses[1]
        assert responses[0][0] == 404
    finally:
        app.close()


def test_failed_archive_identity_is_materialized_once_and_quarantines_aliases(
    tmp_path,
):
    bare, _commit = make_bare_repository(tmp_path)
    config = gateway.load_config(write_alias_config(tmp_path, bare))

    class FailingMaterializer:
        def __init__(self):
            self.calls = []

        def materialize(self, archive, destination):
            self.calls.append((archive, destination))
            raise gateway.GatewayError("test materialization failure")

    materializer = FailingMaterializer()
    app = gateway.GatewayApplication(
        config,
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=materializer,
    )
    try:
        assert len(materializer.calls) == 1
        assert app.repositories == {}
        assert app.quarantined_count == 2
    finally:
        app.close()


def test_manifest_digest_identity_and_origin_are_validated(tmp_path):
    bare, _commit = make_bare_repository(tmp_path)
    config_path = write_config(tmp_path, bare)
    config = json.loads(config_path.read_text())
    manifest_path = Path(config["manifestPath"])
    manifest = json.loads(manifest_path.read_text())
    manifest["endpoint"]["origin"] = "https://attacker.example"
    manifest_path.write_text(json.dumps(manifest))
    with pytest.raises(gateway.GatewayError, match="identity/origin"):
        gateway.load_config(config_path)


def test_capability_message_is_byte_compatible_with_edge_router():
    sys.path.insert(0, str(ROOT / "cloudflare_worker" / "src"))
    import edge_routing

    arguments = (
        "mirror-a",
        "GET",
        "/v1/repositories/alice/project/tree?path=src",
        hashlib.sha256(b"").hexdigest(),
        "request_123456",
        NOW,
    )
    assert gateway.request_message(*arguments) == edge_routing.request_message(
        *arguments
    )


def test_capability_binds_exact_target_body_time_and_is_single_use(application):
    app, _commit, _release_hash, _logs = application
    target = "/v1/repositories/alice/project/tree?path=src"
    headers = capability_headers(target, request_id="request_123456")
    first = app.dispatch("GET", target, headers, b"")
    assert first.status == 200
    replay = app.dispatch("GET", target, headers, b"")
    assert replay.status == 404

    wrong_target = target + "&ref=main"
    bad = app.dispatch("GET", wrong_target, headers, b"")
    assert bad.status == 404

    stale_headers = capability_headers(
        target, request_id="request_654321"
    )
    stale_headers["X-ForkMesh-Issued-At"] = str(NOW - 60_001)
    assert app.dispatch("GET", target, stale_headers, b"").status == 404


def test_opaque_private_replica_stream_requires_capability_and_exact_ciphertext(
    tmp_path,
):
    bare, _commit = make_bare_repository(tmp_path)
    store = tmp_path / "private-replicas"
    store.mkdir(mode=0o700)
    opaque_id = "a" * 64
    raw = private_replica_bytes(opaque_id)
    replica = store / (opaque_id + ".fm-private")
    replica.write_bytes(raw)
    replica.chmod(0o600)
    config_path = write_config(tmp_path, bare)
    value = json.loads(config_path.read_text())
    value["privateReplicaStore"] = str(store)
    config_path.write_text(json.dumps(value))
    logs = []
    app = gateway.GatewayApplication(
        gateway.load_config(config_path),
        verifier=AcceptVerifier(),
        health_signer=HealthSigner(),
        materializer=FakeArchiveMaterializer(bare),
        clock_ms=lambda: NOW,
        log=logs.append,
    )
    try:
        target = "/v1/private-replicas/" + opaque_id
        unauthorized = app.dispatch("GET", target, {}, b"")
        assert unauthorized.status == 404
        response = app.dispatch(
            "GET",
            target,
            capability_headers(target, request_id="private_cipher_01"),
            b"",
        )
        assert response.status == 200
        assert response.content_type == (
            "application/vnd.forkmesh.private-replica+json")
        assert response.stream.path == replica
        assert response.headers["ETag"] == (
            '"sha256-' + hashlib.sha256(raw).hexdigest() + '"')

        missing_target = "/v1/private-replicas/" + ("b" * 64)
        missing = app.dispatch(
            "GET",
            missing_target,
            capability_headers(
                missing_target, request_id="private_cipher_02"),
            b"",
        )
        assert missing.status == unauthorized.status
        assert missing.body == unauthorized.body
        serialized = json.dumps(logs)
        assert opaque_id not in serialized
        assert "private-project" not in serialized
    finally:
        app.close()


def test_private_replica_rejects_tamper_and_path_traversal(tmp_path):
    store = tmp_path / "private-replicas"
    store.mkdir(mode=0o700)
    opaque_id = "c" * 64
    value = json.loads(private_replica_bytes(opaque_id))
    value["envelope"]["body"] = base64.b64encode(
        b"tampered ciphertext").decode()
    raw = gateway._canonical_json(value).encode()
    (store / (opaque_id + ".fm-private")).write_bytes(raw)
    with pytest.raises(gateway.GatewayError, match="not found"):
        gateway.private_replica_file(store, opaque_id, len(raw) + 1)
    with pytest.raises(gateway.GatewayError, match="not found"):
        gateway.private_replica_file(store, "../secret", 1024)


def test_health_challenge_is_domain_separated_and_externally_signed(application):
    app, _commit, _release_hash, _logs = application
    nonce = "0123456789abcdef"
    response = app.dispatch(
        "GET", f"/health?nonce={nonce}&issuedAt={NOW}", {}, b""
    )
    payload = decode_json(response)
    assert response.status == 200
    assert payload["integrity"] == "ok"
    assert payload["publicRepositoryCount"] == 1
    assert "repositories" not in payload
    assert payload["challenge"]["signature"] == SIGNATURE
    assert app.health_signer.messages == [
        f"forkmesh-https-health-v1\nmirror-a\n{nonce}\n{NOW}"
    ]


def test_signed_repository_health_proof_binds_forkmesh_identity_and_refs(
    application,
):
    app, _commit, _release_hash, _logs = application
    nonce = "repository-proof-01"
    response = app.dispatch(
        "GET",
        (
            f"/health?nonce={nonce}&issuedAt={NOW}"
            "&owner=alice&repo=project"
        ),
        {},
        b"",
    )
    payload = decode_json(response)
    proof = payload["repositoryProof"]
    repository = app.repositories[("alice", "project")]
    assert proof["available"] is True
    assert proof["integrity"] == "ok"
    assert proof["refsSha256"] == gateway.refs_sha256(repository.git_dir)
    assert {"git-info-refs", "git-upload-pack", "tree", "raw"} <= set(
        proof["operations"]
    )
    expected = gateway.repository_health_challenge(
        "mirror-a",
        nonce,
        NOW,
        "alice",
        "project",
        available=True,
        integrity="ok",
        refs_digest=proof["refsSha256"],
        operations_digest=proof["operationsSha256"],
    )
    assert app.health_signer.messages[-1] == expected
    assert payload["challenge"]["messageType"] == (
        "forkmesh-https-health-repository-v1"
    )


def test_unknown_and_private_health_proofs_are_uniform_signed_unavailable(
    application,
):
    app, _commit, _release_hash, _logs = application
    proofs = []
    messages = []
    for index, name in enumerate(("private-project", "missing-project")):
        nonce = f"unavailable-proof-{index}"
        response = app.dispatch(
            "GET",
            (
                f"/health?nonce={nonce}&issuedAt={NOW}"
                f"&owner=alice&repo={name}"
            ),
            {},
            b"",
        )
        payload = decode_json(response)
        proof = dict(payload["repositoryProof"])
        proof.pop("repository")
        proofs.append(proof)
        messages.append(app.health_signer.messages[-1])
    assert proofs[0] == proofs[1]
    assert proofs[0]["available"] is False
    assert proofs[0]["integrity"] == "unavailable"
    assert all("private-project" not in json.dumps(proof) for proof in proofs)
    assert all(message.startswith("forkmesh-https-health-repository-v1") for message in messages)


def test_private_unknown_and_quarantined_repositories_are_indistinguishable(
    application,
):
    app, _commit, _release_hash, logs = application
    results = []
    for index, name in enumerate(("private-project", "missing-project")):
        target = f"/v1/repositories/alice/{name}/tree"
        response = app.dispatch(
            "GET",
            target,
            capability_headers(target, request_id=f"private_req_{index:04d}"),
            b"",
        )
        results.append((response.status, response.body))
    assert results[0] == results[1]
    assert b"private-project" not in results[0][1]
    assert all("owner" not in row and "repository" not in row for row in logs)


def test_tree_blob_history_commit_branches_search_stats_and_sizes(application):
    app, commit, _release_hash, _logs = application
    cases = [
        ("tree", {"path": "src"}, "entries"),
        ("blob", {"path": "README.md"}, "content"),
        ("history", {}, "commits"),
        ("commit", {"path": commit}, "files"),
        ("branches", {}, "branches"),
        ("search", {"path": "searchable"}, "code"),
        ("stats", {}, "extensions"),
        ("sizes", {}, "fileCount"),
    ]
    for index, (operation, query, field) in enumerate(cases):
        response = dispatch(
            app,
            operation,
            query,
            request_id=f"operation_{index:04d}",
        )
        assert response.status == 200, (operation, response.body)
        payload = decode_json(response)
        assert payload["ok"] is True
        assert field in payload
    tree = decode_json(
        dispatch(
            app,
            "tree",
            {},
            request_id="tree_exact_commit_time",
        )
    )
    assert re.fullmatch(
        r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}",
        tree["latestCommit"]["date"],
    )
    assert all(
        re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2}",
            commit["date"],
        )
        for commit in decode_json(
            dispatch(
                app,
                "history",
                {},
                request_id="history_exact_commit_times",
            )
        )["commits"]
    )
    history = decode_json(
        dispatch(
            app,
            "history",
            {"ref": commit},
            request_id="history_activity_windows",
        )
    )
    assert history["commit"] == commit
    assert history["activity"]["timezone"] == "UTC"
    assert history["activity"]["weekStartsOn"] == "monday"
    assert set(history["activity"]["windows"]) == {"today", "week", "month"}
    assert all(
        window["commits"] == 2
        for window in history["activity"]["windows"].values()
    )
    assert all(
        window["start"].endswith("+00:00")
        for window in history["activity"]["windows"].values()
    )
    raw = dispatch(
        app,
        "raw",
        {"path": "logo.png"},
        request_id="raw_repository_logo",
    )
    assert raw.status == 200
    assert raw.content_type == "image/png"
    assert raw.stream is not None
    assert raw.stream.content_type == "image/png"
    blob = decode_json(
        dispatch(
            app,
            "blob",
            {"path": "README.md"},
            request_id="blob_contents_01",
        )
    )
    assert "searchable mirror gateway" in blob["content"]


def test_branches_returns_main_and_additional_heads(application):
    app, _commit, _release_hash, _logs = application
    payload = decode_json(
        dispatch(
            app,
            "branches",
            {},
            request_id="branches_records_01",
        )
    )
    assert [branch["name"] for branch in payload["branches"]] == [
        "forkmesh/pulls",
        "main",
        "release-preview",
    ]
    assert all(branch["commit"] for branch in payload["branches"])
    assert all(branch["updatedAt"] for branch in payload["branches"])


def test_pull_metadata_branch_is_read_through_its_resolved_commit(application):
    app, _commit, _release_hash, _logs = application
    branches = decode_json(
        dispatch(
            app,
            "branches",
            {},
            request_id="pull_metadata_branches_01",
        )
    )["branches"]
    metadata = next(
        branch for branch in branches
        if branch["name"] == "forkmesh/pulls"
    )
    assert len(metadata["commit"]) == 40

    tree = decode_json(
        dispatch(
            app,
            "tree",
            {"path": "pulls", "ref": metadata["commit"]},
            request_id="pull_metadata_tree_01",
        )
    )
    assert tree["commit"] == metadata["commit"]
    assert [(entry["name"], entry["type"]) for entry in tree["entries"]] == [
        ("42", "tree")
    ]
    blob = decode_json(
        dispatch(
            app,
            "blob",
            {
                "path": "pulls/42/pull.md",
                "ref": metadata["commit"],
            },
            request_id="pull_metadata_blob_01",
        )
    )
    assert "Metadata branch truth" in blob["content"]


def test_collaboration_tree_avoids_whole_repo_analysis_and_per_entry_logs(
    application, monkeypatch,
):
    app, _commit, _release_hash, _logs = application
    repository = app.repositories[("alice", "project")]
    summary_paths = []
    original_summary = repository._commit_summary

    def unavailable_analysis(_commit):
        raise AssertionError(
            "collaboration metadata must not occupy the analysis lock"
        )

    def counted_summary(commit, path=""):
        summary_paths.append(path)
        return original_summary(commit, path)

    monkeypatch.setattr(repository, "_analysis", unavailable_analysis)
    monkeypatch.setattr(repository, "_commit_summary", counted_summary)
    metadata_commit = repository.resolve_commit("forkmesh/pulls")
    payload = repository.tree({
        "path": "pulls",
        "ref": metadata_commit,
    })

    assert payload["ok"] is True
    assert payload["commit"] == metadata_commit
    assert payload["analysis"]["commit"] == metadata_commit
    assert payload["analysis"]["dependency"]["status"] == "not-requested"
    assert payload["analysis"]["coverage"]["status"] == "not-requested"
    assert summary_paths == [""]
    assert [(item["name"], item["type"]) for item in payload["entries"]] == [
        ("42", "tree")
    ]
    assert payload["entries"][0]["analysisCommit"] == metadata_commit
    assert payload["entries"][0]["dependencies"] == []


def test_compare_returns_bounded_portable_pull_change_set(application):
    app, commit, _release_hash, _logs = application
    repository = app.repositories[("alice", "project")]
    base = gateway._run_git(
        repository.git_dir,
        ["rev-parse", commit + "^"],
        max_output=128,
    ).decode().strip()
    payload = decode_json(
        dispatch(
            app,
            "compare",
            {"base": base, "head": "main"},
            request_id="compare_change_set_01",
        )
    )
    assert payload["ok"] is True
    assert payload["baseOid"] == base
    assert payload["headOid"] == commit
    assert payload["mergeBaseOid"] == base
    assert payload["commitCount"] == 1
    assert "src/main.py" in payload["patch"]
    assert "Improve greeting" in payload["commits"]
    assert payload["commits"].startswith("From ")


def test_compare_requires_both_refs_and_enforces_byte_cap(
    application, monkeypatch
):
    app, commit, _release_hash, _logs = application
    missing = dispatch(
        app,
        "compare",
        {"head": commit},
        request_id="compare_missing_ref_01",
    )
    assert missing.status == 404

    repository = app.repositories[("alice", "project")]
    base = gateway._run_git(
        repository.git_dir,
        ["rev-parse", commit + "^"],
        max_output=128,
    ).decode().strip()
    monkeypatch.setattr(gateway, "MAX_COMPARE_BYTES", 8)
    oversized = dispatch(
        app,
        "compare",
        {"base": base, "head": commit},
        request_id="compare_byte_cap_01",
    )
    assert oversized.status == 503
    assert decode_json(oversized)["error"] == "mirror_unavailable"


def test_sizes_exposes_bounded_file_leaves_with_full_paths(application):
    app, _commit, _release_hash, _logs = application
    payload = decode_json(
        dispatch(
            app,
            "sizes",
            {},
            request_id="sizes_file_leaves_01",
        )
    )
    assert payload["type"] == "directory"
    root_children = payload["children"]
    readme = next(item for item in root_children if item["name"] == "README.md")
    assert readme == {
        "name": "README.md",
        "path": "README.md",
        "size": len("# Example\n\nsearchable mirror gateway\n".encode()),
        "type": "file",
    }
    src = next(item for item in root_children if item["name"] == "src")
    assert src["type"] == "directory"
    assert {
        item["path"] for item in src["children"] if item["type"] == "file"
    } == {"src/main.js", "src/main.py", "src/util.js"}
    assert all(
        len(node.get("children", ())) <= 41
        for node in [payload, *root_children]
    )


def test_tree_analysis_is_commit_matched_and_uses_real_edges_and_coverage(
    application,
):
    app, commit, _release_hash, _logs = application
    payload = decode_json(
        dispatch(
            app,
            "tree",
            {"path": "src", "ref": commit},
            request_id="tree_analysis_01",
        )
    )
    assert payload["commit"] == commit
    assert payload["analysis"]["commit"] == commit
    assert payload["analysis"]["dependency"]["filesParsed"] >= 3
    assert payload["analysis"]["dependency"]["edgeCount"] == 1
    assert payload["analysis"]["coverage"]["status"] == "available"
    assert payload["analysis"]["coverage"]["artifacts"] == [
        "coverage/lcov.info"
    ]
    entries = {entry["path"]: entry for entry in payload["entries"]}
    assert entries["src/main.js"]["dependencies"] == ["src/util.js"]
    assert entries["src/main.js"]["dependencyDepth"] == 1
    assert entries["src/main.js"]["coverage"] == 75.0
    assert entries["src/util.js"]["dependencies"] == []
    assert entries["src/util.js"]["dependencyDepth"] == 0
    assert entries["src/util.js"]["coverage"] == 100.0
    assert all(
        entry["analysisCommit"] == commit for entry in entries.values()
    )
    assert "../../outside.py" not in json.dumps(payload)


def test_bounded_analysis_parsers_reject_escape_and_parse_common_artifacts():
    paths = {"src/main.ts", "src/util.ts", "pkg/model.py"}
    istanbul = json.dumps({
        "/checkout/src/main.ts": {"s": {"0": 1, "1": 0, "2": 2}},
        "../../etc/passwd": {"s": {"0": 1}},
    }).encode()
    assert gateway._parse_json_coverage(istanbul, paths) == {
        "src/main.ts": 66.7
    }
    coverage_py = json.dumps({
        "files": {
            "pkg/model.py": {
                "summary": {
                    "covered_lines": 7,
                    "num_statements": 10,
                }
            }
        }
    }).encode()
    assert gateway._parse_json_coverage(coverage_py, paths) == {
        "pkg/model.py": 70.0
    }
    cobertura = (
        b'<coverage><class filename="src/util.ts" line-rate="0.5">'
        b"</class></coverage>"
    )
    assert gateway._parse_xml_coverage(cobertura, paths) == {
        "src/util.ts": 50.0
    }
    deeply_nested = b"[" * 2_000 + b"]" * 2_000
    assert gateway._parse_json_coverage(deeply_nested, paths) == {}
    suffix_index = {
        "src/util.ts": ["src/util.ts"],
        "util.ts": ["src/util.ts"],
    }
    assert gateway._dependency_target(
        "src/main.ts", "./util", paths, suffix_index
    ) == "src/util.ts"
    assert gateway._dependency_target(
        "src/main.ts", "../../../etc/passwd", paths, suffix_index
    ) == ""
    assert gateway._dependency_target(
        "src/main.ts", "https://example.test/a.js", paths, suffix_index
    ) == ""
    assert gateway._dependency_target(
        "src/main.ts",
        "//example.test/a.js",
        paths | {"example.test/a.js"},
        {**suffix_index, "example.test/a.js": ["example.test/a.js"]},
    ) == ""
    assert gateway._dependency_target(
        "src/main.ts",
        "react",
        paths | {"react.ts"},
        {**suffix_index, "react.ts": ["react.ts"]},
    ) == ""


def test_batched_blobs_preserve_repeated_paths_and_bound_missing_files(application):
    app, commit, _release_hash, _logs = application
    target = (
        "/v1/repositories/alice/project/blobs"
        "?path=README.md&path=src%2Fmain.py"
        "&path=does-not-exist&ref=HEAD"
    )
    response = app.dispatch(
        "GET",
        target,
        capability_headers(target, request_id="batched_blobs_01"),
        b"",
    )
    assert response.status == 200
    payload = decode_json(response)
    assert payload["commit"] == commit
    assert "searchable mirror gateway" in payload["blobs"]["README.md"]["content"]
    assert payload["blobs"]["src/main.py"]["ok"] is True
    assert payload["blobs"]["does-not-exist"] is None


def test_raw_release_and_git_upload_pack_are_stream_specs(application):
    app, commit, release_hash, _logs = application
    raw = dispatch(
        app,
        "raw",
        {"path": "README.md"},
        request_id="raw_stream_0001",
    )
    assert raw.status == 200
    assert raw.stream.kind == "process"
    assert raw.stream.disposition == "inline"
    raw_bytes = subprocess.run(
        raw.stream.command,
        check=True,
        capture_output=True,
        env=gateway._git_environment(),
    ).stdout
    assert b"searchable mirror gateway" in raw_bytes

    release = dispatch(
        app,
        "release-blob",
        {"sha256": release_hash},
        request_id="release_stream_1",
    )
    assert release.stream.kind == "file"
    assert release.stream.disposition == "attachment"
    assert release.stream.path.read_bytes() == b"release-content"

    advert = dispatch(
        app,
        "git-info-refs",
        {"service": "git-upload-pack"},
        request_id="git_advert_0001",
    )
    assert advert.status == 200
    assert advert.body.startswith(b"001e# service=git-upload-pack\n0000")

    def pkt_line(value):
        payload = value.encode("ascii")
        return f"{len(payload) + 4:04x}".encode("ascii") + payload

    upload_request = (
        pkt_line(
            f"want {commit} multi_ack_detailed no-done side-band-64k "
            "thin-pack no-progress include-tag ofs-delta deepen-since "
            "deepen-not agent=git/test"
        )
        + pkt_line("deepen 1")
        + b"0000"
        + pkt_line("done\n")
        + b"0000"
    )
    upload = dispatch(
        app,
        "git-upload-pack",
        request_id="git_upload_0001",
        method="POST",
        body=upload_request,
    )
    assert upload.status == 200
    assert upload.stream.kind == "process"
    assert "upload-pack" in upload.stream.command
    assert "receive-pack" not in upload.stream.command
    assert not any(
        item.startswith("uploadpack.packObjectsHook")
        for item in upload.stream.command
    )
    assert "core.alternateRefsCommand=/usr/bin/true" in upload.stream.command
    assert "uploadpack.hideRefs=refs/forkmesh/" in upload.stream.command
    assert "uploadpack.allowTipSHA1InWant=false" in upload.stream.command
    assert "uploadpack.allowReachableSHA1InWant=false" in upload.stream.command
    assert "uploadpack.allowAnySHA1InWant=false" in upload.stream.command
    completed = subprocess.run(
        upload.stream.command,
        input=upload.stream.input_bytes,
        capture_output=True,
        env=gateway._git_environment(),
        timeout=5,
        check=False,
    )
    assert completed.returncode == 0
    assert b"PACK" in completed.stdout


def test_real_git_clone_cannot_advertise_or_fetch_internal_merge_objects(
    application, tmp_path
):
    app, _commit, _release_hash, _logs = application
    repository = next(iter(app.repositories.values()))
    secret = subprocess.run(
        [
            "git", "--git-dir", str(repository.git_dir),
            "hash-object", "-w", "--stdin",
        ],
        input=b"owner-only merge job",
        check=True,
        capture_output=True,
    ).stdout.decode("ascii").strip()
    run([
        "git", "--git-dir", str(repository.git_dir), "update-ref",
        "refs/forkmesh/merge-jobs/test-job", secret,
    ], tmp_path)
    run([
        "git", "--git-dir", str(repository.git_dir), "update-ref",
        "refs/forkmesh/merge-staging/test-job/base", secret,
    ], tmp_path)
    # Repository-local settings are hostile input at this boundary. Explicit
    # command-line policy must override attempts to expose arbitrary objects.
    run([
        "git", "--git-dir", str(repository.git_dir), "config",
        "uploadpack.allowAnySHA1InWant", "true",
    ], tmp_path)
    run([
        "git", "--git-dir", str(repository.git_dir), "config", "--add",
        "uploadpack.hideRefs", "!refs/forkmesh/",
    ], tmp_path)

    advertisement = repository.git_advertisement()
    assert b"refs/forkmesh/" not in advertisement
    assert secret.encode("ascii") not in advertisement

    class GitHandler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass

        def do_GET(self):
            if self.path != "/repo/info/refs?service=git-upload-pack":
                self.send_error(404)
                return
            body = repository.git_advertisement()
            self.send_response(200)
            self.send_header(
                "Content-Type",
                "application/x-git-upload-pack-advertisement",
            )
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path != "/repo/git-upload-pack":
                self.send_error(404)
                return
            length = int(self.headers.get("Content-Length") or 0)
            request_body = self.rfile.read(length)
            stream = repository.upload_pack_spec(request_body)
            completed = subprocess.run(
                stream.command,
                input=stream.input_bytes,
                capture_output=True,
                env=gateway._git_environment(),
                timeout=10,
                check=False,
            )
            body = completed.stdout
            self.send_response(200 if completed.returncode == 0 else 500)
            self.send_header(
                "Content-Type", "application/x-git-upload-pack-result")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer(("127.0.0.1", 0), GitHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = "http://127.0.0.1:%d/repo" % server.server_address[1]
    clone = tmp_path / "clone-with-hidden-refs"
    try:
        cloned = subprocess.run(
            ["git", "clone", "--no-tags", origin, str(clone)],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
            env={**gateway._git_environment(), "GIT_PROTOCOL": "version=0"},
        )
        assert cloned.returncode == 0, cloned.stderr
        refs = run(["git", "-C", str(clone), "show-ref"], tmp_path)
        assert "refs/forkmesh/" not in refs
        assert (clone / "README.md").exists()
        assert subprocess.run(
            ["git", "-C", str(clone), "cat-file", "-e", secret],
            capture_output=True,
            check=False,
        ).returncode != 0
        exact_fetch = subprocess.run(
            ["git", "-C", str(clone), "fetch", origin, secret],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
            env={**gateway._git_environment(), "GIT_PROTOCOL": "version=0"},
        )
        assert exact_fetch.returncode != 0
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def test_git_upload_pack_decodes_gzip_after_verifying_transport_digest(application):
    app, _commit, _release_hash, _logs = application
    target = "/v1/repositories/alice/project/git-upload-pack"
    compressed = gzip.compress(b"0000")
    headers = capability_headers(
        target,
        method="POST",
        body=compressed,
        request_id="git_gzip_req_01",
    )
    headers["Content-Type"] = "application/x-git-upload-pack-request"
    headers["Content-Encoding"] = "gzip"
    response = app.dispatch("POST", target, headers, compressed)
    assert response.status == 200
    assert response.stream.input_bytes == b"0000"


def test_http_adapter_streams_ordinary_https_origin_contract_without_redirect(
    application,
):
    app, _commit, _release_hash, logs = application
    server = gateway.MirrorGatewayServer(("127.0.0.1", 0), app)
    # The server owns application cleanup after this point.
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        target = "/v1/repositories/alice/project/raw?path=README.md"
        connection = http.client.HTTPConnection(
            "127.0.0.1", server.server_address[1], timeout=5
        )
        connection.request(
            "GET",
            target,
            headers=capability_headers(
                target, request_id="http_raw_req_01"
            ),
        )
        response = connection.getresponse()
        body = response.read()
        assert response.status == 200
        assert b"searchable mirror gateway" in body
        assert response.getheader("Location") is None
        assert response.getheader("Set-Cookie") is None
        assert response.getheader("X-Content-Type-Options") == "nosniff"
        connection.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    assert logs
    serialized = json.dumps(logs)
    assert "README.md" not in serialized
    assert "127.0.0.1" not in serialized


def test_release_hash_is_verified_before_any_bytes_are_served(application):
    app, _commit, release_hash, _logs = application
    repository = next(iter(app.repositories.values()))
    path = (
        repository.config.release_store
        / "sha256"
        / release_hash[:2]
        / release_hash
        / "data"
    )
    path.write_bytes(b"tampered")
    response = dispatch(
        app,
        "release-blob",
        {"sha256": release_hash},
        request_id="release_bad_001",
    )
    assert response.status == 404
    assert response.stream is None


def test_external_helpers_receive_public_json_and_no_ambient_credentials(monkeypatch):
    monkeypatch.setenv("CLOUDFLARE_API_TOKEN", "api-secret")
    monkeypatch.setenv("FORKMESH_PRIVATE_KEY", "private-secret")
    calls = []

    def run_helper(command, **kwargs):
        calls.append((command, kwargs))
        request = json.loads(kwargs["input"])
        if request["type"].endswith("verification"):
            return SimpleNamespace(
                stdout=json.dumps({"valid": True, "publicKey": ROUTER_KEY})
            )
        return SimpleNamespace(
            stdout=json.dumps({"publicKey": PUBLIC_KEY, "signature": SIGNATURE})
        )

    verifier = gateway.ExternalCapabilityVerifier(
        ["verify"], ROUTER_KEY, runner=run_helper
    )
    assert verifier.verify("public-message", SIGNATURE)
    signer = gateway.ExternalHealthSigner(
        ["sign"], PUBLIC_KEY, runner=run_helper
    )
    assert signer.sign("public-health-message") == SIGNATURE
    assert all("CLOUDFLARE_API_TOKEN" not in call[1]["env"] for call in calls)
    assert all("FORKMESH_PRIVATE_KEY" not in call[1]["env"] for call in calls)
    assert all("api-secret" not in call[1]["input"] for call in calls)
    assert all("private-secret" not in call[1]["input"] for call in calls)


def test_encrypted_archive_materializer_uses_ciphertext_digest_and_key_reference(
    tmp_path,
):
    bare, _commit = make_bare_repository(tmp_path)
    ciphertext = tmp_path / "repository.tar.age"
    ciphertext.write_bytes(
        gateway.AGE_NATIVE_HEADER + b"opaque-age-ciphertext")
    archive = gateway.EncryptedArchive(
        scheme="age-encrypted-tar-v1",
        ciphertext_path=ciphertext,
        ciphertext_sha256=hashlib.sha256(ciphertext.read_bytes()).hexdigest(),
        key_reference="keychain:forkmesh/alice-project",
        materialize_command=("decrypt-archive",),
    )
    captured = {}

    def materialize(command, **kwargs):
        request = json.loads(kwargs["input"])
        captured.update(request)
        destination = Path(request["destination"])
        shutil.copytree(bare, destination / "repo.git")
        return SimpleNamespace(
            stdout=json.dumps({"ok": True, "repositoryPath": "repo.git"})
        )

    destination = tmp_path / "runtime"
    destination.mkdir()
    result = gateway.ArchiveMaterializer(runner=materialize).materialize(
        archive, destination
    )
    assert result == destination / "repo.git"
    assert captured["keyReference"] == "keychain:forkmesh/alice-project"
    assert "key" not in {
        key.lower()
        for key in captured
        if key.lower() not in {"keyreference"}
    }
    assert not any(
        field in captured for field in ("privateKey", "secret", "seed", "token")
    )


def test_archive_materializer_rejects_plaintext_before_external_helper(tmp_path):
    ciphertext = tmp_path / "not-encrypted.tar.age"
    ciphertext.write_bytes(b"plaintext tar bytes")
    archive = gateway.EncryptedArchive(
        scheme="age-encrypted-tar-v1",
        ciphertext_path=ciphertext,
        ciphertext_sha256=hashlib.sha256(ciphertext.read_bytes()).hexdigest(),
        key_reference="keychain:forkmesh/alice-project",
        materialize_command=("decrypt-archive",),
    )
    calls = []

    def must_not_run(*args, **kwargs):
        calls.append((args, kwargs))
        raise AssertionError("plaintext reached external materializer")

    with pytest.raises(gateway.GatewayError, match="not an age file"):
        gateway.ArchiveMaterializer(runner=must_not_run).materialize(
            archive, tmp_path / "runtime")
    assert calls == []


def test_source_never_enables_push_or_default_request_logging():
    source = (ROOT / "tools" / "mirror_gateway.py").read_text(encoding="utf-8")
    schema = json.loads(
        (ROOT / "docs" / "mirror-gateway-config.schema.json").read_text(
            encoding="utf-8"))
    assert "receive-pack" not in source
    assert "def log_message" in source
    assert "BaseHTTPRequestHandler logs client IP and raw path" in source
    assert "gateway must listen on loopback" in source
    assert "plaintextPublicRepository" not in schema["$defs"]
    enabled = schema["$defs"]["encryptedPublicRepository"]["allOf"][1]
    assert "encryptedArchive" in enabled["required"]
    assert "gitDir" not in json.dumps(enabled)
    scheme = enabled["properties"]["encryptedArchive"]["properties"]["scheme"]
    assert scheme == {"const": "age-encrypted-tar-v1"}
    assert "operator-envelope-v1" not in source


def test_log_rejects_attacker_controlled_request_ids(application):
    app, _commit, _release_hash, logs = application
    app._record(
        request_id="private-form-value\nnext",
        operation="tree",
        status=404,
        started=0,
        byte_count=0,
    )
    assert logs[-1]["requestId"] == ""
    assert "private-form-value" not in json.dumps(logs[-1])
