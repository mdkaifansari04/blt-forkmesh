#!/usr/bin/env python3
"""Fail-closed local HTTPS-origin gateway for independent ForkMesh mirrors.

The process intentionally listens on loopback HTTP.  ``cloudflared`` (or an
equivalent operator-controlled TLS proxy) is the only supported public-facing
transport.  Every repository request must carry a short-lived capability
signed by the ForkMesh routing Worker; the gateway invokes an external
verifier with public data only.  Health challenges are signed by a separate
local signer process, so this process never loads an identity private key.

Repository names, paths, request queries, Git stderr, source bytes, client
addresses, and user agents are never written to the gateway log.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass, field
from datetime import datetime, timezone
import gzip
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import ipaddress
import itertools
import json
import mimetypes
import os
from pathlib import Path
import posixpath
import re
import selectors
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Callable, Iterable, Mapping
from urllib.parse import parse_qs, unquote, urlsplit


SCHEMA_VERSION = 1
CAPABILITY_WINDOW_MS = 60_000
MAX_REQUEST_BODY = 8 * 1024 * 1024
MAX_JSON_BLOB = 2 * 1024 * 1024
MAX_JSON_OUTPUT = 8 * 1024 * 1024
MAX_COMPARE_BYTES = 4 * 1024 * 1024
MAX_COMPARE_COMMITS = 500
MAX_TREE_ENTRIES = 500
MAX_SEARCH_RESULTS = 60
MAX_ANALYSIS_FILES = 320
MAX_ANALYSIS_REPOSITORY_FILES = 20_000
MAX_ANALYSIS_FILE_BYTES = 256 * 1024
MAX_ANALYSIS_TOTAL_BYTES = 6 * 1024 * 1024
MAX_COVERAGE_ARTIFACT_BYTES = 2 * 1024 * 1024
MAX_DEPENDENCY_EDGES = 4_000
PROCESS_TIMEOUT_SECONDS = 30
STREAM_TIMEOUT_SECONDS = 10 * 60
PUBLIC_OPERATIONS = frozenset(
    {
        "git-info-refs",
        "git-upload-pack",
        "tree",
        "blobs",
        "blob",
        "raw",
        "history",
        "commit",
        "compare",
        "branches",
        "search",
        "stats",
        "sizes",
        "release-blob",
    }
)
LOG_OPERATIONS = PUBLIC_OPERATIONS | {"private-replica"}
NODE_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
REQUEST_ID_RE = re.compile(r"^[A-Za-z0-9_-]{12,80}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
OPAQUE_REPLICA_RE = re.compile(r"^[0-9a-f]{64}$")
AGE_NATIVE_HEADER = b"age-encryption.org/v1\n"
AGE_ARMORED_HEADER = b"-----BEGIN AGE ENCRYPTED FILE-----\n"
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{4,64}$")
BASE64URL_RE = re.compile(r"^[A-Za-z0-9_-]+$")
FORBIDDEN_CONFIG_KEY_RE = re.compile(
    r"(?:private.?key|secret|token|seed|mnemonic|keypair|password|credential)",
    re.IGNORECASE,
)
SENSITIVE_ENV_RE = re.compile(
    r"(?:SECRET|TOKEN|PRIVATE|SEED|MNEMONIC|KEYPAIR|PASSWORD|CREDENTIAL)",
    re.IGNORECASE,
)

ANALYZED_SOURCE_SUFFIXES = frozenset(
    {
        ".c", ".cc", ".cpp", ".cs", ".css", ".dart", ".go", ".h", ".hpp",
        ".java", ".js", ".jsx", ".kt", ".mjs", ".php", ".py", ".rb", ".rs",
        ".scss", ".swift", ".ts", ".tsx",
    }
)
COVERAGE_ARTIFACT_NAMES = frozenset(
    {
        "coverage.json",
        "coverage-final.json",
        "coverage-summary.json",
        "cobertura.xml",
        "jacoco.xml",
        "lcov.info",
    }
)


class GatewayError(RuntimeError):
    """Safe-to-display configuration or request failure."""


class GitError(RuntimeError):
    """Internal Git failure; its message never includes Git stderr or paths."""


def _canonical_json(value: Any) -> str:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    )


def _base64url_decode(value: str, expected_bytes: int | None = None) -> bytes:
    if not isinstance(value, str) or not BASE64URL_RE.fullmatch(value):
        raise GatewayError("value must be unpadded base64url")
    try:
        decoded = base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))
    except ValueError as exc:
        raise GatewayError("value is not valid base64url") from exc
    if expected_bytes is not None and len(decoded) != expected_bytes:
        raise GatewayError(f"value must contain exactly {expected_bytes} bytes")
    return decoded


def _base64url_encode(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).decode("ascii").rstrip("=")


def _assert_no_secret_fields(value: Any, context: str = "configuration") -> None:
    """Reject secret-shaped fields at every depth.

    Public-key and opaque key-reference names are deliberately allowed.  A key
    reference identifies an OS keychain/HSM entry; it is not key material.
    """

    allowed = {
        "publicKey",
        "routerPublicKey",
        "keyReference",
        "expectedRefsSha256",
        "ciphertextSha256",
        "sha256",
    }
    if isinstance(value, dict):
        for key, item in value.items():
            name = str(key)
            if name not in allowed and FORBIDDEN_CONFIG_KEY_RE.search(name):
                raise GatewayError(f"{context} contains a prohibited secret field")
            _assert_no_secret_fields(item, context)
    elif isinstance(value, list):
        for item in value:
            _assert_no_secret_fields(item, context)


def _safe_external_environment() -> dict[str, str]:
    """Return a local process environment with ambient credentials removed."""

    return {
        key: value
        for key, value in os.environ.items()
        if not SENSITIVE_ENV_RE.search(key)
        and key
        not in {
            "CLOUDFLARE_API_TOKEN",
            "CF_API_TOKEN",
            "CLOUDFLARE_TOKEN",
            "CF_TOKEN",
        }
    }


def _validate_command(value: Any, label: str) -> tuple[str, ...]:
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(part, str) or not part for part in value)
    ):
        raise GatewayError(f"{label} must be a non-empty JSON string array")
    for part in value:
        if re.search(
            r"(?:--?(?:token|secret|private[-_]?key|password)|"
            r"(?:token|secret|private[-_]?key|password)=)",
            part,
            re.IGNORECASE,
        ):
            raise GatewayError(f"{label} must not contain credential arguments")
    return tuple(value)


def _normalize_origin(value: Any) -> str:
    parsed = urlsplit(str(value or "").strip())
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.path not in ("", "/")
        or parsed.port not in (None, 443)
    ):
        raise GatewayError("publicOrigin must be a credential-free HTTPS origin")
    return "https://" + parsed.hostname.lower()


def _is_loopback(value: str) -> bool:
    if value.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(value).is_loopback
    except ValueError:
        return False


def _safe_repo_path(value: Any, *, allow_empty: bool = True) -> str:
    text = str(value or "")
    if not text:
        if allow_empty:
            return ""
        raise GatewayError("repository path is required")
    if (
        len(text.encode("utf-8")) > 1024
        or text.startswith("/")
        or "\x00" in text
        or "\r" in text
        or "\n" in text
        or "\\" in text
        or ":" in text
    ):
        raise GatewayError("invalid repository path")
    parts = text.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise GatewayError("invalid repository path")
    return text


def _safe_ref(value: Any) -> str:
    text = str(value or "").strip() or "HEAD"
    if (
        len(text) > 200
        or text.startswith("-")
        or ".." in text
        or "@{" in text
        or text.endswith(".")
        or not re.fullmatch(r"[A-Za-z0-9._/-]+", text)
    ):
        raise GatewayError("invalid repository ref")
    return text


def _decode_git_body(data: bytes, content_encoding: str) -> bytes:
    encodings = [
        item.strip().lower()
        for item in str(content_encoding or "").split(",")
        if item.strip()
    ]
    for encoding in reversed(encodings):
        if encoding == "identity":
            continue
        if encoding != "gzip":
            raise GatewayError("unsupported Git content encoding")
        try:
            with gzip.GzipFile(fileobj=io.BytesIO(data)) as stream:
                data = stream.read(MAX_REQUEST_BODY + 1)
        except (EOFError, OSError) as exc:
            raise GatewayError("invalid compressed Git request") from exc
        if len(data) > MAX_REQUEST_BODY:
            raise GatewayError("expanded Git request is too large")
    return data


def request_message(
    node: str,
    method: str,
    target: str,
    body_sha256: str,
    request_id: str,
    issued_at: int,
) -> str:
    """Canonical capability message shared with ``edge_routing.py``."""

    node = str(node or "").strip().lower()
    method = str(method or "").strip().upper()
    target = str(target or "")
    body_sha256 = str(body_sha256 or "").strip().lower()
    request_id = str(request_id or "").strip()
    try:
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if (
        not NODE_RE.fullmatch(node)
        or method not in ("GET", "HEAD", "POST")
        or not target.startswith("/")
        or "\r" in target
        or "\n" in target
        or not SHA256_RE.fullmatch(body_sha256)
        or not REQUEST_ID_RE.fullmatch(request_id)
        or issued_at <= 0
    ):
        return ""
    return "\n".join(
        (
            "forkmesh-masked-proxy-v1",
            node,
            method,
            target,
            body_sha256,
            request_id,
            str(issued_at),
        )
    )


def health_challenge(node: str, nonce: str, issued_at: int) -> str:
    node = str(node or "").strip().lower()
    nonce = str(nonce or "").strip()
    try:
        issued_at = int(issued_at)
    except (TypeError, ValueError):
        return ""
    if (
        not NODE_RE.fullmatch(node)
        or not re.fullmatch(r"[A-Za-z0-9_-]{16,128}", nonce)
        or issued_at <= 0
    ):
        return ""
    return "\n".join(
        ("forkmesh-https-health-v1", node, nonce, str(issued_at))
    )


def repository_health_challenge(
    node: str,
    nonce: str,
    issued_at: int,
    owner: str,
    repository: str,
    *,
    available: bool,
    integrity: str,
    refs_digest: str,
    operations_digest: str,
) -> str:
    """Canonical signed proof for one explicitly requested public repository."""

    base = health_challenge(node, nonce, issued_at)
    owner = str(owner or "").strip().lower()
    repository = str(repository or "").strip()
    integrity = str(integrity or "").strip().lower()
    refs_digest = str(refs_digest or "").strip().lower()
    operations_digest = str(operations_digest or "").strip().lower()
    if (
        not base
        or not NODE_RE.fullmatch(owner)
        or not REPO_RE.fullmatch(repository)
        or integrity not in ("ok", "unavailable")
        or not SHA256_RE.fullmatch(refs_digest)
        or not SHA256_RE.fullmatch(operations_digest)
        or (available and integrity != "ok")
        or (not available and integrity != "unavailable")
    ):
        return ""
    return "\n".join(
        (
            "forkmesh-https-health-repository-v1",
            node,
            nonce,
            str(int(issued_at)),
            owner,
            repository,
            "1" if available else "0",
            integrity,
            refs_digest,
            operations_digest,
        )
    )


class ExternalJSONCommand:
    """Direct, shell-free JSON adapter with redacted errors and bounded output."""

    def __init__(
        self,
        command: Iterable[str],
        *,
        runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    ) -> None:
        self.command = tuple(command)
        if not self.command:
            raise GatewayError("external command is required")
        self._runner = runner

    def call(self, request: dict[str, Any]) -> dict[str, Any]:
        try:
            completed = self._runner(
                list(self.command),
                input=_canonical_json(request) + "\n",
                text=True,
                capture_output=True,
                env=_safe_external_environment(),
                timeout=15,
                check=True,
            )
        except (OSError, subprocess.SubprocessError) as exc:
            raise GatewayError("external identity helper failed") from exc
        if len(completed.stdout.encode("utf-8", "replace")) > 16 * 1024:
            raise GatewayError("external identity helper returned too much data")
        try:
            response = json.loads(completed.stdout)
        except (json.JSONDecodeError, TypeError) as exc:
            raise GatewayError("external identity helper returned invalid JSON") from exc
        if not isinstance(response, dict):
            raise GatewayError("external identity helper returned a non-object")
        _assert_no_secret_fields(response, "external identity helper response")
        return response


class ExternalCapabilityVerifier:
    def __init__(
        self,
        command: Iterable[str],
        public_key: str,
        *,
        runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    ) -> None:
        self.public_key = public_key
        _base64url_decode(public_key, 32)
        self.helper = ExternalJSONCommand(command, runner=runner)

    def verify(self, message: str, signature: str) -> bool:
        try:
            _base64url_decode(signature, 64)
            payload = message.encode("utf-8")
            response = self.helper.call(
                {
                    "schemaVersion": 1,
                    "type": "forkmesh.request-capability-verification",
                    "algorithm": "Ed25519",
                    "encoding": "base64url-no-padding",
                    "publicKey": self.public_key,
                    "messageBase64": _base64url_encode(payload),
                    "messageSha256": hashlib.sha256(payload).hexdigest(),
                    "signature": signature,
                }
            )
            return (
                response.get("valid") is True
                and response.get("publicKey", self.public_key) == self.public_key
            )
        except GatewayError:
            return False


class ExternalHealthSigner:
    def __init__(
        self,
        command: Iterable[str],
        public_key: str,
        *,
        runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    ) -> None:
        self.public_key = public_key
        _base64url_decode(public_key, 32)
        self.helper = ExternalJSONCommand(command, runner=runner)

    def sign(self, message: str) -> str:
        payload = message.encode("utf-8")
        response = self.helper.call(
            {
                "schemaVersion": 1,
                "type": "forkmesh.health-challenge-signing",
                "algorithm": "Ed25519",
                "encoding": "base64url-no-padding",
                "publicKey": self.public_key,
                "messageBase64": _base64url_encode(payload),
                "messageSha256": hashlib.sha256(payload).hexdigest(),
            }
        )
        if response.get("publicKey", self.public_key) != self.public_key:
            raise GatewayError("health signer used a different public key")
        signature = str(response.get("signature") or "")
        _base64url_decode(signature, 64)
        return signature


@dataclass(frozen=True)
class EncryptedArchive:
    scheme: str
    ciphertext_path: Path
    ciphertext_sha256: str
    key_reference: str
    materialize_command: tuple[str, ...]


@dataclass(frozen=True)
class RepositoryConfig:
    owner: str
    name: str
    visibility: str
    enabled: bool
    git_dir: Path | None
    release_store: Path | None
    expected_refs_sha256: str
    operations: frozenset[str]
    encrypted_archive: EncryptedArchive | None = None


@dataclass(frozen=True)
class GatewayConfig:
    node: str
    public_key: str
    router_public_key: str
    public_origin: str
    listen_host: str
    listen_port: int
    manifest_path: Path
    verifier_command: tuple[str, ...]
    health_signer_command: tuple[str, ...]
    repositories: tuple[RepositoryConfig, ...]
    max_release_bytes: int = 2 * 1024 * 1024 * 1024
    private_replica_store: Path | None = None
    max_private_replica_bytes: int = 512 * 1024 * 1024


def _load_json_file(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GatewayError(f"{label} is not readable valid JSON") from exc
    if not isinstance(value, dict):
        raise GatewayError(f"{label} must be a JSON object")
    return value


def validate_manifest(
    manifest: dict[str, Any],
    *,
    node: str,
    public_key: str,
    public_origin: str,
) -> None:
    _assert_no_secret_fields(manifest, "mirror manifest")
    signature = manifest.get("signature")
    endpoint = manifest.get("endpoint")
    manifest_node = manifest.get("node")
    if (
        manifest.get("schemaVersion") != 1
        or manifest.get("type") != "forkmesh.mirror-endpoint"
        or not isinstance(signature, dict)
        or signature.get("algorithm") != "Ed25519"
        or signature.get("encoding") != "base64url-no-padding"
        or not isinstance(endpoint, dict)
        or endpoint.get("origin") != public_origin
        or endpoint.get("healthUrl") != public_origin + "/health"
        or endpoint.get("manifestUrl")
        != public_origin + "/forkmesh-mirror.json"
        or endpoint.get("transport") != "direct-https"
        or endpoint.get("mainProxyMode") != "masked"
        or not isinstance(manifest_node, dict)
        or manifest_node.get("name") != node
        or manifest_node.get("publicKey") != public_key
    ):
        raise GatewayError("mirror manifest does not match gateway identity/origin")
    _base64url_decode(str(signature.get("value") or ""), 64)
    unsigned = {key: value for key, value in manifest.items() if key != "signature"}
    payload_hash = hashlib.sha256(
        _canonical_json(unsigned).encode("utf-8")
    ).hexdigest()
    if signature.get("payloadSha256") != payload_hash:
        raise GatewayError("mirror manifest payload digest is invalid")


def load_config(path: Path) -> GatewayConfig:
    source = _load_json_file(path, "gateway configuration")
    _assert_no_secret_fields(source)
    if set(source) - {
        "schemaVersion",
        "node",
        "routerPublicKey",
        "publicOrigin",
        "listen",
        "manifestPath",
        "requestVerifierCommand",
        "healthSignerCommand",
        "repositories",
        "privateReplicaStore",
        "limits",
    }:
        raise GatewayError("gateway configuration contains an unknown field")
    if source.get("schemaVersion") != SCHEMA_VERSION:
        raise GatewayError("unsupported gateway configuration schemaVersion")
    base = path.resolve().parent
    node_value = source.get("node")
    if not isinstance(node_value, dict):
        raise GatewayError("node must be an object")
    if set(node_value) != {"name", "publicKey"}:
        raise GatewayError("node contains an unknown or missing field")
    node = str(node_value.get("name") or "").strip().lower()
    if not NODE_RE.fullmatch(node):
        raise GatewayError("node.name is invalid")
    public_key = str(node_value.get("publicKey") or "")
    router_public_key = str(source.get("routerPublicKey") or "")
    _base64url_decode(public_key, 32)
    _base64url_decode(router_public_key, 32)
    public_origin = _normalize_origin(source.get("publicOrigin"))

    listen = source.get("listen")
    if not isinstance(listen, dict):
        raise GatewayError("listen must be an object")
    if set(listen) != {"host", "port"}:
        raise GatewayError("listen contains an unknown or missing field")
    listen_host = str(listen.get("host") or "127.0.0.1")
    if not _is_loopback(listen_host):
        raise GatewayError("gateway must listen on loopback behind a TLS proxy")
    try:
        listen_port = int(listen.get("port"))
    except (TypeError, ValueError) as exc:
        raise GatewayError("listen.port is invalid") from exc
    if not 1 <= listen_port <= 65535:
        raise GatewayError("listen.port is outside 1..65535")

    manifest_raw = source.get("manifestPath")
    if not isinstance(manifest_raw, str) or not manifest_raw:
        raise GatewayError("manifestPath is required")
    manifest_path = (base / manifest_raw).resolve()
    validate_manifest(
        _load_json_file(manifest_path, "mirror manifest"),
        node=node,
        public_key=public_key,
        public_origin=public_origin,
    )

    verifier = _validate_command(
        source.get("requestVerifierCommand"), "requestVerifierCommand"
    )
    signer = _validate_command(
        source.get("healthSignerCommand"), "healthSignerCommand"
    )
    limits = source.get("limits", {})
    if not isinstance(limits, dict) or set(limits) - {
        "maxReleaseBytes", "maxPrivateReplicaBytes"
    }:
        raise GatewayError("limits contains an unknown field")
    try:
        max_release = int(
            limits.get(
                "maxReleaseBytes", 2 * 1024 * 1024 * 1024
            )
        )
    except (AttributeError, TypeError, ValueError) as exc:
        raise GatewayError("limits.maxReleaseBytes is invalid") from exc
    if not 1 <= max_release <= 16 * 1024 * 1024 * 1024:
        raise GatewayError("limits.maxReleaseBytes exceeds the safe range")
    try:
        max_private_replica = int(
            limits.get("maxPrivateReplicaBytes", 512 * 1024 * 1024)
        )
    except (AttributeError, TypeError, ValueError) as exc:
        raise GatewayError(
            "limits.maxPrivateReplicaBytes is invalid") from exc
    if not 1 <= max_private_replica <= 2 * 1024 * 1024 * 1024:
        raise GatewayError(
            "limits.maxPrivateReplicaBytes exceeds the safe range")

    private_replica_store = None
    private_store_raw = source.get("privateReplicaStore")
    if private_store_raw is not None:
        if not isinstance(private_store_raw, str) or not private_store_raw:
            raise GatewayError("privateReplicaStore must be a path string")
        private_replica_store = (base / private_store_raw).resolve()
        if (
            not private_replica_store.is_dir()
            or private_replica_store.is_symlink()
        ):
            raise GatewayError(
                "privateReplicaStore must be an existing real directory")
        mode = stat.S_IMODE(private_replica_store.stat().st_mode)
        if mode & (stat.S_IRWXG | stat.S_IRWXO):
            raise GatewayError(
                "privateReplicaStore must be accessible only to its owner")

    repositories_raw = source.get("repositories")
    if not isinstance(repositories_raw, list):
        raise GatewayError("repositories must be an array")
    repositories: list[RepositoryConfig] = []
    identities: set[tuple[str, str]] = set()
    for item in repositories_raw:
        if not isinstance(item, dict):
            raise GatewayError("repository entry must be an object")
        owner = str(item.get("owner") or "").strip().lower()
        name = str(item.get("name") or "").strip()
        if not NODE_RE.fullmatch(owner) or not REPO_RE.fullmatch(name):
            raise GatewayError("repository owner/name is invalid")
        identity = (owner, name.lower())
        if identity in identities:
            raise GatewayError("repository entries must be unique")
        identities.add(identity)
        visibility = str(item.get("visibility") or "private").lower()
        if visibility not in ("public", "private"):
            raise GatewayError("repository visibility must be public or private")
        enabled = item.get("enabled") is True
        if visibility != "public":
            if enabled:
                raise GatewayError("private repositories cannot be enabled publicly")
            if set(item) - {"owner", "name", "visibility", "enabled"}:
                raise GatewayError(
                    "private repository entry may contain identity fields only"
                )
            # Do not inspect or materialize private repository locations.
            repositories.append(
                RepositoryConfig(
                    owner,
                    name,
                    visibility,
                    False,
                    None,
                    None,
                    "",
                    frozenset(),
                )
            )
            continue
        if not enabled:
            if set(item) - {"owner", "name", "visibility", "enabled"}:
                raise GatewayError(
                    "disabled public repository may contain identity fields only"
                )
            repositories.append(
                RepositoryConfig(
                    owner,
                    name,
                    visibility,
                    False,
                    None,
                    None,
                    "",
                    frozenset(),
                )
            )
            continue

        integrity = item.get("integrity")
        if not isinstance(integrity, dict) or set(integrity) != {
            "expectedRefsSha256"
        }:
            raise GatewayError("repository integrity metadata is invalid")
        expected_refs = (
            str(integrity.get("expectedRefsSha256") or "").lower()
            if isinstance(integrity, dict)
            else ""
        )
        if not SHA256_RE.fullmatch(expected_refs):
            raise GatewayError(
                "enabled public repository requires integrity.expectedRefsSha256"
            )
        operations_raw = item.get("operations", sorted(PUBLIC_OPERATIONS))
        if (
            not isinstance(operations_raw, list)
            or not operations_raw
            or any(op not in PUBLIC_OPERATIONS for op in operations_raw)
        ):
            raise GatewayError("repository operations contain an unsupported value")
        operations = frozenset(operations_raw)

        git_dir: Path | None = None
        encrypted_archive: EncryptedArchive | None = None
        archive_raw = item.get("encryptedArchive")
        allowed_repository_fields = {
            "owner",
            "name",
            "visibility",
            "enabled",
            "integrity",
            "operations",
            "releaseStore",
            "encryptedArchive",
        }
        if "gitDir" in item:
            raise GatewayError(
                "plaintext gitDir is prohibited; enabled public mirrors "
                "require encryptedArchive storage"
            )
        if set(item) - allowed_repository_fields:
            raise GatewayError("repository entry contains an unknown field")
        if not archive_raw:
            raise GatewayError(
                "enabled public repository requires encryptedArchive storage"
            )
        if not isinstance(archive_raw, dict):
            raise GatewayError("encryptedArchive must be an object")
        if set(archive_raw) != {
            "scheme",
            "ciphertextPath",
            "ciphertextSha256",
            "keyReference",
            "materializeCommand",
        }:
            raise GatewayError(
                "encryptedArchive contains an unknown or missing field"
            )
        scheme = str(archive_raw.get("scheme") or "")
        if scheme != "age-encrypted-tar-v1":
            raise GatewayError(
                "encryptedArchive scheme must be age-encrypted-tar-v1"
            )
        ciphertext_raw = archive_raw.get("ciphertextPath")
        ciphertext_hash = str(
            archive_raw.get("ciphertextSha256") or ""
        ).lower()
        key_reference = str(archive_raw.get("keyReference") or "")
        if (
            not isinstance(ciphertext_raw, str)
            or not ciphertext_raw
            or not SHA256_RE.fullmatch(ciphertext_hash)
            or not re.fullmatch(r"[A-Za-z0-9._:/@+-]{3,240}", key_reference)
        ):
            raise GatewayError("encryptedArchive metadata is invalid")
        encrypted_archive = EncryptedArchive(
            scheme=scheme,
            ciphertext_path=(base / ciphertext_raw).resolve(),
            ciphertext_sha256=ciphertext_hash,
            key_reference=key_reference,
            materialize_command=_validate_command(
                archive_raw.get("materializeCommand"),
                "encryptedArchive.materializeCommand",
            ),
        )

        release_store = None
        if item.get("releaseStore"):
            if not isinstance(item["releaseStore"], str):
                raise GatewayError("releaseStore must be a path string")
            release_store = (base / item["releaseStore"]).resolve()
        repositories.append(
            RepositoryConfig(
                owner=owner,
                name=name,
                visibility=visibility,
                enabled=True,
                git_dir=git_dir,
                release_store=release_store,
                expected_refs_sha256=expected_refs,
                operations=operations,
                encrypted_archive=encrypted_archive,
            )
        )

    return GatewayConfig(
        node=node,
        public_key=public_key,
        router_public_key=router_public_key,
        public_origin=public_origin,
        listen_host=listen_host,
        listen_port=listen_port,
        manifest_path=manifest_path,
        verifier_command=verifier,
        health_signer_command=signer,
        repositories=tuple(repositories),
        max_release_bytes=max_release,
        private_replica_store=private_replica_store,
        max_private_replica_bytes=max_private_replica,
    )


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class ArchiveMaterializer:
    """Materialize an encrypted archive through an operator-owned crypto tool."""

    def __init__(
        self,
        *,
        runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
    ) -> None:
        self._runner = runner

    def materialize(self, archive: EncryptedArchive, destination: Path) -> Path:
        if archive.scheme != "age-encrypted-tar-v1":
            raise GatewayError(
                "repository archive must use age-encrypted-tar-v1")
        if not archive.ciphertext_path.is_file():
            raise GatewayError("encrypted repository archive is unavailable")
        if not _sha256_file(archive.ciphertext_path) == (
            archive.ciphertext_sha256
        ):
            raise GatewayError("encrypted repository archive digest mismatch")
        try:
            with archive.ciphertext_path.open("rb") as stream:
                header = stream.read(max(
                    len(AGE_NATIVE_HEADER), len(AGE_ARMORED_HEADER)))
        except OSError as exc:
            raise GatewayError(
                "encrypted repository archive is unavailable") from exc
        if not (
            header.startswith(AGE_NATIVE_HEADER)
            or header.startswith(AGE_ARMORED_HEADER)
        ):
            raise GatewayError(
                "encrypted repository archive is not an age file")
        request = {
            "schemaVersion": 1,
            "type": "forkmesh.repository-archive-materialize",
            "scheme": archive.scheme,
            "ciphertextPath": str(archive.ciphertext_path),
            "ciphertextSha256": archive.ciphertext_sha256,
            "keyReference": archive.key_reference,
            "destination": str(destination),
        }
        helper = ExternalJSONCommand(
            archive.materialize_command, runner=self._runner
        )
        response = helper.call(request)
        if response.get("ok") is not True:
            raise GatewayError("repository archive materialization was rejected")
        relative = _safe_repo_path(
            response.get("repositoryPath"), allow_empty=False
        )
        repository_path = (destination / relative).resolve()
        if destination.resolve() not in repository_path.parents:
            raise GatewayError("materialized repository escaped its destination")
        return repository_path


def _git_environment() -> dict[str, str]:
    environment = {
        "PATH": os.environ.get("PATH", ""),
        "LANG": "C",
        "LC_ALL": "C",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_TERMINAL_PROMPT": "0",
        "GIT_OPTIONAL_LOCKS": "0",
    }
    return environment


def _git_prefix(git_dir: Path) -> list[str]:
    # Command-line values override executable local configuration that could
    # otherwise start helper programs while serving a repository. Git ignores
    # repository-scoped uploadpack.packObjectsHook because it is protected
    # configuration; setting that key to an empty command makes upload-pack
    # try to execute an empty program instead of disabling the hook.
    return [
        "git",
        "-c",
        "core.alternateRefsCommand=/usr/bin/true",
        "-c",
        "core.fsmonitor=",
        "-c",
        "credential.helper=",
        "-c",
        "protocol.ext.allow=never",
        f"--git-dir={git_dir}",
    ]


def _run_git(
    git_dir: Path,
    arguments: Iterable[str],
    *,
    input_bytes: bytes | None = None,
    max_output: int = MAX_JSON_OUTPUT,
    timeout: int = PROCESS_TIMEOUT_SECONDS,
    allow_exit_one: bool = False,
) -> bytes:
    try:
        completed = subprocess.run(
            _git_prefix(git_dir) + list(arguments),
            input=input_bytes,
            capture_output=True,
            env=_git_environment(),
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise GitError("git operation unavailable") from exc
    if completed.returncode != 0 and not (
        allow_exit_one and completed.returncode == 1
    ):
        raise GitError("git operation failed")
    if len(completed.stdout) > max_output:
        raise GitError("git operation exceeded its output limit")
    return completed.stdout


def refs_canonical(git_dir: Path) -> str:
    output = _run_git(
        git_dir,
        [
            "for-each-ref",
            "--sort=refname",
            "--format=%(objectname) %(refname)",
            "refs/heads/",
            "refs/tags/",
        ],
        max_output=16 * 1024 * 1024,
    )
    return "\n".join(
        line
        for line in output.decode("utf-8", "replace").splitlines()
        if line
    )


def refs_sha256(git_dir: Path) -> str:
    return hashlib.sha256(refs_canonical(git_dir).encode("utf-8")).hexdigest()


def private_replica_file(
    store: Path | None, opaque_id: str, max_bytes: int
) -> tuple[Path, int, str]:
    """Validate one Qt PrivateMirrorStore ciphertext without decrypting it."""
    if store is None or not OPAQUE_REPLICA_RE.fullmatch(str(opaque_id or "")):
        raise GatewayError("private replica was not found")
    root = store.resolve()
    path = root / (opaque_id + ".fm-private")
    try:
        if path.is_symlink() or not path.is_file() or path.parent.resolve() != root:
            raise GatewayError("private replica was not found")
        size = path.stat().st_size
        if size <= 0 or size > max_bytes:
            raise GatewayError("private replica was not found")
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as exc:
        raise GatewayError("private replica was not found") from exc
    if not isinstance(value, dict) or set(value) != {
        "schemaVersion", "kind", "opaqueId", "keyEpoch", "createdAt",
        "updatedAt", "ciphertextSha256", "envelope",
    }:
        raise GatewayError("private replica was not found")
    envelope = value.get("envelope")
    if (
        value.get("schemaVersion") != 1
        or value.get("kind") != "forkmesh.private-replica"
        or value.get("opaqueId") != opaque_id
        or isinstance(value.get("keyEpoch"), bool)
        or not isinstance(value.get("keyEpoch"), int)
        or value.get("keyEpoch") <= 0
        or isinstance(value.get("createdAt"), bool)
        or not isinstance(value.get("createdAt"), int)
        or isinstance(value.get("updatedAt"), bool)
        or not isinstance(value.get("updatedAt"), int)
        or value.get("createdAt") <= 0
        or value.get("updatedAt") < value.get("createdAt")
        or not SHA256_RE.fullmatch(
            str(value.get("ciphertextSha256") or ""))
        or not isinstance(envelope, dict)
        or set(envelope) != {
            "kind", "v", "alg", "nonce", "tag", "body", "recipients",
        }
        or envelope.get("kind") != "forkmesh.mirror"
        or envelope.get("v") != 1
        or envelope.get("alg") != "x25519+mlkem768/aes256gcm"
    ):
        raise GatewayError("private replica was not found")
    recipients = envelope.get("recipients")
    if not isinstance(recipients, list) or not 1 <= len(recipients) <= 256:
        raise GatewayError("private replica was not found")
    b64_re = re.compile(r"^[A-Za-z0-9+/]*={0,2}$")
    if any(
        not isinstance(envelope.get(field), str)
        or not envelope.get(field)
        or not b64_re.fullmatch(envelope[field])
        for field in ("nonce", "tag", "body")
    ):
        raise GatewayError("private replica was not found")
    seen = set()
    for recipient in recipients:
        if not isinstance(recipient, dict) or set(recipient) != {
            "kid", "x25519", "mlkem768", "nonce", "tag", "key",
        }:
            raise GatewayError("private replica was not found")
        kid = str(recipient.get("kid") or "")
        if (
            not SHA256_RE.fullmatch(kid)
            or kid in seen
            or any(
                not isinstance(recipient.get(field), str)
                or not recipient.get(field)
                or not b64_re.fullmatch(recipient[field])
                for field in ("x25519", "mlkem768", "nonce", "tag", "key")
            )
        ):
            raise GatewayError("private replica was not found")
        seen.add(kid)
    actual_digest = hashlib.sha256(
        _canonical_json(envelope).encode("utf-8")
    ).hexdigest()
    if not hmac.compare_digest(
        actual_digest, str(value["ciphertextSha256"])):
        raise GatewayError("private replica was not found")
    return path, size, hashlib.sha256(raw).hexdigest()


def _ensure_bare_repository(git_dir: Path) -> None:
    if not git_dir.is_dir():
        raise GatewayError("configured public mirror repository is unavailable")
    try:
        value = _run_git(
            git_dir, ["rev-parse", "--is-bare-repository"], max_output=128
        )
    except GitError as exc:
        raise GatewayError("configured public mirror is not a Git repository") from exc
    if value.strip() != b"true":
        raise GatewayError("configured mirror must be a bare Git repository")


def _safe_analysis_path(value: Any) -> str:
    """Normalize an artifact/import path without allowing repository escape."""

    raw = str(value or "").strip().replace("\\", "/")
    if not raw or "\x00" in raw or "\r" in raw or "\n" in raw:
        return ""
    raw = raw.split("#", 1)[0].split("?", 1)[0]
    if raw.startswith("file://"):
        raw = raw[7:]
    normalized = posixpath.normpath(raw).lstrip("/")
    if normalized in ("", ".") or normalized == ".." or normalized.startswith("../"):
        return ""
    return normalized[:500]


def _extract_dependency_specs(path: str, text: str) -> list[str]:
    """Return bounded source-level import/include targets.

    Only syntax that names another source path/module is considered. Package
    registry coordinates remain manifest concerns and are deliberately not
    turned into fake file edges.
    """

    suffix = Path(path).suffix.lower()
    specs: list[str] = []

    def add(value: Any) -> None:
        value = str(value or "").strip()
        if value and value not in specs and len(specs) < 80:
            specs.append(value)

    if suffix in {".js", ".jsx", ".mjs", ".ts", ".tsx"}:
        patterns = (
            r"\b(?:from|import)\s*\(?\s*[\"']([^\"']+)[\"']",
            r"\brequire\s*\(\s*[\"']([^\"']+)[\"']\s*\)",
            r"\bimport\s*\(\s*[\"']([^\"']+)[\"']\s*\)",
        )
        for pattern in patterns:
            for match in re.finditer(pattern, text):
                add(match.group(1))
    elif suffix == ".py":
        for match in re.finditer(
            r"(?m)^\s*from\s+([.A-Za-z_][A-Za-z0-9_.]*)\s+import\s+"
            r"([A-Za-z_][A-Za-z0-9_, ]*)",
            text,
        ):
            module = match.group(1)
            if module.startswith("."):
                dots = len(module) - len(module.lstrip("."))
                prefix = "../" * max(0, dots - 1)
                local_module = module[dots:].replace(".", "/")
                if local_module:
                    add(prefix + local_module)
                else:
                    for imported in match.group(2).split(","):
                        add(prefix + "./" + imported.strip().split()[0])
            else:
                add(module.replace(".", "/"))
        for match in re.finditer(
            r"(?m)^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)", text
        ):
            add(match.group(1).replace(".", "/"))
    elif suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}:
        for match in re.finditer(
            r"(?m)^\s*#\s*include\s*\"([^\"\r\n]+)\"", text
        ):
            add(match.group(1))
    elif suffix == ".rs":
        for match in re.finditer(
            r"(?m)^\s*(?:pub\s+)?mod\s+([A-Za-z_][A-Za-z0-9_]*)\s*;",
            text,
        ):
            add("./" + match.group(1))
        for match in re.finditer(
            r"(?m)^\s*use\s+crate::([A-Za-z_][A-Za-z0-9_:]*)", text
        ):
            add(match.group(1).replace("::", "/"))
    elif suffix in {".java", ".kt"}:
        for match in re.finditer(
            r"(?m)^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)\s*;?", text
        ):
            add(match.group(1).replace(".", "/"))
    elif suffix == ".dart":
        for match in re.finditer(
            r"\b(?:import|export|part)\s+[\"']([^\"']+)[\"']", text
        ):
            add(match.group(1))
    elif suffix == ".rb":
        for match in re.finditer(
            r"\brequire_relative\s*[ \(]?\s*[\"']([^\"']+)[\"']", text
        ):
            add(match.group(1))
    elif suffix in {".css", ".scss"}:
        for match in re.finditer(
            r"@(?:import|use|forward)\s+(?:url\()?\s*[\"']([^\"']+)[\"']",
            text,
        ):
            add(match.group(1))
    elif suffix == ".go":
        for block in re.finditer(
            r"(?ms)^\s*import\s*(?:\((.*?)\)|[\"']([^\"']+)[\"'])", text
        ):
            if block.group(2):
                add(block.group(2))
            else:
                for match in re.finditer(r"[\"']([^\"']+)[\"']", block.group(1)):
                    add(match.group(1))
    elif suffix in {".php", ".cs", ".swift"}:
        for match in re.finditer(
            r"(?m)^\s*(?:require_once|require|include|using|import)\s*"
            r"(?:\(\s*)?[\"']?([^\"';\r\n)]+)",
            text,
        ):
            add(match.group(1).strip())
    return specs


def _dependency_target(
    source: str,
    spec: str,
    known_paths: set[str],
    suffix_index: Mapping[str, list[str]],
) -> str:
    """Resolve one import to a committed repository file, or return empty."""

    raw = str(spec or "").strip().replace("\\", "/")
    if (
        not raw
        or raw.startswith(
            ("//", "http:", "https:", "data:", "node:", "dart:")
        )
        or "\x00" in raw
    ):
        return ""
    source_dir = posixpath.dirname(source)
    relative = raw.startswith(".")
    source_suffix = Path(source).suffix.lower()
    # Bare JavaScript/TypeScript specifiers use package or project-alias
    # resolution. Without a committed resolver configuration, treating a
    # same-named source file as the target would manufacture an edge.
    if (
        source_suffix in {".js", ".jsx", ".mjs", ".ts", ".tsx"}
        and not relative
    ):
        return ""
    if relative:
        base = _safe_analysis_path(posixpath.join(source_dir, raw))
    else:
        base = _safe_analysis_path(raw)
    if not base:
        return ""

    extensions = [
        source_suffix,
        ".ts", ".tsx", ".js", ".jsx", ".mjs", ".py", ".rs", ".go",
        ".java", ".kt", ".dart", ".rb", ".php", ".c", ".cc", ".cpp",
        ".h", ".hpp", ".cs", ".swift", ".css", ".scss",
    ]
    candidates = [base]
    if Path(base).suffix.lower() not in ANALYZED_SOURCE_SUFFIXES:
        candidates.extend(base + extension for extension in extensions)
        candidates.extend(
            posixpath.join(base, name)
            for name in (
                "__init__.py", "index.ts", "index.tsx", "index.js",
                "index.jsx", "mod.rs", "lib.rs",
            )
        )
    for candidate in candidates:
        if candidate in known_paths and candidate != source:
            return candidate

    # Absolute module/package imports often include a configured project or
    # Java package prefix. Resolve only a unique committed suffix; ambiguity
    # stays unresolved instead of manufacturing an edge.
    if not relative:
        for candidate in candidates:
            matches = suffix_index.get(candidate, [])
            if len(matches) == 1 and matches[0] != source:
                return matches[0]
    return ""


def _match_coverage_path(raw: Any, known_paths: set[str]) -> str:
    candidate = _safe_analysis_path(raw)
    if candidate in known_paths:
        return candidate
    if not candidate:
        return ""
    matches = [
        path
        for path in known_paths
        if candidate.endswith("/" + path) or path.endswith("/" + candidate)
    ]
    return matches[0] if len(matches) == 1 else ""


def _percentage(covered: Any, total: Any) -> float | None:
    try:
        covered_number = max(0.0, float(covered))
        total_number = max(0.0, float(total))
    except (TypeError, ValueError):
        return None
    if total_number <= 0:
        return None
    return round(min(100.0, covered_number * 100.0 / total_number), 1)


def _parse_json_coverage(
    payload: bytes, known_paths: set[str]
) -> dict[str, float]:
    try:
        value = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError):
        return {}
    if not isinstance(value, dict):
        return {}
    files = value.get("files")
    records = files if isinstance(files, dict) else value
    result: dict[str, float] = {}
    for raw_path, record in list(records.items())[:10_000]:
        if not isinstance(record, dict):
            continue
        path = _match_coverage_path(raw_path, known_paths)
        if not path:
            continue
        percent: float | None = None
        summary = record.get("summary")
        if isinstance(summary, dict):
            percent_value = summary.get(
                "percent_covered",
                summary.get("percentCovered", summary.get("percent")),
            )
            try:
                percent = round(float(percent_value), 1)
            except (TypeError, ValueError):
                percent = _percentage(
                    summary.get("covered_lines", summary.get("covered")),
                    summary.get("num_statements", summary.get("total")),
                )
        statement_counts = record.get("s")
        if percent is None and isinstance(statement_counts, dict):
            counts = [
                count for count in statement_counts.values()
                if isinstance(count, (int, float)) and not isinstance(count, bool)
            ]
            percent = _percentage(
                sum(1 for count in counts if count > 0), len(counts)
            )
        lines = record.get("lines")
        if percent is None and isinstance(lines, dict):
            percent = _percentage(lines.get("covered"), lines.get("total"))
            try:
                if percent is None and "pct" in lines:
                    percent = round(float(lines["pct"]), 1)
            except (TypeError, ValueError):
                pass
        if percent is not None and 0 <= percent <= 100:
            result[path] = percent
    return result


def _parse_lcov_coverage(
    payload: bytes, known_paths: set[str]
) -> dict[str, float]:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        return {}
    result: dict[str, float] = {}
    current = ""
    found = hit = 0
    for line in text.splitlines()[:200_000]:
        if line.startswith("SF:"):
            current = _match_coverage_path(line[3:], known_paths)
            found = hit = 0
        elif line.startswith("DA:") and current:
            parts = line[3:].split(",", 2)
            if len(parts) >= 2:
                found += 1
                try:
                    hit += int(parts[1]) > 0
                except ValueError:
                    pass
        elif line.startswith("LF:") and current:
            try:
                found = max(found, int(line[3:]))
            except ValueError:
                pass
        elif line.startswith("LH:") and current:
            try:
                hit = max(hit, int(line[3:]))
            except ValueError:
                pass
        elif line == "end_of_record" and current:
            percent = _percentage(hit, found)
            if percent is not None:
                result[current] = percent
            current = ""
    if current:
        percent = _percentage(hit, found)
        if percent is not None:
            result[current] = percent
    return result


def _parse_xml_coverage(
    payload: bytes, known_paths: set[str]
) -> dict[str, float]:
    """Parse common Cobertura/JaCoCo file counters without an XML entity parser."""

    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        return {}
    result: dict[str, float] = {}
    # Cobertura class records usually carry a direct line-rate.
    for match in itertools.islice(re.finditer(
        r"<class\b[^>]*\bfilename=[\"']([^\"']+)[\"'][^>]*"
        r"\bline-rate=[\"']([0-9.]+)[\"']",
        text,
        re.IGNORECASE,
    ), 10_000):
        path = _match_coverage_path(match.group(1), known_paths)
        try:
            percent = round(float(match.group(2)) * 100.0, 1)
        except ValueError:
            continue
        if path and 0 <= percent <= 100:
            result[path] = percent
    # JaCoCo sourcefile records expose missed/covered LINE counters.
    for package in itertools.islice(re.finditer(
        r"<package\b[^>]*\bname=[\"']([^\"']*)[\"'][^>]*>(.*?)</package>",
        text,
        re.IGNORECASE | re.DOTALL,
    ), 2_000):
        prefix = package.group(1).strip("/")
        for source in itertools.islice(re.finditer(
            r"<sourcefile\b[^>]*\bname=[\"']([^\"']+)[\"'][^>]*>"
            r"(.*?)</sourcefile>",
            package.group(2),
            re.IGNORECASE | re.DOTALL,
        ), 10_000):
            path = _match_coverage_path(
                posixpath.join(prefix, source.group(1)), known_paths
            )
            counter = re.search(
                r"<counter\b[^>]*\btype=[\"']LINE[\"'][^>]*"
                r"\bmissed=[\"']([0-9]+)[\"'][^>]*"
                r"\bcovered=[\"']([0-9]+)[\"']",
                source.group(2),
                re.IGNORECASE,
            )
            if not path or not counter:
                continue
            missed = int(counter.group(1))
            covered = int(counter.group(2))
            percent = _percentage(covered, missed + covered)
            if percent is not None:
                result[path] = percent
    return result


def _dependency_depths(
    adjacency: Mapping[str, list[str]], maximum: int = 12
) -> dict[str, int]:
    """Compute bounded transitive outbound depth with cycle-safe DFS."""

    memo: dict[str, int] = {}
    visiting: set[str] = set()

    def depth(path: str) -> int:
        if path in memo:
            return memo[path]
        if path in visiting:
            return 0
        visiting.add(path)
        value = 0
        for target in adjacency.get(path, []):
            value = max(value, min(maximum, 1 + depth(target)))
            if value >= maximum:
                break
        visiting.discard(path)
        memo[path] = value
        return value

    for path in adjacency:
        depth(path)
    return memo


class GitRepository:
    def __init__(self, config: RepositoryConfig, git_dir: Path) -> None:
        self.config = config
        self.git_dir = git_dir
        self._integrity_lock = threading.Lock()
        self._integrity_checked_at = 0.0
        self._integrity_ok = False
        self._analysis_lock = threading.Lock()
        self._analysis_commit = ""
        self._analysis_value: dict[str, Any] = {}
        _ensure_bare_repository(git_dir)
        self.check_integrity(force=True)

    def check_integrity(self, *, force: bool = False) -> bool:
        with self._integrity_lock:
            now = time.monotonic()
            if not force and now - self._integrity_checked_at < 15:
                return self._integrity_ok
            try:
                actual = refs_sha256(self.git_dir)
                self._integrity_ok = hmac.compare_digest(
                    actual, self.config.expected_refs_sha256
                )
            except (GitError, OSError):
                self._integrity_ok = False
            self._integrity_checked_at = now
            return self._integrity_ok

    def resolve_commit(self, requested: Any = "") -> str:
        ref = _safe_ref(requested)
        candidates = [ref]
        if ref != "HEAD" and not ref.startswith("refs/"):
            candidates = [
                "refs/heads/" + ref,
                "refs/remotes/origin/" + ref,
                ref,
            ]
        for candidate in candidates:
            try:
                output = _run_git(
                    self.git_dir,
                    [
                        "rev-parse",
                        "--verify",
                        "--quiet",
                        "--end-of-options",
                        candidate + "^{commit}",
                    ],
                    max_output=128,
                )
            except GitError:
                continue
            commit = output.decode("ascii", "ignore").strip().lower()
            if re.fullmatch(r"[0-9a-f]{40,64}", commit):
                return commit
        raise GitError("repository ref was not found")

    def _commit_summary(self, commit: str, path: str = "") -> dict[str, Any]:
        args = [
            "log",
            "-n",
            "20",
            "--date=format:%Y-%m-%d",
            "--format=%H%x1f%an%x1f%ad%x1f%s",
            commit,
        ]
        if path:
            args += ["--", path]
        output = _run_git(self.git_dir, args, max_output=128 * 1024)
        rows = output.decode("utf-8", "replace").splitlines()
        if not rows:
            return {}
        fields = rows[0].split("\x1f", 3)
        if len(fields) != 4:
            return {}
        return {
            "hash": fields[0],
            "commit": fields[0],
            "commitHash": fields[0],
            "author": fields[1],
            "date": fields[2],
            "subject": fields[3],
            "message": fields[3],
            "commitMessage": fields[3],
            "changeCount": len(rows),
            "changeCountCapped": len(rows) >= 20,
        }

    def _read_analysis_blobs(
        self,
        commit: str,
        blobs: list[tuple[int, str]],
    ) -> tuple[dict[str, bytes], bool]:
        """Read selected committed blobs through one bounded cat-file process."""

        selected: list[tuple[int, str]] = []
        total = 0
        coverage_candidates = sorted(
            (
                (size, path)
                for size, path in blobs
                if path.rsplit("/", 1)[-1].lower() in COVERAGE_ARTIFACT_NAMES
                and size <= MAX_COVERAGE_ARTIFACT_BYTES
            ),
            key=lambda item: (
                0 if item[1].lower().endswith("lcov.info") else 1,
                item[1],
            ),
        )
        source_candidates = sorted(
            (
                (size, path)
                for size, path in blobs
                if Path(path).suffix.lower() in ANALYZED_SOURCE_SUFFIXES
                and size <= MAX_ANALYSIS_FILE_BYTES
            ),
            key=lambda item: item[1],
        )
        for size, path in coverage_candidates + source_candidates:
            if (
                len(selected) >= MAX_ANALYSIS_FILES
                or total + size > MAX_ANALYSIS_TOTAL_BYTES
                or any(character in path for character in ("\x00", "\r", "\n"))
            ):
                continue
            selected.append((size, path))
            total += size
        partial = len(source_candidates) > sum(
            Path(path).suffix.lower() in ANALYZED_SOURCE_SUFFIXES
            for _size, path in selected
        )
        if not selected:
            return {}, partial
        request = b"".join(
            f"{commit}:{path}\n".encode("utf-8") for _size, path in selected
        )
        output = _run_git(
            self.git_dir,
            ["cat-file", "--batch"],
            input_bytes=request,
            max_output=MAX_ANALYSIS_TOTAL_BYTES + len(selected) * 160,
        )
        cursor = 0
        result: dict[str, bytes] = {}
        for _expected_size, path in selected:
            line_end = output.find(b"\n", cursor)
            if line_end < 0:
                break
            header = output[cursor:line_end].split()
            cursor = line_end + 1
            if len(header) != 3 or header[1] != b"blob":
                continue
            try:
                size = int(header[2])
            except ValueError:
                break
            end = cursor + size
            if size < 0 or end > len(output):
                break
            result[path] = output[cursor:end]
            cursor = end + 1
        return result, partial

    def _build_analysis(self, commit: str) -> dict[str, Any]:
        all_blobs = self._all_blobs(commit)
        repository_partial = len(all_blobs) > MAX_ANALYSIS_REPOSITORY_FILES
        blobs = all_blobs[:MAX_ANALYSIS_REPOSITORY_FILES]
        known_paths = {path for _size, path in blobs}
        raw_blobs, partial = self._read_analysis_blobs(commit, blobs)
        partial = partial or repository_partial
        source_text: dict[str, str] = {}
        for path, payload in raw_blobs.items():
            if Path(path).suffix.lower() not in ANALYZED_SOURCE_SUFFIXES:
                continue
            try:
                source_text[path] = payload.decode("utf-8")
            except UnicodeDecodeError:
                continue

        suffix_index: dict[str, list[str]] = {}
        for path in known_paths:
            parts = path.split("/")
            for start in range(len(parts)):
                suffix = "/".join(parts[start:])
                suffix_index.setdefault(suffix, []).append(path)
        adjacency: dict[str, list[str]] = {path: [] for path in source_text}
        edge_count = 0
        for source, text in source_text.items():
            for spec in _extract_dependency_specs(source, text):
                target = _dependency_target(
                    source, spec, known_paths, suffix_index
                )
                if (
                    target
                    and target not in adjacency[source]
                    and edge_count < MAX_DEPENDENCY_EDGES
                ):
                    adjacency[source].append(target)
                    edge_count += 1
        depths = _dependency_depths(adjacency)

        coverage: dict[str, float] = {}
        artifacts: list[str] = []
        for path, payload in raw_blobs.items():
            name = path.rsplit("/", 1)[-1].lower()
            if name not in COVERAGE_ARTIFACT_NAMES:
                continue
            if name.endswith(".json"):
                parsed = _parse_json_coverage(payload, known_paths)
            elif name == "lcov.info":
                parsed = _parse_lcov_coverage(payload, known_paths)
            else:
                parsed = _parse_xml_coverage(payload, known_paths)
            if parsed:
                artifacts.append(path)
                for source_path, percent in parsed.items():
                    coverage.setdefault(source_path, percent)

        return {
            "commit": commit,
            "adjacency": adjacency,
            "depths": depths,
            "coverage": coverage,
            "summary": {
                "commit": commit,
                "dependency": {
                    "status": "partial" if partial else "complete",
                    "filesParsed": len(source_text),
                    "edgeCount": edge_count,
                    "maxFiles": MAX_ANALYSIS_FILES,
                    "maxRepositoryFiles": MAX_ANALYSIS_REPOSITORY_FILES,
                    "maxBytes": MAX_ANALYSIS_TOTAL_BYTES,
                },
                "coverage": {
                    "status": "available" if artifacts else "unavailable",
                    "artifacts": artifacts[:12],
                    "files": len(coverage),
                },
            },
        }

    def _analysis(self, commit: str) -> dict[str, Any]:
        with self._analysis_lock:
            if self._analysis_commit == commit and self._analysis_value:
                return self._analysis_value
            value = self._build_analysis(commit)
            self._analysis_commit = commit
            self._analysis_value = value
            return value

    @staticmethod
    def _analysis_child(path: str, candidate: str) -> str:
        prefix = path + "/" if path else ""
        if prefix and not candidate.startswith(prefix):
            return ""
        remainder = candidate[len(prefix):] if prefix else candidate
        first = remainder.split("/", 1)[0]
        return prefix + first if first else ""

    def _tree_analysis_fields(
        self,
        analysis: Mapping[str, Any],
        directory: str,
        full_path: str,
        object_type: str,
    ) -> dict[str, Any]:
        adjacency = analysis.get("adjacency", {})
        depths = analysis.get("depths", {})
        coverage = analysis.get("coverage", {})
        if object_type == "blob":
            return {
                "path": full_path,
                "dependencies": list(adjacency.get(full_path, []))[:80],
                "dependencyDepth": int(depths.get(full_path, 0)),
                "coverage": coverage.get(full_path),
                "analysisCommit": analysis.get("commit", ""),
            }

        prefix = full_path + "/"
        descendants = [
            source for source in adjacency if source.startswith(prefix)
        ]
        dependencies: set[str] = set()
        for source in descendants:
            for target in adjacency.get(source, []):
                child = self._analysis_child(directory, target)
                if child and child != full_path:
                    dependencies.add(child)
        covered = [
            float(percent)
            for source, percent in coverage.items()
            if source.startswith(prefix)
        ]
        return {
            "path": full_path,
            "dependencies": sorted(dependencies)[:80],
            "dependencyDepth": max(
                (int(depths.get(source, 0)) for source in descendants),
                default=0,
            ),
            "coverage": (
                round(sum(covered) / len(covered), 1) if covered else None
            ),
            "analysisCommit": analysis.get("commit", ""),
        }

    def tree(self, query: Mapping[str, str]) -> dict[str, Any]:
        path = _safe_repo_path(query.get("path", ""))
        commit = self.resolve_commit(query.get("ref", ""))
        analysis = self._analysis(commit)
        treeish = commit if not path else f"{commit}:{path}"
        output = _run_git(
            self.git_dir,
            ["ls-tree", "-l", "-z", treeish],
            max_output=16 * 1024 * 1024,
        )
        entries = []
        records = output.split(b"\0")
        if len(records) > MAX_TREE_ENTRIES + 1:
            records = records[:MAX_TREE_ENTRIES]
            truncated = True
        else:
            truncated = False
        for record in records:
            if not record or b"\t" not in record:
                continue
            metadata, raw_name = record.split(b"\t", 1)
            fields = metadata.split()
            if len(fields) < 4:
                continue
            name = raw_name.decode("utf-8", "replace")
            full_path = name if not path else path + "/" + name
            try:
                size = int(fields[3]) if fields[3] != b"-" else 0
            except ValueError:
                size = 0
            entry = {
                "name": name,
                "type": fields[1].decode("ascii", "replace"),
                "size": max(0, size),
            }
            entry.update(
                self._tree_analysis_fields(
                    analysis, path, full_path, entry["type"]
                )
            )
            entry.update(self._commit_summary(commit, full_path))
            entries.append(entry)
        return {
            "ok": True,
            "commit": commit,
            "entries": entries,
            "latestCommit": self._commit_summary(commit),
            "analysis": analysis["summary"],
            "truncated": truncated,
        }

    def blob(self, query: Mapping[str, str]) -> dict[str, Any]:
        path = _safe_repo_path(query.get("path"), allow_empty=False)
        commit = self.resolve_commit(query.get("ref", ""))
        output = _run_git(
            self.git_dir,
            ["cat-file", "blob", f"{commit}:{path}"],
            max_output=MAX_JSON_BLOB + 1,
        )
        truncated = len(output) > MAX_JSON_BLOB
        output = output[:MAX_JSON_BLOB]
        try:
            content = output.decode("utf-8")
            encoding = "utf8"
        except UnicodeDecodeError:
            content = base64.b64encode(output).decode("ascii")
            encoding = "base64"
        return {
            "ok": True,
            "size": len(output),
            "truncated": truncated,
            "encoding": encoding,
            "content": content,
        }

    def blobs(self, paths: Iterable[str], ref: str = "") -> dict[str, Any]:
        """Bounded compatibility batch for the repository web UI."""
        output: dict[str, Any] = {}
        used = 0
        truncated = False
        for path in list(paths)[:60]:
            try:
                result = self.blob({"path": path, "ref": ref})
            except (GatewayError, GitError):
                result = None
            encoded_size = len(
                json.dumps(
                    result, sort_keys=True, separators=(",", ":"),
                    ensure_ascii=False,
                ).encode("utf-8")
            )
            if used + encoded_size > MAX_JSON_OUTPUT:
                output[path] = None
                truncated = True
                continue
            output[path] = result
            used += encoded_size
        return {"ok": True, "blobs": output, "truncated": truncated}

    def raw_spec(self, query: Mapping[str, str]) -> "StreamSpec":
        path = _safe_repo_path(query.get("path"), allow_empty=False)
        commit = self.resolve_commit(query.get("ref", ""))
        object_name = f"{commit}:{path}"
        object_type = _run_git(
            self.git_dir,
            ["cat-file", "-t", object_name],
            max_output=64,
        ).strip()
        if object_type != b"blob":
            raise GitError("repository object is not a blob")
        size_raw = _run_git(
            self.git_dir,
            ["cat-file", "-s", object_name],
            max_output=64,
        )
        try:
            size = int(size_raw)
        except ValueError as exc:
            raise GitError("repository object has an invalid size") from exc
        guessed_type = mimetypes.guess_type(path)[0] or ""
        active_types = {
            "image/svg+xml",
            "text/html",
            "application/xhtml+xml",
            "application/xml",
            "text/xml",
            "application/javascript",
            "text/javascript",
        }
        if guessed_type in active_types:
            content_type = "application/octet-stream"
            disposition = "attachment"
        elif guessed_type.startswith("text/"):
            content_type = "text/plain; charset=utf-8"
            disposition = "inline"
        else:
            content_type = guessed_type or "application/octet-stream"
            disposition = "inline"
        return StreamSpec(
            kind="process",
            content_type=content_type,
            content_length=size,
            command=tuple(
                _git_prefix(self.git_dir) + ["cat-file", "blob", object_name]
            ),
            filename=Path(path).name,
            disposition=disposition,
        )

    def history(self, query: Mapping[str, str]) -> dict[str, Any]:
        commit = self.resolve_commit(query.get("ref", ""))
        output = _run_git(
            self.git_dir,
            [
                "log",
                "--date=format:%Y-%m-%d",
                "-n",
                "60",
                "--format=%H%x1f%an%x1f%ad%x1f%s",
                commit,
            ],
            max_output=512 * 1024,
        )
        commits = []
        for row in output.decode("utf-8", "replace").splitlines():
            fields = row.split("\x1f", 3)
            if len(fields) == 4:
                commits.append(
                    {
                        "hash": fields[0],
                        "author": fields[1],
                        "date": fields[2],
                        "subject": fields[3],
                    }
                )
        return {"ok": True, "commits": commits}

    def commit(self, query: Mapping[str, str]) -> dict[str, Any]:
        requested = str(query.get("path") or "").lower()
        if not COMMIT_RE.fullmatch(requested):
            raise GatewayError("invalid commit hash")
        commit = self.resolve_commit(requested)
        metadata = _run_git(
            self.git_dir,
            [
                "show",
                "-s",
                "--date=format:%Y-%m-%d %H:%M",
                "--format=%H%x1f%an%x1f%ad%x1f%P%x1f%s%x1f%b",
                commit,
            ],
            max_output=512 * 1024,
        ).decode("utf-8", "replace")
        fields = metadata.split("\x1f", 5)
        if len(fields) < 5:
            raise GitError("commit metadata is invalid")
        parents = fields[3].strip().split()
        base = (
            parents[0]
            if parents
            else "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
        )
        numstat = _run_git(
            self.git_dir,
            ["diff", "--numstat", base, commit],
            max_output=2 * 1024 * 1024,
        )
        files = []
        for row in numstat.decode("utf-8", "replace").splitlines()[:1000]:
            parts = row.split("\t", 2)
            if len(parts) == 3:
                files.append(
                    {"path": parts[2], "adds": parts[0], "dels": parts[1]}
                )
        diff = _run_git(
            self.git_dir,
            ["diff", "-M", "--no-color", base, commit],
            max_output=2 * 1024 * 1024,
        )
        return {
            "ok": True,
            "hash": fields[0],
            "author": fields[1],
            "date": fields[2],
            "parents": parents,
            "subject": fields[4],
            "body": fields[5] if len(fields) > 5 else "",
            "files": files,
            "diff": diff.decode("utf-8", "replace"),
            "diffTruncated": len(diff) >= 2 * 1024 * 1024,
        }

    def compare(self, query: Mapping[str, str]) -> dict[str, Any]:
        """Return one immutable, portable pull-request change set.

        The browser cannot safely hand the owner only mutable branch names:
        by the time the inbox drains, either ref may have moved or may not
        exist on the owner's node. Resolve both names once, then carry the
        exact binary patch and (for linear histories) authored commit series
        that the submitter signs. Merge commits fall back to the net patch
        because ``format-patch`` intentionally omits merge commits and could
        otherwise describe a different tree.
        """
        if not query.get("base") or not query.get("head"):
            raise GatewayError("repository comparison requires base and head")
        base = self.resolve_commit(query["base"])
        head = self.resolve_commit(query["head"])
        merge_base_raw = _run_git(
            self.git_dir,
            ["merge-base", base, head],
            max_output=128,
            allow_exit_one=True,
        )
        merge_base = merge_base_raw.decode("ascii", "ignore").strip().lower()
        if not re.fullmatch(r"[0-9a-f]{40,64}", merge_base):
            raise GatewayError("repository refs do not share a merge base")

        count_raw = _run_git(
            self.git_dir,
            [
                "rev-list",
                "--count",
                f"--max-count={MAX_COMPARE_COMMITS + 1}",
                base + ".." + head,
            ],
            max_output=32,
        )
        try:
            commit_count = int(count_raw)
        except ValueError as exc:
            raise GitError("repository commit count is invalid") from exc
        if commit_count > MAX_COMPARE_COMMITS:
            raise GatewayError("repository comparison is too large")

        patch = _run_git(
            self.git_dir,
            [
                "diff",
                "--no-ext-diff",
                "--no-textconv",
                "--binary",
                merge_base,
                head,
            ],
            max_output=MAX_COMPARE_BYTES,
        )
        merges = _run_git(
            self.git_dir,
            ["rev-list", "--min-parents=2", "--max-count=1", base + ".." + head],
            max_output=128,
        ).strip()
        commits = b""
        if not merges:
            commits = _run_git(
                self.git_dir,
                [
                    "format-patch",
                    "--binary",
                    "--no-signature",
                    "--stdout",
                    base + ".." + head,
                ],
                max_output=MAX_COMPARE_BYTES,
            )
        if len(patch) + len(commits) > MAX_COMPARE_BYTES:
            raise GatewayError("repository comparison is too large")
        return {
            "ok": True,
            "baseOid": base,
            "headOid": head,
            "mergeBaseOid": merge_base,
            "commitCount": commit_count,
            "patch": patch.decode("utf-8", "replace"),
            "commits": commits.decode("utf-8", "replace"),
        }

    def branches(self, _query: Mapping[str, str]) -> dict[str, Any]:
        output = _run_git(
            self.git_dir,
            [
                "for-each-ref",
                "--sort=refname",
                "--format=%(refname)%00%(objectname)%00"
                "%(committerdate:iso8601)",
                "refs/heads/",
                "refs/remotes/",
            ],
            max_output=2 * 1024 * 1024,
        )
        branches = []
        seen: set[str] = set()
        for row in output.decode("utf-8", "replace").splitlines():
            fields = row.split("\x00", 2)
            if len(fields) != 3:
                continue
            name = fields[0]
            for prefix in ("refs/heads/", "refs/remotes/origin/"):
                if name.startswith(prefix):
                    name = name[len(prefix) :]
                    break
            if name.endswith("/HEAD") or name in seen:
                continue
            seen.add(name)
            branches.append(
                {"name": name, "commit": fields[1], "updatedAt": fields[2]}
            )
        return {"ok": True, "branches": branches}

    def search(self, query: Mapping[str, str]) -> dict[str, Any]:
        needle = str(query.get("path") or "").strip()
        if (
            len(needle) < 2
            or len(needle) > 120
            or "\x00" in needle
            or "\r" in needle
            or "\n" in needle
        ):
            raise GatewayError("invalid search query")
        commit = self.resolve_commit(query.get("ref", ""))
        output = _run_git(
            self.git_dir,
            ["grep", "-n", "-I", "-i", "-F", "-e", needle, commit, "--"],
            max_output=2 * 1024 * 1024,
            allow_exit_one=True,
        )
        code = []
        prefix = commit + ":"
        for row in output.decode("utf-8", "replace").splitlines():
            if len(code) >= MAX_SEARCH_RESULTS:
                break
            if row.startswith(prefix):
                row = row[len(prefix) :]
            match = re.match(r"^(.+?):([0-9]+):(.*)$", row)
            if not match:
                continue
            code.append(
                {
                    "path": match.group(1),
                    "line": int(match.group(2)),
                    "snippet": match.group(3).strip()[:200],
                }
            )
        return {
            "ok": True,
            "issues": [],
            "pulls": [],
            "code": code,
            "truncated": len(code) >= MAX_SEARCH_RESULTS,
        }

    def _all_blobs(self, commit: str) -> list[tuple[int, str]]:
        output = _run_git(
            self.git_dir,
            ["ls-tree", "-r", "-l", "-z", commit],
            max_output=64 * 1024 * 1024,
        )
        blobs = []
        for record in output.split(b"\0"):
            if not record or b"\t" not in record:
                continue
            metadata, path = record.split(b"\t", 1)
            fields = metadata.split()
            if len(fields) < 4 or fields[1] != b"blob":
                continue
            try:
                size = int(fields[3])
            except ValueError:
                continue
            blobs.append((max(0, size), path.decode("utf-8", "replace")))
        return blobs

    def stats(self, query: Mapping[str, str]) -> dict[str, Any]:
        commit = self.resolve_commit(query.get("ref", ""))
        blobs = self._all_blobs(commit)
        extensions: dict[str, dict[str, int]] = {}
        for size, path in blobs:
            name = path.rsplit("/", 1)[-1]
            if "." not in name or name.startswith("."):
                continue
            extension = name.rsplit(".", 1)[-1].lower()
            if not extension or len(extension) > 12:
                continue
            bucket = extensions.setdefault(extension, {"bytes": 0, "files": 0})
            bucket["bytes"] += size
            bucket["files"] += 1
        shortlog = _run_git(
            self.git_dir,
            ["shortlog", "-nse", commit],
            max_output=2 * 1024 * 1024,
        )
        contributors = []
        for row in shortlog.decode("utf-8", "replace").splitlines()[:500]:
            match = re.match(r"^\s*([0-9]+)\s+(.+?)(?:\s+<([^>]*)>)?\s*$", row)
            if match:
                contributors.append(
                    {
                        "name": match.group(2),
                        "email": match.group(3) or "",
                        "commits": int(match.group(1)),
                    }
                )
        return {
            "ok": True,
            "commit": commit,
            "fileCount": len(blobs),
            "extensions": extensions,
            "contributors": contributors,
            "contributorCount": len(contributors),
        }

    def sizes(self, query: Mapping[str, str]) -> dict[str, Any]:
        commit = self.resolve_commit(query.get("ref", ""))
        blobs = self._all_blobs(commit)
        max_depth = 8
        max_children = 40
        root: dict[str, Any] = {
            "name": "",
            "size": 0,
            "type": "directory",
            "_children": {},
        }
        for size, path in blobs:
            root["size"] += size
            current = root
            parts = path.split("/")
            directories = parts[:-1]
            for part in directories[:max_depth]:
                children = current["_children"]
                current = children.setdefault(
                    "directory:" + part,
                    {
                        "name": part,
                        "size": 0,
                        "type": "directory",
                        "_children": {},
                    },
                )
                current["size"] += size
            display_name = parts[-1]
            if len(directories) > max_depth:
                display_name = "…/" + display_name
            current["_children"]["file:" + path] = {
                "name": display_name,
                "path": path,
                "size": size,
                "type": "file",
            }

        def serialize(node: dict[str, Any]) -> dict[str, Any]:
            result = {
                "name": node["name"],
                "size": node["size"],
                "type": node["type"],
            }
            if node["type"] == "file":
                result["path"] = node["path"]
                return result
            children = sorted(
                node["_children"].values(),
                key=lambda item: (-item["size"], item["name"]),
            )
            shown = children[:max_children]
            output = [serialize(item) for item in shown]
            if len(children) > max_children:
                output.append(
                    {
                        "name": "…",
                        "size": sum(
                            item["size"] for item in children[max_children:]
                        ),
                        "type": "summary",
                    }
                )
            if output:
                result["children"] = output
            return result

        serialized = serialize(root)
        serialized["ok"] = True
        serialized["commit"] = commit
        serialized["fileCount"] = len(blobs)
        return serialized

    def release_spec(
        self, query: Mapping[str, str], max_release_bytes: int
    ) -> "StreamSpec":
        digest = str(query.get("sha256") or query.get("path") or "").lower()
        if not SHA256_RE.fullmatch(digest) or self.config.release_store is None:
            raise GatewayError("release blob is unavailable")
        root = self.config.release_store.resolve()
        path = (root / "sha256" / digest[:2] / digest / "data").resolve()
        if root not in path.parents or not path.is_file():
            raise GatewayError("release blob is unavailable")
        size = path.stat().st_size
        if size > max_release_bytes:
            raise GatewayError("release blob exceeds configured size limit")
        if not hmac.compare_digest(_sha256_file(path), digest):
            raise GatewayError("release blob integrity check failed")
        return StreamSpec(
            kind="file",
            content_type="application/octet-stream",
            content_length=size,
            path=path,
            filename=digest,
            disposition="attachment",
        )

    def git_advertisement(self) -> bytes:
        advertisement = _run_git(
            self.git_dir,
            [
                "-c",
                "uploadpack.allowTipSHA1InWant=true",
                "-c",
                "uploadpack.allowReachableSHA1InWant=true",
                "upload-pack",
                "--stateless-rpc",
                "--advertise-refs",
                str(self.git_dir),
            ],
            max_output=16 * 1024 * 1024,
        )
        service = b"# service=git-upload-pack\n"
        return ("%04x" % (len(service) + 4)).encode("ascii") + service + b"0000" + advertisement

    def upload_pack_spec(self, body: bytes) -> "StreamSpec":
        return StreamSpec(
            kind="process",
            content_type="application/x-git-upload-pack-result",
            content_length=None,
            command=tuple(
                _git_prefix(self.git_dir)
                + [
                    "-c",
                    "uploadpack.allowTipSHA1InWant=true",
                    "-c",
                    "uploadpack.allowReachableSHA1InWant=true",
                    "upload-pack",
                    "--stateless-rpc",
                    str(self.git_dir),
                ]
            ),
            input_bytes=body,
        )


@dataclass
class StreamSpec:
    kind: str
    content_type: str
    content_length: int | None
    path: Path | None = None
    command: tuple[str, ...] = ()
    input_bytes: bytes = b""
    filename: str = ""
    disposition: str = "inline"


@dataclass
class GatewayResponse:
    status: int
    content_type: str
    body: bytes = b""
    headers: dict[str, str] = field(default_factory=dict)
    stream: StreamSpec | None = None


def json_response(value: Any, status: int = 200) -> GatewayResponse:
    body = (
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
        + b"\n"
    )
    return GatewayResponse(status, "application/json; charset=utf-8", body)


class ReplayCache:
    def __init__(self, maximum: int = 10_000) -> None:
        self.maximum = maximum
        self._seen: dict[str, int] = {}
        self._lock = threading.Lock()

    def claim(self, request_id: str, expires_at: int, now_ms: int) -> bool:
        with self._lock:
            self._seen = {
                key: expiry for key, expiry in self._seen.items() if expiry >= now_ms
            }
            if request_id in self._seen:
                return False
            if len(self._seen) >= self.maximum:
                oldest = min(self._seen, key=self._seen.get)
                del self._seen[oldest]
            self._seen[request_id] = expires_at
            return True


class GatewayApplication:
    def __init__(
        self,
        config: GatewayConfig,
        *,
        verifier: Any | None = None,
        health_signer: Any | None = None,
        materializer: ArchiveMaterializer | None = None,
        clock_ms: Callable[[], int] = lambda: int(time.time() * 1000),
        log: Callable[[dict[str, Any]], None] | None = None,
    ) -> None:
        self.config = config
        self.clock_ms = clock_ms
        self.log = log or self._default_log
        self.verifier = verifier or ExternalCapabilityVerifier(
            config.verifier_command, config.router_public_key
        )
        self.health_signer = health_signer or ExternalHealthSigner(
            config.health_signer_command, config.public_key
        )
        self.replays = ReplayCache()
        self._temporary_root = tempfile.TemporaryDirectory(
            prefix="forkmesh-mirror-runtime-"
        )
        os.chmod(self._temporary_root.name, 0o700)
        materializer = materializer or ArchiveMaterializer()
        self.repositories: dict[tuple[str, str], GitRepository] = {}
        self.quarantined_count = 0
        for index, repository in enumerate(config.repositories):
            if repository.visibility != "public" or not repository.enabled:
                continue
            try:
                git_dir = repository.git_dir
                if repository.encrypted_archive is not None:
                    destination = (
                        Path(self._temporary_root.name) / f"repository-{index}"
                    )
                    destination.mkdir(mode=0o700)
                    git_dir = materializer.materialize(
                        repository.encrypted_archive, destination
                    )
                if git_dir is None:
                    raise GatewayError("public repository storage is missing")
                runtime = GitRepository(repository, git_dir)
                if not runtime.check_integrity(force=True):
                    raise GatewayError("public repository integrity pin mismatch")
                self.repositories[(repository.owner, repository.name.lower())] = runtime
            except (GatewayError, GitError, OSError):
                # Do not log the repository identity or local storage path.
                self.quarantined_count += 1

        self._manifest = config.manifest_path.read_bytes()

    def close(self) -> None:
        self._temporary_root.cleanup()

    @staticmethod
    def _default_log(event: dict[str, Any]) -> None:
        print(_canonical_json(event), flush=True)

    def _record(
        self,
        *,
        request_id: str,
        operation: str,
        status: int,
        started: float,
        byte_count: int,
    ) -> None:
        safe_request_id = (
            request_id if REQUEST_ID_RE.fullmatch(request_id) else ""
        )
        self.log(
            {
                "time": datetime.now(timezone.utc)
                .isoformat()
                .replace("+00:00", "Z"),
                "requestId": safe_request_id,
                "operation": operation
                if operation in LOG_OPERATIONS
                else "control",
                "status": int(status),
                "durationMs": max(0, int((time.monotonic() - started) * 1000)),
                "bytes": max(0, int(byte_count)),
            }
        )

    def _authorize(
        self,
        method: str,
        target: str,
        headers: Mapping[str, str],
        body: bytes,
    ) -> str:
        normalized_headers = {str(k).lower(): str(v) for k, v in headers.items()}
        node = normalized_headers.get("x-forkmesh-node", "")
        request_id = normalized_headers.get("x-forkmesh-request-id", "")
        body_hash = normalized_headers.get("x-forkmesh-body-sha256", "").lower()
        signature = normalized_headers.get("x-forkmesh-signature", "")
        try:
            issued_at = int(normalized_headers.get("x-forkmesh-issued-at", "0"))
        except ValueError:
            raise GatewayError("request capability is invalid")
        actual_hash = hashlib.sha256(body).hexdigest()
        now = self.clock_ms()
        if (
            node != self.config.node
            or not hmac.compare_digest(body_hash, actual_hash)
            or abs(now - issued_at) > CAPABILITY_WINDOW_MS
        ):
            raise GatewayError("request capability is invalid")
        message = request_message(
            node, method, target, body_hash, request_id, issued_at
        )
        if not message or not self.verifier.verify(message, signature):
            raise GatewayError("request capability is invalid")
        if not self.replays.claim(
            request_id, issued_at + CAPABILITY_WINDOW_MS, now
        ):
            raise GatewayError("request capability was already used")
        return request_id

    @staticmethod
    def _parse_query(
        raw_query: str, allowed: frozenset[str]
    ) -> dict[str, str]:
        try:
            parsed = parse_qs(
                raw_query,
                keep_blank_values=True,
                strict_parsing=bool(raw_query),
                max_num_fields=8,
            )
        except ValueError as exc:
            raise GatewayError("request query is invalid") from exc
        if any(key not in allowed or len(values) != 1 for key, values in parsed.items()):
            raise GatewayError("request query is invalid")
        return {key: values[0] for key, values in parsed.items()}

    def _health(self, query_text: str) -> GatewayResponse:
        query = self._parse_query(
            query_text, frozenset({"nonce", "issuedAt", "owner", "repo"})
        )
        response: dict[str, Any] = {
            "ok": self.quarantined_count == 0,
            "type": "forkmesh.mirror-health",
            "schemaVersion": 1,
            "node": self.config.node,
            "publicKey": self.config.public_key,
            "transport": "direct-https",
            "integrity": "ok" if self.quarantined_count == 0 else "degraded",
            "publicRepositoryCount": len(self.repositories),
            "quarantinedRepositoryCount": self.quarantined_count,
            "checkedAt": self.clock_ms(),
        }
        if query:
            if set(query) not in (
                {"nonce", "issuedAt"},
                {"nonce", "issuedAt", "owner", "repo"},
            ):
                raise GatewayError("health challenge is incomplete")
            try:
                issued_at = int(query["issuedAt"])
            except ValueError as exc:
                raise GatewayError("health challenge is invalid") from exc
            if abs(self.clock_ms() - issued_at) > CAPABILITY_WINDOW_MS:
                raise GatewayError("health challenge is stale")
            message_type = "forkmesh-https-health-v1"
            if "owner" in query:
                owner = query["owner"].strip().lower()
                repository_name = query["repo"].strip()
                if (
                    not NODE_RE.fullmatch(owner)
                    or not REPO_RE.fullmatch(repository_name)
                ):
                    raise GatewayError("health repository request is invalid")
                repository = self.repositories.get(
                    (owner, repository_name.lower())
                )
                available = bool(
                    repository is not None and repository.check_integrity(force=True)
                )
                empty_digest = hashlib.sha256(b"").hexdigest()
                refs_digest = (
                    refs_sha256(repository.git_dir)
                    if available and repository is not None
                    else empty_digest
                )
                operations = (
                    sorted(repository.config.operations)
                    if available and repository is not None
                    else []
                )
                operations_digest = hashlib.sha256(
                    "\n".join(operations).encode("utf-8")
                ).hexdigest()
                integrity = "ok" if available else "unavailable"
                response["repositoryProof"] = {
                    "owner": owner,
                    "repository": repository_name,
                    "available": available,
                    "integrity": integrity,
                    "refsSha256": refs_digest,
                    "operations": operations,
                    "operationsSha256": operations_digest,
                }
                message = repository_health_challenge(
                    self.config.node,
                    query["nonce"],
                    issued_at,
                    owner,
                    repository_name,
                    available=available,
                    integrity=integrity,
                    refs_digest=refs_digest,
                    operations_digest=operations_digest,
                )
                message_type = "forkmesh-https-health-repository-v1"
            else:
                message = health_challenge(
                    self.config.node, query["nonce"], issued_at
                )
            if not message:
                raise GatewayError("health challenge is invalid")
            response["challenge"] = {
                "messageType": message_type,
                "nonce": query["nonce"],
                "issuedAt": issued_at,
                "algorithm": "Ed25519",
                "encoding": "base64url-no-padding",
                "messageSha256": hashlib.sha256(
                    message.encode("utf-8")
                ).hexdigest(),
                "signature": self.health_signer.sign(message),
            }
        return json_response(response, 200 if response["ok"] else 503)

    def dispatch(
        self,
        method: str,
        target: str,
        headers: Mapping[str, str],
        body: bytes,
    ) -> GatewayResponse:
        started = time.monotonic()
        operation = "control"
        request_id = ""
        try:
            if len(body) > MAX_REQUEST_BODY:
                return json_response({"ok": False, "error": "request_too_large"}, 413)
            parsed = urlsplit(target)
            if parsed.fragment or not parsed.path.startswith("/"):
                raise GatewayError("request target is invalid")
            if method in ("GET", "HEAD") and body:
                raise GatewayError("GET/HEAD requests must not contain a body")
            if parsed.path == "/health" and method in ("GET", "HEAD"):
                return self._health(parsed.query)
            if (
                parsed.path
                in ("/forkmesh-mirror.json", "/.well-known/forkmesh-mirror.json")
                and method in ("GET", "HEAD")
                and not parsed.query
            ):
                return GatewayResponse(
                    200, "application/json; charset=utf-8", self._manifest
                )

            request_id = self._authorize(method, target, headers, body)
            pieces = parsed.path.split("/")
            if (
                len(pieces) == 4
                and pieces[1] == "v1"
                and pieces[2] == "private-replicas"
            ):
                operation = "private-replica"
                opaque_id = unquote(pieces[3], errors="strict").lower()
                if method not in ("GET", "HEAD") or parsed.query:
                    raise GatewayError("private replica was not found")
                path, size, replica_digest = private_replica_file(
                    self.config.private_replica_store,
                    opaque_id,
                    self.config.max_private_replica_bytes,
                )
                return GatewayResponse(
                    200,
                    "application/vnd.forkmesh.private-replica+json",
                    stream=StreamSpec(
                        kind="file",
                        content_type=(
                            "application/vnd.forkmesh.private-replica+json"),
                        content_length=size,
                        path=path,
                        filename=opaque_id + ".fm-private",
                        disposition="attachment",
                    ),
                    headers={"ETag": '"sha256-' + replica_digest + '"'},
                )
            if (
                len(pieces) != 6
                or pieces[1] != "v1"
                or pieces[2] != "repositories"
            ):
                raise GatewayError("repository route was not found")
            try:
                owner = unquote(pieces[3], errors="strict").lower()
                name = unquote(pieces[4], errors="strict")
                operation = unquote(pieces[5], errors="strict")
            except UnicodeError as exc:
                raise GatewayError("repository route was not found") from exc
            # Unknown, disabled, private, and quarantined repositories all take
            # the same lookup path and produce the same response.
            repository = self.repositories.get((owner, name.lower()))
            if repository is None or operation not in repository.config.operations:
                raise GatewayError("repository route was not found")
            if not repository.check_integrity():
                raise GatewayError("repository route was not found")

            query_fields = {
                "git-info-refs": frozenset({"service"}),
                "git-upload-pack": frozenset(),
                "tree": frozenset({"path", "ref"}),
                "blobs": frozenset({"path", "ref"}),
                "blob": frozenset({"path", "ref"}),
                "raw": frozenset({"path", "ref"}),
                "history": frozenset({"ref"}),
                "commit": frozenset({"path"}),
                "compare": frozenset({"base", "head"}),
                "branches": frozenset(),
                "search": frozenset({"path", "ref"}),
                "stats": frozenset({"ref"}),
                "sizes": frozenset({"ref"}),
                "release-blob": frozenset({"sha256", "path"}),
            }
            if operation == "blobs":
                try:
                    repeated = parse_qs(
                        parsed.query,
                        keep_blank_values=True,
                        strict_parsing=bool(parsed.query),
                        max_num_fields=61,
                    )
                except ValueError as exc:
                    raise GatewayError("request query is invalid") from exc
                if (
                    set(repeated) - {"path", "ref"}
                    or not 1 <= len(repeated.get("path", [])) <= 60
                    or len(repeated.get("ref", [])) > 1
                ):
                    raise GatewayError("request query is invalid")
                paths = [
                    _safe_repo_path(path, allow_empty=False)
                    for path in repeated["path"]
                ]
                query = {
                    "paths": paths,
                    "ref": (repeated.get("ref") or [""])[0],
                }
            else:
                query = self._parse_query(
                    parsed.query, query_fields[operation])
            if operation == "git-info-refs":
                if method not in ("GET", "HEAD") or query != {
                    "service": "git-upload-pack"
                }:
                    raise GatewayError("Git service is not available")
                payload = repository.git_advertisement()
                return GatewayResponse(
                    200,
                    "application/x-git-upload-pack-advertisement",
                    payload,
                    {"Cache-Control": "no-store"},
                )
            if operation == "git-upload-pack":
                normalized_headers = {
                    str(key).lower(): str(value)
                    for key, value in headers.items()
                }
                if (
                    method != "POST"
                    or normalized_headers.get("content-type", "")
                    .split(";", 1)[0]
                    .strip()
                    != "application/x-git-upload-pack-request"
                ):
                    raise GatewayError("Git service is not available")
                decoded_body = _decode_git_body(
                    body, normalized_headers.get("content-encoding", "")
                )
                return GatewayResponse(
                    200,
                    "application/x-git-upload-pack-result",
                    stream=repository.upload_pack_spec(decoded_body),
                )
            if method not in ("GET", "HEAD"):
                raise GatewayError("repository operation is read-only")
            if operation == "blobs":
                return json_response(
                    repository.blobs(query["paths"], query.get("ref", "")))
            if operation == "raw":
                return GatewayResponse(
                    200,
                    "application/octet-stream",
                    stream=repository.raw_spec(query),
                )
            if operation == "release-blob":
                return GatewayResponse(
                    200,
                    "application/octet-stream",
                    stream=repository.release_spec(
                        query, self.config.max_release_bytes
                    ),
                )
            function = {
                "tree": repository.tree,
                "blob": repository.blob,
                "history": repository.history,
                "commit": repository.commit,
                "compare": repository.compare,
                "branches": repository.branches,
                "search": repository.search,
                "stats": repository.stats,
                "sizes": repository.sizes,
            }[operation]
            return json_response(function(query))
        except GatewayError:
            response = json_response({"ok": False, "error": "not_found"}, 404)
        except GitError:
            response = json_response({"ok": False, "error": "mirror_unavailable"}, 503)
        except Exception:
            response = json_response({"ok": False, "error": "mirror_unavailable"}, 503)
        finally:
            # Successful responses are recorded by the HTTP adapter after
            # streaming. Error/control responses are intentionally identity-free.
            pass
        self._record(
            request_id=request_id,
            operation=operation,
            status=response.status,
            started=started,
            byte_count=len(response.body),
        )
        return response


def _safe_download_filename(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._ -]+", "_", value).strip(" .")
    return (cleaned or "file")[:180]


class MirrorGatewayHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "ForkMeshMirrorGateway/1"
    sys_version = ""

    @property
    def application(self) -> GatewayApplication:
        return self.server.application  # type: ignore[attr-defined]

    def log_message(self, _format: str, *_arguments: Any) -> None:
        # BaseHTTPRequestHandler logs client IP and raw path by default.
        return

    def do_GET(self) -> None:  # noqa: N802
        self._handle()

    def do_HEAD(self) -> None:  # noqa: N802
        self._handle()

    def do_POST(self) -> None:  # noqa: N802
        self._handle()

    def do_PUT(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_DELETE(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def do_PATCH(self) -> None:  # noqa: N802
        self._method_not_allowed()

    def _method_not_allowed(self) -> None:
        response = json_response({"ok": False, "error": "read_only"}, 405)
        self._send_headers(response, content_length=len(response.body))
        if self.command != "HEAD":
            self.wfile.write(response.body)

    def _read_body(self) -> bytes:
        raw_length = self.headers.get("Content-Length", "0")
        try:
            length = int(raw_length)
        except ValueError as exc:
            raise GatewayError("invalid request length") from exc
        if length < 0 or length > MAX_REQUEST_BODY:
            raise GatewayError("invalid request length")
        if self.headers.get("Transfer-Encoding"):
            # Cloudflare sends a Content-Length for these bounded capability
            # requests; rejecting ambiguous framing prevents request smuggling.
            raise GatewayError("transfer encoding is not supported")
        return self.rfile.read(length) if length else b""

    def _handle(self) -> None:
        started = time.monotonic()
        try:
            body = self._read_body()
            response = self.application.dispatch(
                self.command,
                self.path,
                dict(self.headers.items()),
                body,
            )
        except GatewayError:
            response = json_response({"ok": False, "error": "bad_request"}, 400)
        byte_count = 0
        try:
            if response.stream is not None:
                content_length = response.stream.content_length
            else:
                content_length = len(response.body)
            self._send_headers(response, content_length=content_length)
            if self.command == "HEAD":
                return
            if response.stream is not None:
                byte_count = self._stream(response.stream)
            else:
                self.wfile.write(response.body)
                byte_count = len(response.body)
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        finally:
            operation = self.path.rsplit("/", 1)[-1].split("?", 1)[0]
            if urlsplit(self.path).path.startswith("/v1/private-replicas/"):
                operation = "private-replica"
            self.application._record(
                request_id=self.headers.get("X-ForkMesh-Request-Id", ""),
                operation=operation,
                status=response.status,
                started=started,
                byte_count=byte_count,
            )

    def _send_headers(
        self, response: GatewayResponse, *, content_length: int | None
    ) -> None:
        self.send_response(response.status)
        self.send_header("Content-Type", response.content_type)
        if content_length is not None:
            self.send_header("Content-Length", str(content_length))
        else:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'none'")
        self.send_header("Referrer-Policy", "no-referrer")
        for key, value in response.headers.items():
            if key.lower() in {
                "cache-control",
                "etag",
                "last-modified",
                "accept-ranges",
            }:
                self.send_header(key, value)
        if response.stream and response.stream.filename:
            filename = _safe_download_filename(response.stream.filename)
            self.send_header(
                "Content-Disposition",
                response.stream.disposition
                + '; filename="'
                + filename.replace('"', "_")
                + '"',
            )
        self.end_headers()

    def _stream(self, spec: StreamSpec) -> int:
        if spec.kind == "file" and spec.path is not None:
            total = 0
            with spec.path.open("rb") as source:
                for chunk in iter(lambda: source.read(256 * 1024), b""):
                    self.wfile.write(chunk)
                    total += len(chunk)
            return total
        if spec.kind != "process" or not spec.command:
            raise GatewayError("invalid stream specification")
        process = subprocess.Popen(
            list(spec.command),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env=_git_environment(),
            start_new_session=True,
        )
        assert process.stdin is not None
        assert process.stdout is not None
        os.set_blocking(process.stdin.fileno(), False)
        os.set_blocking(process.stdout.fileno(), False)
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
        pending = memoryview(spec.input_bytes)
        if pending:
            selector.register(process.stdin, selectors.EVENT_WRITE, "stdin")
        else:
            process.stdin.close()
        total = 0
        deadline = time.monotonic() + STREAM_TIMEOUT_SECONDS
        try:
            while selector.get_map():
                if time.monotonic() > deadline:
                    raise TimeoutError("stream timed out")
                for key, _mask in selector.select(timeout=1):
                    if key.data == "stdin":
                        try:
                            written = os.write(process.stdin.fileno(), pending[:65536])
                            pending = pending[written:]
                        except BlockingIOError:
                            continue
                        if not pending:
                            selector.unregister(process.stdin)
                            process.stdin.close()
                    else:
                        try:
                            chunk = os.read(process.stdout.fileno(), 256 * 1024)
                        except BlockingIOError:
                            continue
                        if not chunk:
                            selector.unregister(process.stdout)
                            continue
                        self.wfile.write(chunk)
                        total += len(chunk)
            if process.wait(timeout=5) != 0:
                raise GitError("streaming Git operation failed")
            return total
        finally:
            selector.close()
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait(timeout=2)
                except (OSError, subprocess.TimeoutExpired):
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except OSError:
                        pass


class MirrorGatewayServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self, address: tuple[str, int], application: GatewayApplication
    ) -> None:
        super().__init__(address, MirrorGatewayHandler)
        self.application = application

    def server_close(self) -> None:
        try:
            super().server_close()
        finally:
            self.application.close()

    def handle_error(
        self, _request: Any, _client_address: Any
    ) -> None:
        # socketserver's default includes the raw client address and traceback.
        self.application.log(
            {
                "time": datetime.now(timezone.utc)
                .isoformat()
                .replace("+00:00", "Z"),
                "requestId": "",
                "operation": "control",
                "status": 503,
                "durationMs": 0,
                "bytes": 0,
            }
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Serve explicitly configured public Git mirrors on loopback for a "
            "Cloudflare Tunnel. Private repositories fail closed."
        )
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--config", type=Path, help="gateway JSON configuration"
    )
    source.add_argument(
        "--refs-sha256",
        type=Path,
        metavar="BARE_REPOSITORY",
        help="print the canonical heads/tags SHA-256 integrity pin",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate configuration, manifest, repositories, and integrity pins",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    application: GatewayApplication | None = None
    try:
        if args.refs_sha256 is not None:
            repository_path = args.refs_sha256.resolve()
            _ensure_bare_repository(repository_path)
            print(refs_sha256(repository_path))
            return 0
        assert args.config is not None
        config = load_config(args.config)
        application = GatewayApplication(config)
        if args.check:
            summary = {
                "ok": application.quarantined_count == 0,
                "node": config.node,
                "transport": "loopback-http-behind-cloudflare-tunnel",
                "publicRepositoryCount": len(application.repositories),
                "quarantinedRepositoryCount": application.quarantined_count,
                "privateRepositoryDetailsPublished": False,
                "privateKeysLoaded": False,
            }
            print(_canonical_json(summary))
            return 0 if summary["ok"] else 1
        server = MirrorGatewayServer(
            (config.listen_host, config.listen_port), application
        )
        application = None
        print(
            _canonical_json(
                {
                    "ok": True,
                    "event": "gateway_started",
                    "listen": f"{config.listen_host}:{config.listen_port}",
                    "publicRepositoryCount": len(server.application.repositories),
                }
            ),
            flush=True,
        )
        try:
            server.serve_forever(poll_interval=0.5)
        finally:
            server.server_close()
        return 0
    except (GatewayError, OSError) as exc:
        print(f"mirror gateway failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if application is not None:
            application.close()


if __name__ == "__main__":
    raise SystemExit(main())
