#!/usr/bin/env python3
"""Fail-closed refresh orchestration for a headless ForkMesh mirror.

The identity helper owns all node and age private material.  This process
accepts only a bounded, owner-only, secret-free JSON configuration and invokes
the helper through its typed JSON stdin protocols.

The local refresh is deliberately separate from network publication:

* ``refresh`` fscks the exact bare source, seals a content-addressed age
  archive, validates a staged gateway configuration, and atomically switches
  the gateway configuration to it.
* the service manager restarts the gateway;
* ``register`` validates the active configuration, registers the endpoint, and
  publishes the node-owner catalog-v2 record.
* ``check`` performs the same active-state validation without sealing or
  making a network request.

Operational output contains only a generic event and counts.  Paths, node and
repository identities, subprocess output, HTTP response bodies, and request
payloads are never logged.
"""

from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
from dataclasses import dataclass
import fcntl
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys
import tempfile
import time
from typing import Any, BinaryIO, Callable, Iterable, Iterator, Mapping
from urllib import error as urlerror
from urllib import request as urlrequest
from urllib.parse import quote, urlsplit


SCHEMA_VERSION = 1
CONFIG_TYPE = "forkmesh.headless-mirror-refresh"
MAX_CONFIG_BYTES = 64 * 1024
MAX_JSON_OUTPUT_BYTES = 128 * 1024
MAX_HTTP_RESPONSE_BYTES = 256 * 1024
MAX_ARCHIVE_BYTES = 64 * 1024 * 1024 * 1024
PROCESS_TIMEOUT_SECONDS = 30 * 60
HTTP_TIMEOUT_SECONDS = 30
MAX_PUBLIC_REPOSITORY_COUNT = 999_999_999_999
MAX_REPOSITORY_METADATA_PATHS = 100_000
MAX_REPOSITORY_METADATA_BYTES = 32 * 1024 * 1024
MAX_ISSUE_RECORD_BYTES = 1024 * 1024
AGE_NATIVE_HEADER = b"age-encryption.org/v1\n"
AGE_ARMORED_HEADER = b"-----BEGIN AGE ENCRYPTED FILE-----\n"
NODE_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT_ID_RE = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
BASE64URL_RE = re.compile(r"^[A-Za-z0-9_-]+$")
SOLANA_RE = re.compile(r"^[1-9A-HJ-NP-Za-km-z]{32,44}$")
KEY_REFERENCE_RE = re.compile(r"^[A-Za-z0-9._:/@+-]{3,240}$")
SENSITIVE_FIELD_RE = re.compile(
    r"(?:private.?key|secret|token|seed|mnemonic|keypair|password|credential)",
    re.IGNORECASE,
)
PUBLIC_KEY_FIELDS = frozenset(
    {
        "publicKey",
        "nodePublicKey",
        "routerPublicKey",
        "keyReference",
        "ciphertextSha256",
        "expectedRefsSha256",
        "stateHash",
        "payloadSha256",
    }
)
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
REQUIRED_ROUTING_OPERATIONS = frozenset(
    {"git-info-refs", "git-upload-pack", "tree", "blob", "raw"}
)


class RefreshError(RuntimeError):
    """A fixed policy failure safe to print without leaking operator data."""


@dataclass(frozen=True)
class CatalogDefaults:
    description: str
    solana: str
    channel: str
    hosted_since: str
    branch: str
    platform: str
    version: str
    report_cpu: bool
    report_memory: bool
    report_disk: bool


@dataclass(frozen=True)
class RefreshConfig:
    config_path: Path
    source_repository: Path
    archive_directory: Path
    release_store: Path | None
    gateway_config_path: Path
    identity_state_directory: Path
    identity_helper_path: Path
    mirror_gateway_path: Path
    python_program: Path
    git_program: Path
    manifest_path: Path
    node_owner: str
    repository_name: str
    owner_aliases: tuple[str, ...]
    worker_origin: str
    public_origin: str
    listen_host: str
    listen_port: int
    operations: tuple[str, ...]
    catalog: CatalogDefaults

    def helper_command(self, mode: str) -> list[str]:
        return [
            str(self.python_program),
            str(self.identity_helper_path),
            "--state-dir",
            str(self.identity_state_directory),
            mode,
        ]

    def gateway_check_command(self, config_path: Path) -> list[str]:
        return [
            str(self.python_program),
            str(self.mirror_gateway_path),
            "--config",
            str(config_path),
            "--check",
        ]


@dataclass(frozen=True)
class PublicIdentity:
    node_name: str
    node_public_key: str
    router_public_key: str
    allowed_origins: tuple[str, ...]


@dataclass(frozen=True)
class SealMetadata:
    ciphertext_sha256: str
    ciphertext_bytes: int
    key_reference: str
    expected_refs_sha256: str


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise RefreshError("JSON contains a duplicate field")
        result[key] = value
    return result


def _parse_json(raw: bytes, *, maximum: int, label: str) -> dict[str, Any]:
    if not raw or len(raw) > maximum:
        raise RefreshError(f"{label} size is invalid")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RefreshError(f"{label} is not valid JSON") from exc
    if not isinstance(value, dict):
        raise RefreshError(f"{label} must be a JSON object")
    return value


def _canonical_json(value: Any) -> bytes:
    return json.dumps(
        value,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")


def _expect_fields(
    value: Mapping[str, Any],
    expected: Iterable[str],
    *,
    optional: Iterable[str] = (),
    label: str,
) -> None:
    fields = set(value)
    required = set(expected)
    allowed = required | set(optional)
    if not required.issubset(fields) or not fields.issubset(allowed):
        raise RefreshError(f"{label} contains an unknown or missing field")


def _assert_no_secret_fields(value: Any) -> None:
    if isinstance(value, dict):
        for raw_key, item in value.items():
            key = str(raw_key)
            if (
                key not in PUBLIC_KEY_FIELDS
                and SENSITIVE_FIELD_RE.search(key)
            ):
                raise RefreshError("configuration contains a prohibited secret field")
            _assert_no_secret_fields(item)
    elif isinstance(value, list):
        for item in value:
            _assert_no_secret_fields(item)


def _safe_environment() -> dict[str, str]:
    # Keep the child environment allowlisted.  In particular, do not pass
    # cloud credentials, Python import hooks, dynamic-loader options, Git
    # configuration injection, SSH agents, or wallet-related variables.
    environment = {
        "PATH": os.environ.get(
            "PATH",
            "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
        ),
        "LANG": "C",
        "LC_ALL": "C",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_TERMINAL_PROMPT": "0",
        "GIT_PROTOCOL_FROM_USER": "0",
        "GIT_OPTIONAL_LOCKS": "0",
    }
    # A materialized mirror can be several times larger than its encrypted
    # archive. Small hosts frequently mount /tmp as a bounded tmpfs, so permit
    # an operator to select disk-backed temporary storage without weakening the
    # otherwise allowlisted child environment. Only an existing, normalized,
    # owner-only real directory is trusted; unsafe TMPDIR values are ignored
    # and Python retains its normal fail-closed temporary-directory behavior.
    temporary_raw = os.environ.get("TMPDIR", "")
    if temporary_raw:
        temporary = Path(temporary_raw)
        try:
            info = temporary.lstat()
        except OSError:
            info = None
        if (
            temporary.is_absolute()
            and temporary == Path(os.path.normpath(str(temporary)))
            and info is not None
            and stat.S_ISDIR(info.st_mode)
            and not stat.S_ISLNK(info.st_mode)
            and info.st_uid == os.geteuid()
            and not stat.S_IMODE(info.st_mode)
            & (stat.S_IRWXG | stat.S_IRWXO)
        ):
            environment["TMPDIR"] = str(temporary)
    return environment


def _absolute_path(value: Any, label: str) -> Path:
    if (
        not isinstance(value, str)
        or not value
        or "\x00" in value
        or len(value.encode("utf-8")) > 4096
    ):
        raise RefreshError(f"{label} path is invalid")
    path = Path(value)
    if not path.is_absolute() or path != Path(os.path.normpath(str(path))):
        raise RefreshError(f"{label} path must be absolute and normalized")
    return path


def _lstat_no_symlink(path: Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except FileNotFoundError as exc:
        raise RefreshError(f"{label} is unavailable") from exc
    if stat.S_ISLNK(info.st_mode):
        raise RefreshError(f"{label} must not be a symbolic link")
    return info


def _reject_symlink_components(
    path: Path,
    label: str,
    *,
    allow_missing_leaf: bool = False,
) -> None:
    if not path.is_absolute():
        raise RefreshError(f"{label} path must be absolute")
    current = Path(path.anchor)
    parts = path.parts[1:]
    for index, part in enumerate(parts):
        current /= part
        try:
            info = current.lstat()
        except FileNotFoundError:
            if allow_missing_leaf and index == len(parts) - 1:
                return
            raise RefreshError(f"{label} is unavailable")
        if stat.S_ISLNK(info.st_mode):
            raise RefreshError(f"{label} path must not traverse a symbolic link")


def _require_owner_directory(path: Path, label: str) -> Path:
    _reject_symlink_components(path, label)
    info = _lstat_no_symlink(path, label)
    if (
        not stat.S_ISDIR(info.st_mode)
        or info.st_uid != os.geteuid()
        or stat.S_IMODE(info.st_mode) & 0o077
        or stat.S_IMODE(info.st_mode) & 0o700 != 0o700
    ):
        raise RefreshError(f"{label} permissions or ownership are unsafe")
    return path


def _require_secure_parent(path: Path, label: str) -> Path:
    _reject_symlink_components(path.parent, f"{label} parent")
    return _require_owner_directory(path.parent, f"{label} parent")


def _open_bounded_file(
    path: Path,
    *,
    label: str,
    maximum: int,
    owner_only: bool,
) -> tuple[int, os.stat_result]:
    expected = _lstat_no_symlink(path, label)
    mode = stat.S_IMODE(expected.st_mode)
    if (
        not stat.S_ISREG(expected.st_mode)
        or expected.st_nlink != 1
        or expected.st_size <= 0
        or expected.st_size > maximum
        or (owner_only and expected.st_uid != os.geteuid())
        or (not owner_only and expected.st_uid not in {0, os.geteuid()})
        or mode & (stat.S_IWGRP | stat.S_IWOTH)
        or (owner_only and mode & (stat.S_IRWXG | stat.S_IRWXO))
    ):
        raise RefreshError(f"{label} permissions or ownership are unsafe")
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise RefreshError(f"{label} cannot be opened safely") from exc
    actual = os.fstat(descriptor)
    if (
        actual.st_dev != expected.st_dev
        or actual.st_ino != expected.st_ino
        or actual.st_size != expected.st_size
    ):
        os.close(descriptor)
        raise RefreshError(f"{label} changed while opening")
    return descriptor, actual


def _read_bounded_file(
    path: Path,
    *,
    label: str,
    maximum: int,
    owner_only: bool,
) -> bytes:
    descriptor, info = _open_bounded_file(
        path,
        label=label,
        maximum=maximum,
        owner_only=owner_only,
    )
    try:
        chunks = bytearray()
        while len(chunks) <= maximum:
            chunk = os.read(descriptor, min(64 * 1024, maximum + 1 - len(chunks)))
            if not chunk:
                break
            chunks.extend(chunk)
        if len(chunks) != info.st_size or len(chunks) > maximum:
            raise RefreshError(f"{label} changed while reading")
        return bytes(chunks)
    finally:
        os.close(descriptor)


def _read_secure_json(
    path: Path,
    *,
    label: str,
    maximum: int,
    owner_only: bool,
) -> dict[str, Any]:
    return _parse_json(
        _read_bounded_file(
            path,
            label=label,
            maximum=maximum,
            owner_only=owner_only,
        ),
        maximum=maximum,
        label=label,
    )


def _require_safe_program(path: Path, label: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
        info = resolved.stat()
    except (OSError, RuntimeError) as exc:
        raise RefreshError(f"{label} is unavailable") from exc
    if (
        not stat.S_ISREG(info.st_mode)
        or not os.access(resolved, os.X_OK)
        or info.st_uid not in {0, os.geteuid()}
        or stat.S_IMODE(info.st_mode) & (stat.S_IWGRP | stat.S_IWOTH)
    ):
        raise RefreshError(f"{label} permissions or ownership are unsafe")
    return resolved


def _require_safe_script(path: Path, label: str) -> Path:
    descriptor, _info = _open_bounded_file(
        path,
        label=label,
        maximum=8 * 1024 * 1024,
        owner_only=False,
    )
    os.close(descriptor)
    return path


def _normalize_origin(value: Any, label: str) -> str:
    try:
        parsed = urlsplit(str(value or "").strip())
        port = parsed.port
    except (TypeError, ValueError) as exc:
        raise RefreshError(f"{label} is invalid") from exc
    hostname = str(parsed.hostname or "").lower()
    if (
        parsed.scheme != "https"
        or not hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.path not in ("", "/")
        or port not in (None, 443)
        or len(hostname) > 253
        or not all(
            re.fullmatch(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?", label_part)
            for label_part in hostname.split(".")
        )
        or len(hostname.split(".")) < 2
    ):
        raise RefreshError(f"{label} must be a credential-free HTTPS origin")
    return "https://" + hostname


def _clean_public_text(value: Any, *, maximum: int, label: str) -> str:
    if not isinstance(value, str):
        raise RefreshError(f"{label} must be a string")
    text = value.strip()
    if len(text) > maximum or any(char in text for char in ("\x00", "\r")):
        raise RefreshError(f"{label} is invalid")
    return text


def _parse_catalog(value: Any) -> CatalogDefaults:
    if value is None:
        value = {}
    if not isinstance(value, dict):
        raise RefreshError("catalog must be an object")
    _expect_fields(
        value,
        set(),
        optional={
            "description",
            "solana",
            "channel",
            "hostedSince",
            "branch",
            "platform",
            "version",
            "reportCpu",
            "reportMemory",
            "reportDisk",
        },
        label="catalog",
    )
    description = _clean_public_text(
        value.get("description", ""), maximum=240, label="catalog.description"
    )
    solana = _clean_public_text(
        value.get("solana", ""), maximum=64, label="catalog.solana"
    )
    if solana and not SOLANA_RE.fullmatch(solana):
        raise RefreshError("catalog.solana is invalid")
    channel = _clean_public_text(
        value.get("channel", ""), maximum=120, label="catalog.channel"
    )
    hosted_since = _clean_public_text(
        value.get("hostedSince", ""), maximum=32, label="catalog.hostedSince"
    )
    if hosted_since and not re.fullmatch(r"[1-9][0-9]{0,18}", hosted_since):
        raise RefreshError("catalog.hostedSince is invalid")
    branch = _clean_public_text(
        value.get("branch", ""), maximum=120, label="catalog.branch"
    )
    platform = _clean_public_text(
        value.get("platform", "git"), maximum=16, label="catalog.platform"
    )
    version = _clean_public_text(
        value.get("version", ""), maximum=32, label="catalog.version"
    )
    for field in ("reportCpu", "reportMemory", "reportDisk"):
        if field in value and not isinstance(value[field], bool):
            raise RefreshError(f"catalog.{field} must be a boolean")
    return CatalogDefaults(
        description=description,
        solana=solana,
        channel=channel,
        hosted_since=hosted_since,
        branch=branch,
        platform=platform,
        version=version,
        report_cpu=value.get("reportCpu", False),
        report_memory=value.get("reportMemory", False),
        report_disk=value.get("reportDisk", False),
    )


def load_config(path: Path) -> RefreshConfig:
    path = _absolute_path(str(path), "configuration")
    _reject_symlink_components(path, "configuration")
    source = _read_secure_json(
        path,
        label="configuration",
        maximum=MAX_CONFIG_BYTES,
        owner_only=True,
    )
    _assert_no_secret_fields(source)
    _expect_fields(
        source,
        {
            "schemaVersion",
            "type",
            "sourceRepository",
            "archiveDirectory",
            "gatewayConfigPath",
            "identityStateDirectory",
            "identityHelperPath",
            "mirrorGatewayPath",
            "pythonProgram",
            "gitProgram",
            "manifestPath",
            "nodeOwner",
            "repositoryName",
            "ownerAliases",
            "workerOrigin",
            "publicOrigin",
            "listen",
            "operations",
        },
        optional={"catalog", "releaseStore"},
        label="configuration",
    )
    if (
        source.get("schemaVersion") != SCHEMA_VERSION
        or source.get("type") != CONFIG_TYPE
    ):
        raise RefreshError("configuration protocol is unsupported")

    source_repository = _absolute_path(
        source.get("sourceRepository"), "source repository"
    )
    archive_directory = _absolute_path(
        source.get("archiveDirectory"), "archive directory"
    )
    release_store_value = source.get("releaseStore")
    release_store = (
        _absolute_path(release_store_value, "release store")
        if release_store_value is not None
        else None
    )
    gateway_config_path = _absolute_path(
        source.get("gatewayConfigPath"), "gateway configuration"
    )
    identity_state_directory = _absolute_path(
        source.get("identityStateDirectory"), "identity state directory"
    )
    identity_helper_path = _absolute_path(
        source.get("identityHelperPath"), "identity helper"
    )
    mirror_gateway_path = _absolute_path(
        source.get("mirrorGatewayPath"), "mirror gateway"
    )
    python_program = _absolute_path(source.get("pythonProgram"), "Python program")
    git_program = _absolute_path(source.get("gitProgram"), "Git program")
    manifest_path = _absolute_path(source.get("manifestPath"), "mirror manifest")

    _require_owner_directory(archive_directory, "archive directory")
    if release_store is not None:
        _require_owner_directory(release_store, "release store")
    _require_owner_directory(identity_state_directory, "identity state directory")
    _require_secure_parent(gateway_config_path, "gateway configuration")
    _reject_symlink_components(source_repository, "source repository")
    _reject_symlink_components(identity_helper_path, "identity helper")
    _reject_symlink_components(mirror_gateway_path, "mirror gateway")
    _reject_symlink_components(manifest_path, "mirror manifest")
    python_program = _require_safe_program(python_program, "Python program")
    git_program = _require_safe_program(git_program, "Git program")
    _require_safe_script(identity_helper_path, "identity helper")
    _require_safe_script(mirror_gateway_path, "mirror gateway")
    manifest_descriptor, _ = _open_bounded_file(
        manifest_path,
        label="mirror manifest",
        maximum=128 * 1024,
        owner_only=False,
    )
    os.close(manifest_descriptor)

    node_owner = str(source.get("nodeOwner") or "").strip().lower()
    repository_name = str(source.get("repositoryName") or "").strip()
    if not NODE_RE.fullmatch(node_owner):
        raise RefreshError("nodeOwner is invalid")
    if not REPOSITORY_RE.fullmatch(repository_name):
        raise RefreshError("repositoryName is invalid")
    aliases_value = source.get("ownerAliases")
    if (
        not isinstance(aliases_value, list)
        or not 1 <= len(aliases_value) <= 16
        or any(not isinstance(item, str) for item in aliases_value)
    ):
        raise RefreshError("ownerAliases must be a bounded string array")
    owner_aliases = tuple(str(item).strip().lower() for item in aliases_value)
    if (
        any(not NODE_RE.fullmatch(item) for item in owner_aliases)
        or len(set(owner_aliases)) != len(owner_aliases)
        or node_owner not in owner_aliases
    ):
        raise RefreshError(
            "ownerAliases must be unique valid owners and include nodeOwner"
        )

    listen = source.get("listen")
    if not isinstance(listen, dict):
        raise RefreshError("listen must be an object")
    _expect_fields(listen, {"host", "port"}, label="listen")
    listen_host = str(listen.get("host") or "")
    if listen_host not in {"127.0.0.1", "::1", "localhost"}:
        raise RefreshError("listen.host must be loopback")
    port_value = listen.get("port")
    if isinstance(port_value, bool):
        raise RefreshError("listen.port is invalid")
    try:
        listen_port = int(port_value)
    except (TypeError, ValueError) as exc:
        raise RefreshError("listen.port is invalid") from exc
    if not 1 <= listen_port <= 65535:
        raise RefreshError("listen.port is outside 1..65535")

    operations_value = source.get("operations")
    if (
        not isinstance(operations_value, list)
        or not operations_value
        or len(operations_value) > len(PUBLIC_OPERATIONS)
        or any(not isinstance(item, str) for item in operations_value)
    ):
        raise RefreshError("operations must be a bounded string array")
    operations = tuple(operations_value)
    if (
        len(set(operations)) != len(operations)
        or not set(operations).issubset(PUBLIC_OPERATIONS)
        or not REQUIRED_ROUTING_OPERATIONS.issubset(operations)
    ):
        raise RefreshError("operations do not satisfy the public routing contract")

    return RefreshConfig(
        config_path=path,
        source_repository=source_repository,
        archive_directory=archive_directory,
        release_store=release_store,
        gateway_config_path=gateway_config_path,
        identity_state_directory=identity_state_directory,
        identity_helper_path=identity_helper_path,
        mirror_gateway_path=mirror_gateway_path,
        python_program=python_program,
        git_program=git_program,
        manifest_path=manifest_path,
        node_owner=node_owner,
        repository_name=repository_name,
        owner_aliases=owner_aliases,
        worker_origin=_normalize_origin(source.get("workerOrigin"), "workerOrigin"),
        public_origin=_normalize_origin(source.get("publicOrigin"), "publicOrigin"),
        listen_host=listen_host,
        listen_port=listen_port,
        operations=operations,
        catalog=_parse_catalog(source.get("catalog")),
    )


@contextmanager
def _refresh_lock(config: RefreshConfig, *, shared: bool) -> Iterator[None]:
    path = config.identity_state_directory / ".refresh.lock"
    # ``check`` is used inside a ProtectSystem=strict gateway unit.  It must be
    # genuinely read-only: the preceding refresh creates the lock, and a
    # shared flock works on an O_RDONLY descriptor.  Refresh/register retain
    # the writable create path and exclusive lock.
    flags = os.O_RDONLY if shared else os.O_RDWR | os.O_CREAT
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o600)
    except OSError as exc:
        raise RefreshError("refresh lock cannot be opened safely") from exc
    try:
        info = os.fstat(descriptor)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid != os.geteuid()
            or info.st_nlink != 1
            or stat.S_IMODE(info.st_mode) & 0o077
        ):
            raise RefreshError("refresh lock permissions or ownership are unsafe")
        fcntl.flock(descriptor, fcntl.LOCK_SH if shared else fcntl.LOCK_EX)
        yield
    finally:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        finally:
            os.close(descriptor)


def _run_bounded(
    command: list[str],
    *,
    input_bytes: bytes | None = None,
    timeout: int = PROCESS_TIMEOUT_SECONDS,
    maximum_output: int = MAX_JSON_OUTPUT_BYTES,
) -> bytes:
    with tempfile.TemporaryFile(mode="w+b") as output:
        try:
            completed = subprocess.run(
                command,
                input=input_bytes,
                stdout=output,
                stderr=subprocess.DEVNULL,
                env=_safe_environment(),
                timeout=timeout,
                check=False,
            )
        except (OSError, subprocess.SubprocessError) as exc:
            raise RefreshError("required local operation failed") from exc
        if completed.returncode != 0:
            raise RefreshError("required local operation failed")
        size = output.tell()
        if size > maximum_output:
            raise RefreshError("required local operation returned too much data")
        output.seek(0)
        return output.read(maximum_output + 1)


def _helper_call(
    config: RefreshConfig,
    mode: str,
    request: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    input_bytes = (
        _canonical_json(request) + b"\n" if request is not None else None
    )
    raw = _run_bounded(config.helper_command(mode), input_bytes=input_bytes)
    response = _parse_json(
        raw,
        maximum=MAX_JSON_OUTPUT_BYTES,
        label="identity helper response",
    )
    _assert_no_secret_fields(response)
    return response


def _load_public_identity(config: RefreshConfig) -> PublicIdentity:
    response = _helper_call(config, "public-info")
    _expect_fields(
        response,
        {
            "schemaVersion",
            "type",
            "nodeName",
            "nodePublicKey",
            "ageRecipient",
            "ageKeyReference",
            "routerPublicKey",
            "allowedOrigins",
        },
        label="public identity response",
    )
    node = str(response.get("nodeName") or "").strip().lower()
    node_public_key = _validate_public_key(response.get("nodePublicKey"))
    router_public_key = _validate_public_key(response.get("routerPublicKey"))
    origins_value = response.get("allowedOrigins")
    if (
        response.get("schemaVersion") != 1
        or response.get("type") != "forkmesh.headless-mirror-identity"
        or node != config.node_owner
        or not isinstance(origins_value, list)
        or any(not isinstance(item, str) for item in origins_value)
    ):
        raise RefreshError("public identity does not match refresh configuration")
    origins = tuple(
        _normalize_origin(item, "identity allowed origin")
        for item in origins_value
    )
    if config.public_origin not in origins:
        raise RefreshError("public origin is not pinned by the node identity")
    return PublicIdentity(node, node_public_key, router_public_key, origins)


def _validate_public_key(value: Any) -> str:
    if not isinstance(value, str) or not BASE64URL_RE.fullmatch(value):
        raise RefreshError("public identity key is invalid")
    try:
        decoded = base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))
    except (TypeError, ValueError) as exc:
        raise RefreshError("public identity key is invalid") from exc
    if (
        len(decoded) != 32
        or base64.urlsafe_b64encode(decoded).decode("ascii").rstrip("=") != value
    ):
        raise RefreshError("public identity key is invalid")
    return value


def _git_prefix(config: RefreshConfig) -> list[str]:
    return [
        str(config.git_program),
        "-c",
        "core.alternateRefsCommand=/usr/bin/true",
        "-c",
        "core.hooksPath=" + os.devnull,
        "-c",
        "core.fsmonitor=",
        "-c",
        "credential.helper=",
        "-c",
        "protocol.ext.allow=never",
        f"--git-dir={config.source_repository}",
    ]


def _require_bare_source(config: RefreshConfig) -> None:
    info = _lstat_no_symlink(config.source_repository, "bare source repository")
    if (
        not stat.S_ISDIR(info.st_mode)
        or info.st_uid != os.geteuid()
        or stat.S_IMODE(info.st_mode) & stat.S_IWOTH
    ):
        raise RefreshError("bare source repository permissions or ownership are unsafe")
    bare = _run_bounded(
        _git_prefix(config) + ["rev-parse", "--is-bare-repository"],
        timeout=60,
    )
    if bare.strip() != b"true":
        raise RefreshError("source repository is not a bare Git repository")


def _fsck_source(config: RefreshConfig) -> None:
    _require_bare_source(config)
    _run_bounded(
        _git_prefix(config)
        + [
            "fsck",
            "--full",
            "--strict",
            "--no-progress",
            "--no-dangling",
        ]
    )


def _source_refs_sha256(config: RefreshConfig) -> str:
    raw = _run_bounded(
        _git_prefix(config)
        + [
            "for-each-ref",
            "--sort=refname",
            "--format=%(objectname) %(refname)",
            "refs/heads/",
            "refs/tags/",
        ],
        maximum_output=16 * 1024 * 1024,
    )
    try:
        canonical = "\n".join(
            line for line in raw.decode("utf-8").splitlines() if line
        )
    except UnicodeDecodeError as exc:
        raise RefreshError("source repository refs are invalid") from exc
    if len(canonical.encode("utf-8")) > 16 * 1024 * 1024:
        raise RefreshError("source repository has too many public refs")
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _source_branch_commit(config: RefreshConfig) -> str:
    revision = (
        "refs/heads/" + config.catalog.branch
        if config.catalog.branch
        else "HEAD"
    )
    raw = _run_bounded(
        _git_prefix(config)
        + [
            "rev-parse",
            "--verify",
            "--end-of-options",
            revision + "^{commit}",
        ],
        maximum_output=256,
        timeout=60,
    )
    try:
        commit = raw.decode("ascii").strip().lower()
    except UnicodeDecodeError as exc:
        raise RefreshError("source repository commit is invalid") from exc
    if not GIT_OBJECT_ID_RE.fullmatch(commit):
        raise RefreshError("source repository commit is invalid")
    return commit


def _source_revision(config: RefreshConfig) -> str:
    return (
        "refs/heads/" + config.catalog.branch
        if config.catalog.branch
        else "HEAD"
    )


def _bounded_repository_count(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    try:
        number = int(value)
    except (TypeError, ValueError, OverflowError):
        return None
    if number < 0 or number > MAX_PUBLIC_REPOSITORY_COUNT:
        return None
    return number


def _source_commit_count(config: RefreshConfig) -> int | None:
    try:
        raw = _run_bounded(
            _git_prefix(config)
            + ["rev-list", "--count", _source_revision(config), "--"],
            maximum_output=64,
            timeout=60,
        )
        return _bounded_repository_count(raw.decode("ascii").strip())
    except (RefreshError, UnicodeDecodeError):
        return None


def _source_branch_count(config: RefreshConfig) -> int | None:
    try:
        raw = _run_bounded(
            _git_prefix(config)
            + [
                "for-each-ref",
                "--format=%(refname)",
                "refs/heads/",
            ],
            maximum_output=MAX_REPOSITORY_METADATA_BYTES,
            timeout=60,
        )
    except RefreshError:
        return None
    lines = [line for line in raw.splitlines() if line]
    if len(lines) > MAX_REPOSITORY_METADATA_PATHS:
        return None
    return _bounded_repository_count(len(lines))


def _source_revision_exists(config: RefreshConfig, revision: str) -> bool:
    try:
        _run_bounded(
            _git_prefix(config)
            + [
                "rev-parse",
                "--verify",
                "--quiet",
                "--end-of-options",
                revision + "^{commit}",
            ],
            maximum_output=128,
            timeout=60,
        )
        return True
    except RefreshError:
        return False


def _source_tree_paths(
    config: RefreshConfig,
    revision: str,
    path: str,
) -> tuple[bytes, ...] | None:
    """Return bounded raw paths below one repository metadata directory.

    ``git ls-tree`` exits successfully with no output when the path does not
    exist, which lets a genuine empty count stay distinct from a failed read.
    Paths remain bytes: Git permits non-UTF-8 names, while the metadata records
    selected below have strict ASCII names.
    """
    try:
        raw = _run_bounded(
            _git_prefix(config)
            + [
                "ls-tree",
                "-r",
                "-z",
                "--name-only",
                revision,
                "--",
                path,
            ],
            maximum_output=MAX_REPOSITORY_METADATA_BYTES,
            timeout=60,
        )
    except RefreshError:
        return None
    paths = tuple(item for item in raw.split(b"\0") if item)
    if len(paths) > MAX_REPOSITORY_METADATA_PATHS:
        return None
    return paths


def _numbered_metadata_count(
    paths: tuple[bytes, ...] | None,
    prefix: bytes,
) -> int | None:
    if paths is None:
        return None
    pattern = re.compile(
        rb"^" + re.escape(prefix) + rb"/([1-9][0-9]*)/"
    )
    numbers: set[int] = set()
    for path in paths:
        match = pattern.match(path)
        if match:
            number = _bounded_repository_count(match.group(1))
            if number is None:
                return None
            numbers.add(number)
    return _bounded_repository_count(len(numbers))


def _source_blob_batch(
    config: RefreshConfig,
    revision: str,
    paths: tuple[bytes, ...],
) -> dict[bytes, bytes] | None:
    if not paths:
        return {}
    requests = b"".join(
        revision.encode("ascii") + b":" + path + b"\n" for path in paths
    )
    try:
        raw = _run_bounded(
            _git_prefix(config) + ["cat-file", "--batch"],
            input_bytes=requests,
            maximum_output=MAX_REPOSITORY_METADATA_BYTES,
            timeout=60,
        )
    except (RefreshError, UnicodeEncodeError):
        return None
    result: dict[bytes, bytes] = {}
    position = 0
    for path in paths:
        line_end = raw.find(b"\n", position)
        if line_end < 0:
            return None
        header = raw[position:line_end].split()
        position = line_end + 1
        if len(header) != 3 or header[1] != b"blob":
            return None
        try:
            size = int(header[2])
        except ValueError:
            return None
        if size < 0 or size > MAX_ISSUE_RECORD_BYTES:
            return None
        content_end = position + size
        if content_end >= len(raw) or raw[content_end:content_end + 1] != b"\n":
            return None
        result[path] = raw[position:content_end]
        position = content_end + 1
    if position != len(raw):
        return None
    return result


def _issue_record_is_tombstoned(record: Mapping[str, Any]) -> bool | None:
    events = record.get("events", [])
    if not isinstance(events, list):
        return None
    creator = ""
    for value in events:
        if not isinstance(value, dict):
            return None
        if value.get("type") == "open" and not creator:
            author = value.get("author")
            if isinstance(author, str):
                creator = author
    if not creator:
        return False
    return any(
        value.get("type") == "delete"
        and value.get("target") == "self"
        and value.get("author") == creator
        for value in events
    )


def _source_issue_counts(
    config: RefreshConfig,
) -> tuple[int | None, int | None] | None:
    revision = _source_revision(config)
    paths = _source_tree_paths(
        config, revision, ".forkmesh/issues"
    )
    if paths is None:
        return None
    # Qt derives the allocation high-water mark from numeric directories, not
    # only from a canonical issue-N.json blob. Do the same so a damaged or
    # partially migrated issue directory can never make ForkBot reuse an
    # already occupied number. Recursive ls-tree does not report directories,
    # so any descendant path proves that the corresponding tree exists.
    directory_pattern = re.compile(
        rb"^\.forkmesh/issues/(?:(open|closed)/)?([0-9]+)/"
    )
    directories: dict[str, set[bytes]] = {
        "open": set(),
        "closed": set(),
        "legacy": set(),
    }
    max_number = 0
    for path in paths:
        match = directory_pattern.match(path)
        if not match:
            continue
        state = (
            match.group(1).decode("ascii")
            if match.group(1)
            else "legacy"
        )
        name = match.group(2)
        number = _bounded_repository_count(name)
        if number is None:
            return None
        directories[state].add(name)
        max_number = max(max_number, number)
    bounded_max = _bounded_repository_count(max_number)
    if bounded_max is None:
        return None

    # Match the desktop reader's precedence: an open/<name> tree is examined
    # first, closed/<name> reserves the same legacy name, and only a remaining
    # pre-split root tree is read. A missing or malformed canonical record is
    # conservatively live, just as the Qt issue list does.
    records: list[tuple[str, bytes]] = []
    counted_names = set(directories["open"])
    for name in sorted(directories["open"]):
        records.append(
            (
                "open",
                b".forkmesh/issues/open/"
                + name
                + b"/issue-"
                + name
                + b".json",
            )
        )
    counted_names.update(directories["closed"])
    for name in sorted(directories["legacy"] - counted_names):
        records.append(
            (
                "legacy",
                b".forkmesh/issues/"
                + name
                + b"/issue-"
                + name
                + b".json",
            )
        )

    available_paths = set(paths)
    readable_paths = tuple(
        path for _state, path in records if path in available_paths
    )
    blobs = _source_blob_batch(config, revision, readable_paths)
    if blobs is None:
        return None, bounded_max
    open_count = 0
    for state, path in records:
        if path not in blobs:
            open_count += 1
            continue
        try:
            record = json.loads(blobs[path].decode("utf-8"))
        except Exception:
            # JSON recursion limits and malformed/unreadable issue records must
            # not take down catalog renewal. Treat them as live, matching the
            # desktop's conservative default, without trusting any tombstone.
            open_count += 1
            continue
        if not isinstance(record, dict):
            open_count += 1
            continue
        if state == "open":
            tombstoned = _issue_record_is_tombstoned(record)
            if tombstoned is not True:
                open_count += 1
        elif record.get("status") != "closed":
            open_count += 1
    bounded_open = _bounded_repository_count(open_count)
    if bounded_open is None:
        return None, bounded_max
    return bounded_open, bounded_max


def _source_pull_count(config: RefreshConfig) -> int | None:
    revision = (
        "refs/heads/forkmesh/pulls"
        if _source_revision_exists(config, "refs/heads/forkmesh/pulls")
        else _source_revision(config)
    )
    return _numbered_metadata_count(
        _source_tree_paths(config, revision, "pulls"),
        b"pulls",
    )


def _source_discussion_count(config: RefreshConfig) -> int | None:
    return _numbered_metadata_count(
        _source_tree_paths(
            config,
            _source_revision(config),
            ".forkmesh/discussions",
        ),
        b".forkmesh/discussions",
    )


def _source_artifact_count(config: RefreshConfig) -> int | None:
    """Count real release blobs in the configured mirror gateway CAS."""
    if config.release_store is None:
        return None
    root = config.release_store / "sha256"
    try:
        root_info = root.lstat()
    except FileNotFoundError:
        return 0
    except OSError:
        return None
    if (
        not stat.S_ISDIR(root_info.st_mode)
        or root_info.st_uid != os.geteuid()
    ):
        return None
    count = 0
    visited = 0
    try:
        with os.scandir(root) as shards:
            for shard in shards:
                visited += 1
                if visited > MAX_REPOSITORY_METADATA_PATHS:
                    return None
                if (
                    not re.fullmatch(r"[0-9a-f]{2}", shard.name)
                    or not shard.is_dir(follow_symlinks=False)
                ):
                    continue
                with os.scandir(shard.path) as hashes:
                    for digest in hashes:
                        visited += 1
                        if visited > MAX_REPOSITORY_METADATA_PATHS:
                            return None
                        if (
                            not SHA256_RE.fullmatch(digest.name)
                            or not digest.name.startswith(shard.name)
                            or not digest.is_dir(follow_symlinks=False)
                        ):
                            continue
                        data = Path(digest.path) / "data"
                        try:
                            if data.is_file() and not data.is_symlink():
                                count += 1
                        except OSError:
                            return None
    except OSError:
        return None
    return _bounded_repository_count(count)


def _sample_repository_statistics(
    config: RefreshConfig,
) -> dict[str, str]:
    """Read public, repository-scoped facts from the validated bare source.

    Each fact fails independently. Headless mirrors have no controller working
    copy and the gateway currently has no durable per-repository request
    counters, so worktree/clones/website fields intentionally remain absent
    instead of being published as misleading zeroes.
    """
    result: dict[str, str] = {}

    def include(field: str, value: int | None) -> None:
        if value is not None:
            result[field] = str(value)

    def sample(field: str, sampler: Callable[[], int | None]) -> None:
        try:
            include(field, sampler())
        except Exception:
            # Repository metadata is untrusted input. One malformed or unusually
            # deep fact must remain unknown without suppressing all other
            # independently readable statistics or breaking lease renewal.
            return

    sample("commitCount", lambda: _source_commit_count(config))
    sample("branchCount", lambda: _source_branch_count(config))
    try:
        issue_counts = _source_issue_counts(config)
    except Exception:
        issue_counts = None
    if issue_counts is not None:
        include("issueCount", issue_counts[0])
        include("issueMaxNumber", issue_counts[1])
    sample("pullCount", lambda: _source_pull_count(config))
    sample("discussionCount", lambda: _source_discussion_count(config))
    sample("artifactCount", lambda: _source_artifact_count(config))
    return result


def _validate_seal_response(value: Mapping[str, Any]) -> SealMetadata:
    _expect_fields(
        value,
        {
            "ok",
            "scheme",
            "ciphertextSha256",
            "ciphertextBytes",
            "keyReference",
            "expectedRefsSha256",
        },
        label="repository seal response",
    )
    digest = str(value.get("ciphertextSha256") or "").lower()
    refs = str(value.get("expectedRefsSha256") or "").lower()
    key_reference = str(value.get("keyReference") or "")
    size_value = value.get("ciphertextBytes")
    if isinstance(size_value, bool):
        raise RefreshError("repository seal response is invalid")
    try:
        size = int(size_value)
    except (TypeError, ValueError) as exc:
        raise RefreshError("repository seal response is invalid") from exc
    if (
        value.get("ok") is not True
        or value.get("scheme") != "age-encrypted-tar-v1"
        or not SHA256_RE.fullmatch(digest)
        or not SHA256_RE.fullmatch(refs)
        or not KEY_REFERENCE_RE.fullmatch(key_reference)
        or not 1 <= size <= MAX_ARCHIVE_BYTES
    ):
        raise RefreshError("repository seal response is invalid")
    return SealMetadata(digest, size, key_reference, refs)


def _validate_archive(
    config: RefreshConfig,
    path: Path,
    *,
    digest: str,
    size: int | None = None,
) -> None:
    if (
        path.parent != config.archive_directory
        or not SHA256_RE.fullmatch(digest)
    ):
        raise RefreshError("encrypted archive permissions or metadata are unsafe")
    descriptor, info = _open_bounded_file(
        path,
        label="encrypted archive",
        maximum=MAX_ARCHIVE_BYTES,
        owner_only=True,
    )
    try:
        if (
            info.st_size <= len(AGE_NATIVE_HEADER)
            or (size is not None and info.st_size != size)
        ):
            raise RefreshError(
                "encrypted archive permissions or metadata are unsafe"
            )
        actual = hashlib.sha256()
        header_size = max(len(AGE_NATIVE_HEADER), len(AGE_ARMORED_HEADER))
        header = os.read(descriptor, header_size)
        actual.update(header)
        while True:
            chunk = os.read(descriptor, 1024 * 1024)
            if not chunk:
                break
            actual.update(chunk)
        if not (
            header.startswith(AGE_NATIVE_HEADER)
            or header.startswith(AGE_ARMORED_HEADER)
        ):
            raise RefreshError("encrypted archive is not an age file")
        if not secrets.compare_digest(actual.hexdigest(), digest):
            raise RefreshError("encrypted archive digest does not match")
    except OSError as exc:
        raise RefreshError("encrypted archive cannot be read safely") from exc
    finally:
        os.close(descriptor)


def _render_gateway_config(
    config: RefreshConfig,
    identity: PublicIdentity,
    metadata: SealMetadata,
    archive_path: Path,
) -> dict[str, Any]:
    helper_base = [
        str(config.python_program),
        str(config.identity_helper_path),
        "--state-dir",
        str(config.identity_state_directory),
    ]
    encrypted_archive = {
        "scheme": "age-encrypted-tar-v1",
        "ciphertextPath": str(archive_path),
        "ciphertextSha256": metadata.ciphertext_sha256,
        "keyReference": metadata.key_reference,
        "materializeCommand": helper_base + ["materialize"],
    }
    repositories = []
    for owner in config.owner_aliases:
        repository = {
            "owner": owner,
            "name": config.repository_name,
            "visibility": "public",
            "enabled": True,
            "encryptedArchive": dict(encrypted_archive),
            "integrity": {
                "expectedRefsSha256": metadata.expected_refs_sha256
            },
            "operations": list(config.operations),
        }
        if config.release_store is not None:
            repository["releaseStore"] = str(config.release_store)
        repositories.append(repository)
    return {
        "schemaVersion": 1,
        "node": {
            "name": identity.node_name,
            "publicKey": identity.node_public_key,
        },
        "routerPublicKey": identity.router_public_key,
        "publicOrigin": config.public_origin,
        "listen": {
            "host": config.listen_host,
            "port": config.listen_port,
        },
        "manifestPath": str(config.manifest_path),
        "requestVerifierCommand": helper_base + ["capability-verify"],
        "healthSignerCommand": helper_base + ["health-sign"],
        "repositories": repositories,
    }


def _write_all(descriptor: int, data: bytes) -> None:
    view = memoryview(data)
    while view:
        written = os.write(descriptor, view)
        if written <= 0:
            raise RefreshError("atomic state write failed")
        view = view[written:]


def _write_staged_json(parent: Path, prefix: str, value: Any) -> Path:
    encoded = _canonical_json(value) + b"\n"
    if len(encoded) > MAX_CONFIG_BYTES:
        raise RefreshError("generated gateway configuration is too large")
    path = parent / ("." + prefix + "." + secrets.token_hex(12) + ".tmp")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags, 0o600)
        try:
            _write_all(descriptor, encoded)
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        return path
    except (OSError, RefreshError) as exc:
        try:
            path.unlink()
        except OSError:
            pass
        raise RefreshError("staged gateway configuration write failed") from exc


def _fsync_directory(path: Path) -> None:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    except OSError as exc:
        raise RefreshError("durable state update failed") from exc


def _install_content_addressed_archive(
    config: RefreshConfig,
    staged: Path,
    metadata: SealMetadata,
) -> Path:
    final = config.archive_directory / (
        "archive-" + metadata.ciphertext_sha256 + ".age"
    )
    try:
        existing = final.lstat()
    except FileNotFoundError:
        existing = None
    if existing is not None:
        _validate_archive(
            config,
            final,
            digest=metadata.ciphertext_sha256,
            size=metadata.ciphertext_bytes,
        )
        try:
            staged.unlink()
        except OSError as exc:
            raise RefreshError("staged archive cleanup failed") from exc
        return final
    try:
        os.link(staged, final, follow_symlinks=False)
        os.chmod(final, 0o600, follow_symlinks=False)
        staged.unlink()
        _fsync_directory(config.archive_directory)
    except (FileExistsError, OSError) as exc:
        try:
            staged.unlink()
        except FileNotFoundError:
            pass
        raise RefreshError("content-addressed archive installation failed") from exc
    _validate_archive(
        config,
        final,
        digest=metadata.ciphertext_sha256,
        size=metadata.ciphertext_bytes,
    )
    return final


def _invoke_gateway_check(config: RefreshConfig, gateway_config: Path) -> None:
    raw = _run_bounded(config.gateway_check_command(gateway_config))
    result = _parse_json(
        raw,
        maximum=MAX_JSON_OUTPUT_BYTES,
        label="gateway validation response",
    )
    if (
        result.get("ok") is not True
        or result.get("privateKeysLoaded") is not False
        or result.get("privateRepositoryDetailsPublished") is not False
    ):
        raise RefreshError("gateway validation rejected the active mirror")


def _active_metadata(
    config: RefreshConfig,
    identity: PublicIdentity,
) -> tuple[SealMetadata, Path]:
    source = _read_secure_json(
        config.gateway_config_path,
        label="active gateway configuration",
        maximum=MAX_CONFIG_BYTES,
        owner_only=True,
    )
    repositories = source.get("repositories")
    if not isinstance(repositories, list) or not repositories:
        raise RefreshError("active gateway configuration is not refresh-managed")
    first = repositories[0]
    if not isinstance(first, dict):
        raise RefreshError("active gateway configuration is not refresh-managed")
    archive = first.get("encryptedArchive")
    integrity = first.get("integrity")
    if not isinstance(archive, dict) or not isinstance(integrity, dict):
        raise RefreshError("active gateway configuration is not refresh-managed")
    digest = str(archive.get("ciphertextSha256") or "").lower()
    refs = str(integrity.get("expectedRefsSha256") or "").lower()
    key_reference = str(archive.get("keyReference") or "")
    archive_path = _absolute_path(
        archive.get("ciphertextPath"), "active encrypted archive"
    )
    try:
        size = archive_path.stat().st_size
    except OSError as exc:
        raise RefreshError("active encrypted archive is unavailable") from exc
    metadata = SealMetadata(digest, size, key_reference, refs)
    if (
        not SHA256_RE.fullmatch(digest)
        or not SHA256_RE.fullmatch(refs)
        or not KEY_REFERENCE_RE.fullmatch(key_reference)
        or archive_path.name != "archive-" + digest + ".age"
    ):
        raise RefreshError("active gateway configuration is not refresh-managed")
    expected = _render_gateway_config(config, identity, metadata, archive_path)
    if not secrets.compare_digest(_canonical_json(source), _canonical_json(expected)):
        raise RefreshError("active gateway configuration drift was detected")
    _validate_archive(config, archive_path, digest=digest, size=size)
    return metadata, archive_path


def _validate_active(
    config: RefreshConfig,
) -> tuple[PublicIdentity, SealMetadata, Path]:
    identity = _load_public_identity(config)
    _fsck_source(config)
    metadata, archive_path = _active_metadata(config, identity)
    if not secrets.compare_digest(
        _source_refs_sha256(config),
        metadata.expected_refs_sha256,
    ):
        raise RefreshError("active archive does not match the exact source refs")
    _invoke_gateway_check(config, config.gateway_config_path)
    return identity, metadata, archive_path


def _validate_active_renewal(
    config: RefreshConfig,
) -> tuple[PublicIdentity, SealMetadata, Path]:
    """Validate the immutable active generation without materializing it.

    A health-lease renewal runs every few minutes, so repeating a full Git fsck
    and encrypted gateway materialization would consume most of a small mirror
    host.  The active configuration and ciphertext are still authenticated
    here, and the exact source refs must still match the sealed state.  The
    Worker then performs its normal fresh signed repository challenge before it
    marks the renewed endpoint healthy.
    """
    identity = _load_public_identity(config)
    _require_bare_source(config)
    metadata, archive_path = _active_metadata(config, identity)
    if not secrets.compare_digest(
        _source_refs_sha256(config),
        metadata.expected_refs_sha256,
    ):
        raise RefreshError("active archive does not match the exact source refs")
    return identity, metadata, archive_path


def refresh(config: RefreshConfig) -> dict[str, Any]:
    with _refresh_lock(config, shared=False):
        identity = _load_public_identity(config)
        _fsck_source(config)
        before_refs = _source_refs_sha256(config)
        staged_archive = config.archive_directory / (
            ".refresh-" + secrets.token_hex(16) + ".age"
        )
        staged_gateway: Path | None = None
        try:
            response = _helper_call(
                config,
                "seal-repository",
                {
                    "schemaVersion": 1,
                    "type": "forkmesh.repository-archive-seal",
                    "sourceRepository": str(config.source_repository),
                    "ciphertextPath": str(staged_archive),
                },
            )
            metadata = _validate_seal_response(response)
            _validate_archive(
                config,
                staged_archive,
                digest=metadata.ciphertext_sha256,
                size=metadata.ciphertext_bytes,
            )
            _fsck_source(config)
            after_refs = _source_refs_sha256(config)
            if not (
                secrets.compare_digest(before_refs, after_refs)
                and secrets.compare_digest(
                    after_refs, metadata.expected_refs_sha256
                )
            ):
                raise RefreshError("source refs changed during repository sealing")
            archive_path = _install_content_addressed_archive(
                config, staged_archive, metadata
            )
            staged_gateway = _write_staged_json(
                config.gateway_config_path.parent,
                config.gateway_config_path.name,
                _render_gateway_config(config, identity, metadata, archive_path),
            )
            _invoke_gateway_check(config, staged_gateway)
            try:
                config.gateway_config_path.lstat()
                active_exists = True
            except FileNotFoundError:
                active_exists = False
            if active_exists:
                descriptor, _ = _open_bounded_file(
                    config.gateway_config_path,
                    label="active gateway configuration",
                    maximum=MAX_CONFIG_BYTES,
                    owner_only=True,
                )
                os.close(descriptor)
            # Everything that can fail is complete before this linearization
            # point.  Both the old and new regular files point only to an
            # already-fsynced content-addressed archive, so a crash around the
            # rename can expose either complete last-good generation, never a
            # config with partially written bytes.
            _fsync_directory(config.gateway_config_path.parent)
            os.replace(staged_gateway, config.gateway_config_path)
            staged_gateway = None
            try:
                _fsync_directory(config.gateway_config_path.parent)
            except RefreshError:
                # The atomic rename has already committed a complete,
                # validated generation.  Reporting failure here would be
                # misleading and could prompt an unsafe retry/rollback.
                pass
            return {
                "ok": True,
                "event": "refresh_complete",
                "aliasCount": len(config.owner_aliases),
            }
        finally:
            for path in (staged_archive, staged_gateway):
                if path is not None:
                    try:
                        path.unlink()
                    except FileNotFoundError:
                        pass


class _NoRedirect(urlrequest.HTTPRedirectHandler):
    def redirect_request(
        self,
        req: urlrequest.Request,
        fp: BinaryIO,
        code: int,
        msg: str,
        headers: Mapping[str, str],
        newurl: str,
    ) -> None:
        del req, fp, code, msg, headers, newurl
        return None


def _post_json(url: str, value: Mapping[str, Any]) -> tuple[int, dict[str, Any]]:
    encoded = _canonical_json(value)
    if len(encoded) > MAX_CONFIG_BYTES:
        raise RefreshError("publication request is too large")
    request = urlrequest.Request(
        url,
        data=encoded,
        method="POST",
        headers={
            "accept": "application/json",
            "content-type": "application/json",
            "user-agent": "ForkMesh-headless-refresh/1.0",
        },
    )
    opener = urlrequest.build_opener(
        _NoRedirect(),
        urlrequest.ProxyHandler({}),
    )
    try:
        with opener.open(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            status = int(response.status)
            try:
                announced = int(response.headers.get("content-length") or 0)
            except (TypeError, ValueError):
                announced = 0
            if announced > MAX_HTTP_RESPONSE_BYTES:
                raise RefreshError("publication response is too large")
            raw = response.read(MAX_HTTP_RESPONSE_BYTES + 1)
    except urlerror.HTTPError as exc:
        # Do not read or surface the remote body; it may echo a public identity
        # or an operator-supplied field.
        raise RefreshError("publication endpoint rejected the request") from exc
    except (urlerror.URLError, TimeoutError, OSError, http.client.HTTPException) as exc:
        raise RefreshError("publication endpoint is unavailable") from exc
    if len(raw) > MAX_HTTP_RESPONSE_BYTES:
        raise RefreshError("publication response is too large")
    return status, _parse_json(
        raw,
        maximum=MAX_HTTP_RESPONSE_BYTES,
        label="publication response",
    )


def _sign_endpoint(
    config: RefreshConfig,
    identity: PublicIdentity,
) -> dict[str, Any]:
    response = _helper_call(
        config,
        "sign-endpoint-registration",
        {
            "schemaVersion": 1,
            "type": "forkmesh.https-endpoint-registration-signing",
            "node": config.node_owner,
            "baseUrl": config.public_origin,
        },
    )
    _expect_fields(
        response,
        {"node", "baseUrl", "publicKey", "issuedAt", "signature"},
        label="endpoint signing response",
    )
    signature = str(response.get("signature") or "")
    if (
        response.get("node") != config.node_owner
        or response.get("baseUrl") != config.public_origin
        or response.get("publicKey") != identity.node_public_key
        or isinstance(response.get("issuedAt"), bool)
        or not isinstance(response.get("issuedAt"), int)
        or response.get("issuedAt", 0) <= 0
        or not re.fullmatch(r"[A-Za-z0-9_-]{86}", signature)
    ):
        raise RefreshError("endpoint signing response is invalid")
    return response


MAX_PUBLIC_TELEMETRY_BYTES = 1 << 50
MAX_PROC_METRIC_BYTES = 64 * 1024


def _read_proc_metric(path: Path) -> bytes | None:
    """Read one fixed Linux proc metric without following a replacement link."""
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError:
        return None
    try:
        data = os.read(descriptor, MAX_PROC_METRIC_BYTES + 1)
    except OSError:
        return None
    finally:
        os.close(descriptor)
    if not data or len(data) > MAX_PROC_METRIC_BYTES:
        return None
    return data


def _proc_cpu_snapshot(raw: bytes | None) -> tuple[int, int] | None:
    """Return Linux aggregate (idle ticks, total ticks) from /proc/stat."""
    if not raw:
        return None
    try:
        first = raw.decode("ascii", "strict").splitlines()[0].split()
    except (UnicodeDecodeError, IndexError):
        return None
    if not first or first[0] != "cpu" or len(first) < 5:
        return None
    values = []
    for item in first[1:9]:
        if not re.fullmatch(r"[0-9]{1,20}", item):
            return None
        number = int(item)
        if number > (1 << 63) - 1:
            return None
        values.append(number)
    if len(values) < 4:
        return None
    total = sum(values)
    idle = values[3] + (values[4] if len(values) > 4 else 0)
    return (idle, total) if total > 0 and idle <= total else None


def _sample_linux_cpu(
    *,
    read_metric: Callable[[Path], bytes | None] = _read_proc_metric,
    sleeper: Callable[[float], None] = time.sleep,
) -> int | None:
    first = _proc_cpu_snapshot(read_metric(Path("/proc/stat")))
    if first is None:
        return None
    sleeper(0.1)
    second = _proc_cpu_snapshot(read_metric(Path("/proc/stat")))
    if second is None:
        return None
    idle_delta = second[0] - first[0]
    total_delta = second[1] - first[1]
    if total_delta <= 0 or idle_delta < 0 or idle_delta > total_delta:
        return None
    busy_delta = total_delta - idle_delta
    return max(0, min(100, (busy_delta * 100 + total_delta // 2) // total_delta))


def _parse_linux_memory(raw: bytes | None) -> tuple[int, int] | None:
    """Return used/total bytes from bounded MemTotal and MemAvailable values."""
    if not raw:
        return None
    values: dict[str, int] = {}
    try:
        lines = raw.decode("ascii", "strict").splitlines()
    except UnicodeDecodeError:
        return None
    for line in lines:
        match = re.fullmatch(
            r"(MemTotal|MemAvailable):[ \t]+([0-9]{1,20})[ \t]+kB[ \t]*",
            line,
        )
        if not match:
            continue
        kibibytes = int(match.group(2))
        if kibibytes > MAX_PUBLIC_TELEMETRY_BYTES // 1024:
            return None
        values[match.group(1)] = kibibytes * 1024
    total = values.get("MemTotal")
    available = values.get("MemAvailable")
    if (
        total is None
        or available is None
        or total <= 0
        or available < 0
        or available > total
    ):
        return None
    return total - available, total


def _sample_linux_memory(
    *,
    read_metric: Callable[[Path], bytes | None] = _read_proc_metric,
) -> tuple[int, int] | None:
    return _parse_linux_memory(read_metric(Path("/proc/meminfo")))


def _sample_linux_disk(
    path: Path,
    *,
    statvfs: Callable[[Path], Any] = os.statvfs,
) -> tuple[int, int] | None:
    try:
        info = statvfs(path)
        block_size = int(info.f_frsize or info.f_bsize)
        blocks = int(info.f_blocks)
        available_blocks = int(info.f_bavail)
    except (AttributeError, OSError, TypeError, ValueError, OverflowError):
        return None
    if (
        block_size <= 0
        or blocks <= 0
        or available_blocks < 0
        or available_blocks > blocks
    ):
        return None
    total_raw = blocks * block_size
    used_raw = total_raw - available_blocks * block_size
    total = min(total_raw, MAX_PUBLIC_TELEMETRY_BYTES)
    return min(max(0, used_raw), total), total


def _bounded_optional_metric(value: Any, maximum: int) -> int | None:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        return None
    return min(value, maximum)


def _bounded_optional_usage(value: Any) -> tuple[int, int] | None:
    if not isinstance(value, tuple) or len(value) != 2:
        return None
    used = _bounded_optional_metric(value[0], MAX_PUBLIC_TELEMETRY_BYTES)
    total = _bounded_optional_metric(value[1], MAX_PUBLIC_TELEMETRY_BYTES)
    if used is None or total is None or total <= 0:
        return None
    return min(used, total), total


def _sample_host_telemetry(
    config: RefreshConfig,
    *,
    cpu_sampler: Callable[[], int | None] = _sample_linux_cpu,
    memory_sampler: Callable[[], tuple[int, int] | None] = _sample_linux_memory,
    disk_sampler: Callable[[Path], tuple[int, int] | None] = _sample_linux_disk,
    platform: str = sys.platform,
) -> dict[str, int]:
    """Collect only explicitly enabled public host metrics.

    Sampling is best-effort and content-free. A disabled, unsupported, or
    malformed reading is omitted, which the catalog/World represents as
    unknown rather than zero.
    """
    if not platform.startswith("linux"):
        return {}
    result: dict[str, int] = {}
    if config.catalog.report_cpu:
        try:
            cpu = _bounded_optional_metric(cpu_sampler(), 100)
        except Exception:
            cpu = None
        if cpu is not None:
            result["cpuPercent"] = cpu
    if config.catalog.report_memory:
        try:
            memory = _bounded_optional_usage(memory_sampler())
        except Exception:
            memory = None
        if memory is not None:
            result["memUsedBytes"], result["memTotalBytes"] = memory
    if config.catalog.report_disk:
        try:
            disk = _bounded_optional_usage(
                disk_sampler(config.source_repository))
        except Exception:
            disk = None
        if disk is not None:
            result["diskUsedBytes"], result["diskTotalBytes"] = disk
    return result


def _catalog_unsigned(
    config: RefreshConfig,
    metadata: SealMetadata,
    now_ms: int,
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "owner": config.node_owner,
        "name": config.repository_name,
        "visibility": "public",
        "sizeBytes": metadata.ciphertext_bytes,
        "description": config.catalog.description,
        "cloneUrl": (
            config.worker_origin
            + "/"
            + quote(config.node_owner, safe="")
            + "/"
            + quote(config.repository_name, safe="")
        ),
        "channel": (
            config.catalog.channel
            or "#" + config.node_owner + "-" + config.repository_name
        ),
        "lastSync": str(now_ms),
        "updatedAt": str(now_ms),
        "source": "remote-clone",
        "branch": config.catalog.branch,
        "platform": config.catalog.platform,
        "version": config.catalog.version,
        "nodeId": config.node_owner,
        "stateHash": metadata.expected_refs_sha256,
        "commit": _source_branch_commit(config),
    }
    if config.catalog.solana:
        record["solana"] = config.catalog.solana
    if config.catalog.hosted_since:
        record["hostedSince"] = config.catalog.hosted_since
    record.update(_sample_repository_statistics(config))
    record.update(_sample_host_telemetry(config))
    return record


def _sign_catalog(
    config: RefreshConfig,
    identity: PublicIdentity,
    metadata: SealMetadata,
) -> dict[str, Any]:
    unsigned = _catalog_unsigned(
        config,
        metadata,
        time.time_ns() // 1_000_000,
    )
    response = _helper_call(
        config,
        "sign-catalog-v2",
        {
            "schemaVersion": 1,
            "type": "forkmesh.catalog-v2-publication-signing",
            "record": unsigned,
        },
    )
    if (
        response.get("owner") != config.node_owner
        or response.get("name") != config.repository_name
        or response.get("visibility") != "public"
        or response.get("maintainer") != identity.node_public_key
        or response.get("stateHash") != metadata.expected_refs_sha256
        or response.get("catalogSigVersion") != 2
        or not re.fullmatch(
            r"[A-Za-z0-9_-]{86}", str(response.get("catalogSig") or "")
        )
        or not re.fullmatch(
            r"[A-Za-z0-9_-]{86}", str(response.get("stateSig") or "")
        )
    ):
        raise RefreshError("catalog signing response is invalid")
    return response


def _publish_registration(
    config: RefreshConfig,
    identity: PublicIdentity,
    metadata: SealMetadata,
    *,
    post_json: Callable[
        [str, Mapping[str, Any]], tuple[int, dict[str, Any]]
    ] = _post_json,
) -> dict[str, Any]:
    endpoint = _sign_endpoint(config, identity)
    endpoint_status, endpoint_response = post_json(
        config.worker_origin + "/api/mirrors/https",
        endpoint,
    )
    if (
        endpoint_status not in {200, 201}
        or endpoint_response.get("ok") is not True
        or endpoint_response.get("node") != config.node_owner
        or endpoint_response.get("baseUrl") != config.public_origin
    ):
        raise RefreshError("endpoint registration was not accepted")

    catalog = _sign_catalog(config, identity, metadata)
    catalog_status, catalog_response = post_json(
        config.worker_origin + "/api/repositories",
        catalog,
    )
    repository = catalog_response.get("repository")
    if (
        catalog_status not in {200, 201}
        or catalog_response.get("ok") is not True
        or not isinstance(repository, dict)
        or repository.get("owner") != config.node_owner
        or repository.get("name") != config.repository_name
        or repository.get("stateHash") != metadata.expected_refs_sha256
    ):
        raise RefreshError("catalog publication was not accepted")

    # A new canonical state can legitimately leave the first endpoint
    # challenge pending: the endpoint is registered before its catalog write
    # advances the public state pin. Re-register once after that durable write
    # and require the Worker to confirm a fresh signed proof against the final
    # pin. Normal renewals already return active and avoid this extra request.
    if endpoint_response.get("health") != "active":
        endpoint_status, endpoint_response = post_json(
            config.worker_origin + "/api/mirrors/https",
            endpoint,
        )
        if (
            endpoint_status not in {200, 201}
            or endpoint_response.get("ok") is not True
            or endpoint_response.get("node") != config.node_owner
            or endpoint_response.get("baseUrl") != config.public_origin
            or endpoint_response.get("health") != "active"
        ):
            raise RefreshError(
                "endpoint registration did not activate signed health")
    return {
        "ok": True,
        "aliasCount": len(config.owner_aliases),
    }


def register(
    config: RefreshConfig,
    *,
    post_json: Callable[
        [str, Mapping[str, Any]], tuple[int, dict[str, Any]]
    ] = _post_json,
) -> dict[str, Any]:
    with _refresh_lock(config, shared=False):
        identity, metadata, _archive_path = _validate_active(config)
        result = _publish_registration(
            config, identity, metadata, post_json=post_json)
        result["event"] = "registration_complete"
        return result


def renew(
    config: RefreshConfig,
    *,
    post_json: Callable[
        [str, Mapping[str, Any]], tuple[int, dict[str, Any]]
    ] = _post_json,
) -> dict[str, Any]:
    with _refresh_lock(config, shared=False):
        identity, metadata, _archive_path = _validate_active_renewal(config)
        result = _publish_registration(
            config, identity, metadata, post_json=post_json)
        result["event"] = "renewal_complete"
        return result


def check(config: RefreshConfig) -> dict[str, Any]:
    with _refresh_lock(config, shared=True):
        _validate_active(config)
        return {
            "ok": True,
            "event": "check_complete",
            "aliasCount": len(config.owner_aliases),
            "networkRequests": 0,
        }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Refresh and publish one encrypted headless ForkMesh mirror "
            "without loading node, wallet, or repository-decryption secrets"
        )
    )
    parser.add_argument(
        "--config",
        required=True,
        type=Path,
        help="absolute owner-only refresh JSON configuration",
    )
    parser.add_argument(
        "mode", choices=("refresh", "check", "register", "renew"))
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        config = load_config(args.config)
        result = {
            "refresh": refresh,
            "check": check,
            "register": register,
            "renew": renew,
        }[args.mode](config)
        print(_canonical_json(result).decode("utf-8"), flush=True)
        return 0
    except RefreshError as exc:
        print(f"headless mirror refresh: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("headless mirror refresh: interrupted", file=sys.stderr)
        return 130
    except Exception:
        # Unexpected library/OS exceptions can interpolate paths or request
        # values.  Fail closed with a fixed message instead of leaking them.
        print("headless mirror refresh: unexpected local failure", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
