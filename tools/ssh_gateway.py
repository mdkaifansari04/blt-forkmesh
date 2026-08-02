#!/usr/bin/env python3
"""Fail-closed OpenSSH forced-command gateway for ForkMesh Git repositories.

The Cloudflare HTTP Worker cannot terminate raw SSH.  This helper is installed
on an independently operated SSH origin and used in two modes:

* ``authorized-key`` is called by sshd's ``AuthorizedKeysCommand``. It asks the
  Worker whether the presented public key is active, then emits that same key
  with a restrictive forced command.
* ``serve`` receives only the forced key id plus ``SSH_ORIGINAL_COMMAND``. It
  asks the Worker to authorize that principal for the exact repository and Git
  operation, resolves the result through a local allowlist, and execs Git
  without a shell.

No private SSH key, repository bytes, requested path, or bearer token is logged.
"""

from __future__ import annotations

import argparse
import base64
from contextlib import ExitStack
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import signal
import socket
import stat
import struct
import subprocess
import sys
import time
from typing import Any


CONFIG_VERSION = 1
MAX_CONFIG_BYTES = 1024 * 1024
MAX_BROKER_FRAME_BYTES = 64 * 1024
BROKER_TIMEOUT_SECONDS = 12
MAX_ORIGINAL_COMMAND_BYTES = 512
MAX_STORAGE_SCAN_ENTRIES = 1_000_000
DEFAULT_LIMITS = {
    "capacityDirectory": "/run/forkmesh-ssh-gateway",
    "maxConcurrentSessions": 16,
    "reservedReceiveSessions": 4,
    "maxConcurrentProcesses": 12,
    "reservedReceiveProcesses": 3,
    "uploadPackDeadlineSeconds": 900,
    "receivePackDeadlineSeconds": 600,
    "terminationGraceSeconds": 3,
    "receiveMaxInputBytes": 256 * 1024 * 1024,
    "defaultRepositoryMaxBytes": 16 * 1024 * 1024 * 1024,
    "storageScanDeadlineSeconds": 10,
    "cleanupDeadlineSeconds": 5,
    "maxPackThreads": 2,
}
LIMIT_RANGES = {
    "maxConcurrentSessions": (2, 256),
    "reservedReceiveSessions": (1, 255),
    "maxConcurrentProcesses": (2, 256),
    "reservedReceiveProcesses": (1, 255),
    "uploadPackDeadlineSeconds": (5, 3600),
    "receivePackDeadlineSeconds": (5, 3600),
    "terminationGraceSeconds": (1, 30),
    "receiveMaxInputBytes": (1024 * 1024, 4 * 1024 * 1024 * 1024),
    "defaultRepositoryMaxBytes": (
        16 * 1024 * 1024,
        1024 * 1024 * 1024 * 1024,
    ),
    "storageScanDeadlineSeconds": (1, 60),
    "cleanupDeadlineSeconds": (1, 30),
    "maxPackThreads": (1, 8),
}
OWNER_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
KEY_ID_RE = re.compile(r"^sk_[A-Za-z0-9_-]{16,80}$")
KEY_TYPE_RE = re.compile(r"^[A-Za-z0-9@._+-]{1,80}$")
EXECUTABLE_RE = re.compile(r"^/[A-Za-z0-9_./+-]{1,511}$")
SUPPORTED_KEY_TYPES = frozenset(
    {
        "ssh-ed25519",
        "sk-ssh-ed25519@openssh.com",
        "ecdsa-sha2-nistp256",
        "ecdsa-sha2-nistp384",
        "ecdsa-sha2-nistp521",
        "sk-ecdsa-sha2-nistp256@openssh.com",
        "ssh-rsa",
    }
)
ORIGINAL_COMMAND_RE = re.compile(r"^git-(upload|receive)-pack\s+(.+)$")
INTERNAL_GIT_REF_NAMESPACE = "refs/forkmesh/"


class GatewayError(RuntimeError):
    """A safe, generic gateway failure."""


class GatewayLimits:
    """Validated, fixed limits inherited by every forced Git command."""

    def __init__(self, source: object = None):
        values = dict(DEFAULT_LIMITS)
        if source is not None:
            if not isinstance(source, dict) or set(source) != set(DEFAULT_LIMITS):
                raise GatewayError("invalid gateway limits")
            values.update(source)
        capacity_directory = Path(str(values.get("capacityDirectory") or ""))
        if (
            not capacity_directory.is_absolute()
            or "\x00" in str(capacity_directory)
            or any(part in ("", ".", "..") for part in capacity_directory.parts)
        ):
            raise GatewayError("invalid gateway limits")
        self.capacity_directory = capacity_directory
        for name, (minimum, maximum) in LIMIT_RANGES.items():
            value = values.get(name)
            if isinstance(value, bool) or not isinstance(value, int):
                raise GatewayError("invalid gateway limits")
            if value < minimum or value > maximum:
                raise GatewayError("invalid gateway limits")
            setattr(self, _snake_case(name), value)
        if (
            self.reserved_receive_sessions >= self.max_concurrent_sessions
            or self.reserved_receive_processes >= self.max_concurrent_processes
            or self.default_repository_max_bytes
            < self.receive_max_input_bytes + 1024 * 1024
        ):
            raise GatewayError("invalid gateway limits")


def _snake_case(value: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", value).lower()


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise GatewayError("invalid gateway configuration")
        value[key] = item
    return value


def _read_json(path: Path) -> dict[str, Any]:
    descriptor = -1
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        info = os.fstat(descriptor)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid not in (0, os.geteuid())
            or info.st_nlink != 1
            or info.st_mode & 0o022
            or info.st_size <= 0
            or info.st_size > MAX_CONFIG_BYTES
        ):
            raise GatewayError("invalid gateway configuration")
        chunks = bytearray()
        while len(chunks) <= MAX_CONFIG_BYTES:
            chunk = os.read(
                descriptor,
                min(64 * 1024, MAX_CONFIG_BYTES + 1 - len(chunks)),
            )
            if not chunk:
                break
            chunks.extend(chunk)
        if len(chunks) != info.st_size or len(chunks) > MAX_CONFIG_BYTES:
            raise GatewayError("invalid gateway configuration")
        raw = bytes(chunks)
        value = json.loads(raw, object_pairs_hook=_reject_duplicate_keys)
    except GatewayError:
        raise
    except (OSError, ValueError, TypeError) as error:
        raise GatewayError("invalid gateway configuration") from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if not isinstance(value, dict):
        raise GatewayError("invalid gateway configuration")
    return value


def _safe_relative_path(value: object) -> Path:
    raw = str(value or "").strip().replace("\\", "/")
    candidate = Path(raw)
    if (
        not raw
        or candidate.is_absolute()
        or any(part in ("", ".", "..") for part in candidate.parts)
    ):
        raise GatewayError("invalid repository allowlist")
    return candidate


def _gateway_command(value: object) -> str:
    command = str(value or "").strip()
    if not EXECUTABLE_RE.fullmatch(command) or any(
        part in ("", ".", "..") for part in command.split("/")[1:]
    ):
        raise GatewayError("invalid gateway executable")
    return command


def _normalize_presented_key(key_type: object, encoded: object) -> str:
    clean_type = str(key_type or "").strip()
    clean_encoded = str(encoded or "").strip()
    if (
        clean_type not in SUPPORTED_KEY_TYPES
        or not KEY_TYPE_RE.fullmatch(clean_type)
        or not clean_encoded
        or len(clean_encoded) > 16 * 1024
        or any(char in clean_encoded for char in "\r\n\x00")
    ):
        raise GatewayError("public key is not authorized")
    try:
        blob = base64.b64decode(clean_encoded, validate=True)
    except ValueError as error:
        raise GatewayError("public key is not authorized") from error
    if not blob:
        raise GatewayError("public key is not authorized")
    return clean_type + " " + base64.b64encode(blob).decode("ascii")


class Config:
    def __init__(self, path: Path):
        source = _read_json(path)
        required_fields = {
            "schemaVersion",
            "authorizationSocket",
            "authorizationBrokerUser",
            "gatewayExecutable",
            "refreshNotifier",
            "repositoryRoot",
            "repositories",
        }
        if set(source) not in (required_fields, required_fields | {"limits"}):
            raise GatewayError("invalid gateway configuration")
        if source.get("schemaVersion") != CONFIG_VERSION:
            raise GatewayError("unsupported gateway configuration")
        self.limits = (
            GatewayLimits(source["limits"])
            if "limits" in source
            else GatewayLimits()
        )
        self.gateway_command = _gateway_command(source.get("gatewayExecutable"))
        gateway_path = Path(self.gateway_command)
        try:
            gateway_info = gateway_path.lstat()
        except OSError as error:
            raise GatewayError("invalid gateway executable") from error
        if (
            not stat.S_ISREG(gateway_info.st_mode)
            or stat.S_ISLNK(gateway_info.st_mode)
            or gateway_info.st_uid != 0
            or stat.S_IMODE(gateway_info.st_mode) & 0o022
            or not os.access(gateway_path, os.X_OK)
        ):
            raise GatewayError("invalid gateway executable")
        self.refresh_notifier = Path(_gateway_command(source.get("refreshNotifier")))
        try:
            notifier_info = self.refresh_notifier.lstat()
        except OSError as error:
            raise GatewayError("invalid refresh notifier") from error
        if (
            not stat.S_ISREG(notifier_info.st_mode)
            or stat.S_ISLNK(notifier_info.st_mode)
            or notifier_info.st_uid != 0
            or stat.S_IMODE(notifier_info.st_mode) & 0o022
            or not os.access(self.refresh_notifier, os.X_OK)
        ):
            raise GatewayError("invalid refresh notifier")
        socket_path = Path(str(source.get("authorizationSocket") or ""))
        if (
            not socket_path.is_absolute()
            or socket_path.name in {"", ".", ".."}
            or "\x00" in str(socket_path)
        ):
            raise GatewayError("invalid authorization socket")
        self.authorization_socket = socket_path
        broker_user = str(source.get("authorizationBrokerUser") or "")
        try:
            broker = pwd.getpwnam(broker_user)
        except KeyError as error:
            raise GatewayError("authorization broker is unavailable") from error
        if broker.pw_uid == 0:
            raise GatewayError("authorization broker is unavailable")
        self.authorization_broker_uid = broker.pw_uid
        root = Path(str(source.get("repositoryRoot") or "").strip())
        if not root.is_absolute():
            raise GatewayError("invalid repository root")
        try:
            root_info = root.lstat()
        except OSError as error:
            raise GatewayError("invalid repository root") from error
        if (
            not stat.S_ISDIR(root_info.st_mode)
            or stat.S_ISLNK(root_info.st_mode)
            or root_info.st_uid not in (0, os.geteuid())
            or (root_info.st_mode & 0o022)
        ):
            raise GatewayError("invalid repository root")
        self.repository_root = root.resolve()
        raw_repositories = source.get("repositories")
        if not isinstance(raw_repositories, list) or len(raw_repositories) > 1000:
            raise GatewayError("invalid repository allowlist")
        self.repositories: dict[tuple[str, str], tuple[Path, bool, int]] = {}
        quotas_by_path: dict[Path, int] = {}
        for item in raw_repositories:
            required_item_fields = {
                "owner",
                "name",
                "path",
                "access",
            }
            if (
                not isinstance(item, dict)
                or set(item) not in (
                    required_item_fields,
                    required_item_fields | {"maxStorageBytes"},
                )
            ):
                raise GatewayError("invalid repository allowlist")
            owner = str(item.get("owner") or "").strip().lower()
            name = str(item.get("name") or "").strip()
            access = str(item.get("access") or "read-only").strip().lower()
            max_storage_bytes = item.get(
                "maxStorageBytes",
                self.limits.default_repository_max_bytes,
            )
            if (
                not OWNER_RE.fullmatch(owner)
                or not REPO_RE.fullmatch(name)
                or access not in ("read-only", "read-write")
                or isinstance(max_storage_bytes, bool)
                or not isinstance(max_storage_bytes, int)
                or max_storage_bytes
                < self.limits.receive_max_input_bytes + 1024 * 1024
                or max_storage_bytes > LIMIT_RANGES["defaultRepositoryMaxBytes"][1]
            ):
                raise GatewayError("invalid repository allowlist")
            relative = _safe_relative_path(item.get("path"))
            target = self.repository_root / relative
            key = (owner, name.lower())
            if key in self.repositories:
                raise GatewayError("duplicate repository allowlist entry")
            previous_quota = quotas_by_path.setdefault(target, max_storage_bytes)
            if previous_quota != max_storage_bytes:
                # Canonical aliases to one repository must not be a quota bypass.
                raise GatewayError("invalid repository allowlist")
            self.repositories[key] = (
                target,
                access == "read-write",
                max_storage_bytes,
            )


def _read_exact(connection: socket.socket, size: int) -> bytes:
    content = bytearray()
    while len(content) < size:
        chunk = connection.recv(size - len(content))
        if not chunk:
            raise GatewayError("authorization broker response is incomplete")
        content.extend(chunk)
    return bytes(content)


def _api(config: Config, payload: dict[str, Any]) -> dict[str, Any]:
    body = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if not body or len(body) > MAX_BROKER_FRAME_BYTES:
        raise GatewayError("authorization request is invalid")
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(BROKER_TIMEOUT_SECONDS)
            connection.connect(str(config.authorization_socket))
            if not hasattr(socket, "SO_PEERCRED"):
                raise GatewayError("authorization broker is unavailable")
            credentials = connection.getsockopt(
                socket.SOL_SOCKET,
                socket.SO_PEERCRED,
                struct.calcsize("3i"),
            )
            _pid, peer_uid, _gid = struct.unpack("3i", credentials)
            if peer_uid != config.authorization_broker_uid:
                raise GatewayError("authorization broker is unavailable")
            connection.sendall(struct.pack("!I", len(body)) + body)
            connection.shutdown(socket.SHUT_WR)
            announced = struct.unpack("!I", _read_exact(connection, 4))[0]
            if announced <= 0 or announced > MAX_BROKER_FRAME_BYTES:
                raise GatewayError("authorization broker response is invalid")
            raw = _read_exact(connection, announced)
            if connection.recv(1):
                raise GatewayError("authorization broker response is invalid")
    except (OSError, TimeoutError, struct.error) as error:
        raise GatewayError("authorization broker is unavailable") from error
    try:
        value = json.loads(raw.decode("utf-8"))
    except (TypeError, ValueError) as error:
        raise GatewayError("authorization broker response is invalid") from error
    if not isinstance(value, dict) or value.get("authorized") is not True:
        raise GatewayError("authorization denied")
    return value


def _authorized_key(config: Config, key_type: str, key_blob: str) -> int:
    public_key = _normalize_presented_key(key_type, key_blob)
    result = _api(config, {"action": "lookup", "publicKey": public_key})
    key_id = str(result.get("keyId") or "")
    if not KEY_ID_RE.fullmatch(key_id):
        raise GatewayError("authorization service response is invalid")
    options = (
        'restrict,command="'
        + config.gateway_command
        + " serve --key-id "
        + key_id
        + '"'
    )
    print(options + " " + public_key)
    return 0


def _parse_original_command(value: object) -> tuple[str, str, str]:
    raw = str(value or "")
    if len(raw.encode("utf-8", errors="ignore")) > MAX_ORIGINAL_COMMAND_BYTES:
        raise GatewayError("invalid Git repository request")
    match = ORIGINAL_COMMAND_RE.fullmatch(raw)
    if not match:
        raise GatewayError("only Git SSH operations are allowed")
    operation = "git-" + match.group(1) + "-pack"
    try:
        path_parts = shlex.split(match.group(2), posix=True)
    except ValueError as error:
        raise GatewayError("invalid Git repository request") from error
    if len(path_parts) != 1:
        raise GatewayError("invalid Git repository request")
    path = path_parts[0].strip().lstrip("/")
    if path.endswith(".git"):
        path = path[:-4]
    pieces = path.split("/")
    if (
        len(pieces) != 2
        or not OWNER_RE.fullmatch(pieces[0])
        or not REPO_RE.fullmatch(pieces[1])
    ):
        raise GatewayError("invalid Git repository request")
    return operation, pieces[0].lower(), pieces[1]


def _bare_repository(path: Path) -> bool:
    try:
        result = subprocess.run(
            ["git", "--git-dir", str(path), "rev-parse", "--is-bare-repository"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=5,
            check=False,
            env={
                "PATH": "/usr/bin:/bin",
                "LANG": "C",
                "LC_ALL": "C",
                "GIT_CONFIG_NOSYSTEM": "1",
                "GIT_CONFIG_GLOBAL": os.devnull,
                "GIT_TERMINAL_PROMPT": "0",
            },
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and result.stdout.strip() == b"true"


class _SlotLease:
    def __init__(self, descriptor: int):
        self.descriptor = descriptor

    def close(self) -> None:
        if self.descriptor >= 0:
            try:
                fcntl.flock(self.descriptor, fcntl.LOCK_UN)
            finally:
                os.close(self.descriptor)
                self.descriptor = -1

    def __enter__(self):
        return self

    def __exit__(self, _kind, _value, _traceback):
        self.close()


def _limits(config: Config) -> GatewayLimits:
    value = getattr(config, "limits", None)
    return value if isinstance(value, GatewayLimits) else GatewayLimits()


def _repository_entry(
    config: Config,
    entry: object,
) -> tuple[Path, bool, int]:
    if not isinstance(entry, tuple) or len(entry) not in (2, 3):
        raise GatewayError("invalid repository allowlist")
    path = entry[0]
    writable = entry[1]
    if not isinstance(path, Path) or not isinstance(writable, bool):
        raise GatewayError("invalid repository allowlist")
    quota = (
        entry[2]
        if len(entry) == 3
        else _limits(config).default_repository_max_bytes
    )
    if isinstance(quota, bool) or not isinstance(quota, int) or quota <= 0:
        raise GatewayError("invalid repository allowlist")
    return path, writable, quota


def _capacity_directory(path: Path) -> Path:
    try:
        info = path.lstat()
    except OSError as error:
        raise GatewayError("Git service capacity is unavailable") from error
    if (
        not stat.S_ISDIR(info.st_mode)
        or stat.S_ISLNK(info.st_mode)
        or info.st_uid != os.geteuid()
        or info.st_mode & 0o077
    ):
        raise GatewayError("Git service capacity is unavailable")
    return path


def _acquire_slot(
    directory: Path,
    prefix: str,
    *,
    maximum: int,
    reserved_receive: int,
    receive: bool,
) -> _SlotLease:
    directory = _capacity_directory(directory)
    general = maximum - reserved_receive
    indices = (
        list(range(general, maximum)) + list(range(general))
        if receive
        else list(range(general))
    )
    for index in indices:
        target = directory / (prefix + "-" + str(index) + ".lock")
        descriptor = -1
        try:
            descriptor = os.open(
                target,
                os.O_RDWR
                | os.O_CREAT
                | getattr(os, "O_CLOEXEC", 0)
                | getattr(os, "O_NOFOLLOW", 0),
                0o600,
            )
            info = os.fstat(descriptor)
            if (
                not stat.S_ISREG(info.st_mode)
                or info.st_uid != os.geteuid()
                or info.st_nlink != 1
                or stat.S_IMODE(info.st_mode) != 0o600
            ):
                raise GatewayError("Git service capacity is unavailable")
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                os.close(descriptor)
                descriptor = -1
                continue
            return _SlotLease(descriptor)
        except GatewayError:
            if descriptor >= 0:
                os.close(descriptor)
            raise
        except OSError as error:
            if descriptor >= 0:
                os.close(descriptor)
            raise GatewayError("Git service capacity is unavailable") from error
    raise GatewayError("Git service is at capacity")


def _acquire_repository_write_slot(directory: Path, repository_path: Path) -> _SlotLease:
    digest = hashlib.sha256(str(repository_path).encode("utf-8")).hexdigest()[:32]
    return _acquire_slot(
        directory,
        "write-" + digest,
        maximum=1,
        reserved_receive=0,
        receive=False,
    )


def _repository_size_bytes(
    repository_path: Path,
    deadline_seconds: int,
    *,
    monotonic=time.monotonic,
) -> int:
    deadline = monotonic() + deadline_seconds
    entries = 0
    total = 0
    stack = [repository_path]
    try:
        while stack:
            if monotonic() >= deadline:
                raise GatewayError("repository storage check timed out")
            directory = stack.pop()
            with os.scandir(directory) as children:
                for child in children:
                    entries += 1
                    if entries > MAX_STORAGE_SCAN_ENTRIES:
                        raise GatewayError("repository storage check is too large")
                    info = child.stat(follow_symlinks=False)
                    if stat.S_ISLNK(info.st_mode):
                        raise GatewayError("repository storage check failed")
                    # Logical size prevents sparse files from bypassing a quota;
                    # allocated size accounts for metadata-heavy object stores.
                    total += max(
                        int(info.st_size),
                        int(getattr(info, "st_blocks", 0)) * 512,
                    )
                    if stat.S_ISDIR(info.st_mode):
                        stack.append(Path(child.path))
            if monotonic() >= deadline:
                raise GatewayError("repository storage check timed out")
    except GatewayError:
        raise
    except OSError as error:
        raise GatewayError("repository storage check failed") from error
    return total


def _receive_artifacts(
    repository_path: Path,
    deadline_seconds: int,
    *,
    monotonic=time.monotonic,
) -> dict[str, tuple[int, int]]:
    """Snapshot only Git transaction artifacts that this process may clean."""

    artifacts: dict[str, tuple[int, int]] = {}
    deadline = monotonic() + deadline_seconds
    visited = 0

    def record(path: Path) -> None:
        nonlocal visited
        visited += 1
        if visited > MAX_STORAGE_SCAN_ENTRIES or monotonic() >= deadline:
            raise GatewayError("repository transaction scan timed out")
        try:
            info = path.lstat()
        except OSError:
            return
        artifacts[str(path.relative_to(repository_path))] = (
            int(info.st_dev),
            int(info.st_ino),
        )

    for name in ("HEAD.lock", "packed-refs.lock", "shallow.lock"):
        record(repository_path / name)
    objects = repository_path / "objects"
    try:
        for child in objects.iterdir():
            if child.name.startswith(("incoming-", "tmp_")):
                record(child)
    except OSError:
        pass
    try:
        for child in (objects / "pack").iterdir():
            if child.name.startswith("tmp_"):
                record(child)
    except OSError:
        pass
    for root in (repository_path / "refs", repository_path / "logs" / "refs"):
        if not root.is_dir():
            continue
        for directory, names, files in os.walk(root, followlinks=False):
            visited += 1
            if visited > MAX_STORAGE_SCAN_ENTRIES or monotonic() >= deadline:
                raise GatewayError("repository transaction scan timed out")
            # Never follow a repository-created symlink while taking a cleanup
            # snapshot. A symlinked subtree is not a Git receive artifact.
            names[:] = [
                name
                for name in names
                if not (Path(directory) / name).is_symlink()
            ]
            for name in files:
                if name.endswith(".lock"):
                    record(Path(directory) / name)
    return artifacts


def _cleanup_interrupted_receive(
    repository_path: Path,
    before: dict[str, tuple[int, int]],
    deadline_seconds: int,
    *,
    monotonic=time.monotonic,
) -> None:
    deadline = monotonic() + deadline_seconds
    try:
        after = _receive_artifacts(
            repository_path,
            deadline_seconds,
            monotonic=monotonic,
        )
    except GatewayError:
        return
    for relative, identity in after.items():
        if monotonic() >= deadline:
            return
        if before.get(relative) == identity:
            continue
        target = repository_path / relative
        try:
            info = target.lstat()
            if (int(info.st_dev), int(info.st_ino)) != identity:
                continue
            if stat.S_ISLNK(info.st_mode):
                target.unlink()
            elif stat.S_ISDIR(info.st_mode):
                _bounded_remove_tree(target, deadline, monotonic=monotonic)
            elif stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
                target.unlink()
        except OSError:
            # Cleanup is deliberately best-effort after the entire process
            # group is gone. A later repository maintenance pass may reap an
            # artifact that could not be removed safely here.
            continue


def _bounded_remove_tree(
    root: Path,
    deadline: float,
    *,
    monotonic=time.monotonic,
) -> None:
    stack: list[tuple[Path, bool]] = [(root, False)]
    visited = 0
    while stack:
        if monotonic() >= deadline or visited >= MAX_STORAGE_SCAN_ENTRIES:
            return
        target, children_seen = stack.pop()
        visited += 1
        try:
            info = target.lstat()
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode):
                target.unlink()
                continue
            if children_seen:
                target.rmdir()
                continue
            stack.append((target, True))
            with os.scandir(target) as children:
                for child in children:
                    if monotonic() >= deadline:
                        return
                    stack.append((Path(child.path), False))
        except OSError:
            continue


def _terminate_process_group(
    process: subprocess.Popen,
    grace_seconds: int,
) -> None:
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    if process.poll() is None:
        try:
            process.wait(timeout=grace_seconds)
        except subprocess.TimeoutExpired:
            pass
    # The direct Git process may have exited after SIGTERM while an index-pack
    # or pack-objects descendant remained alive. Kill the group even when the
    # leader has already been reaped.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=grace_seconds)
    except subprocess.TimeoutExpired:
        # The process group has received SIGKILL. Do not wait forever for an
        # unrelated init/subreaper bug during a forced SSH command.
        pass


def _run_git_service(
    command: list[str],
    environment: dict[str, str],
    *,
    deadline_seconds: int,
    termination_grace_seconds: int,
) -> int:
    try:
        process = subprocess.Popen(
            command,
            env=environment,
            start_new_session=True,
            close_fds=True,
        )
    except OSError as error:
        raise GatewayError("Git service could not start") from error
    try:
        return int(process.wait(timeout=deadline_seconds))
    except subprocess.TimeoutExpired as error:
        _terminate_process_group(process, termination_grace_seconds)
        raise GatewayError("Git service deadline exceeded") from error
    except BaseException:
        _terminate_process_group(process, termination_grace_seconds)
        raise


def _serve(config: Config, key_id: str) -> int:
    if not KEY_ID_RE.fullmatch(str(key_id or "")):
        raise GatewayError("public key is not authorized")
    operation, requested_owner, requested_repo = _parse_original_command(
        os.environ.get("SSH_ORIGINAL_COMMAND", "")
    )
    requested_entry = config.repositories.get((requested_owner, requested_repo.lower()))
    if not requested_entry:
        raise GatewayError("repository is not available on this gateway")
    result = _api(
        config,
        {
            "action": "authorize",
            "keyId": key_id,
            "owner": requested_owner,
            "repository": requested_repo,
            "operation": operation,
        },
    )
    owner = str(result.get("owner") or "").strip().lower()
    repository = str(result.get("repository") or "").strip()
    relative_path = str(result.get("repositoryRelativePath") or "")
    if (
        not OWNER_RE.fullmatch(owner)
        or not REPO_RE.fullmatch(repository)
        or result.get("keyId") != key_id
        or repository.lower() != requested_repo.lower()
        or result.get("operation") != operation
        or relative_path != owner + "/" + repository + ".git"
    ):
        raise GatewayError("authorization service response is invalid")
    entry = config.repositories.get((owner, repository.lower()))
    if not entry:
        raise GatewayError("repository is not available on this gateway")
    repository_path, writable, repository_quota = _repository_entry(config, entry)
    requested_path, requested_writable, requested_quota = _repository_entry(
        config,
        requested_entry,
    )
    if requested_path != repository_path:
        raise GatewayError("repository is not available on this gateway")
    if requested_quota != repository_quota:
        raise GatewayError("repository is not available on this gateway")
    if operation == "git-receive-pack" and (not writable or not requested_writable):
        raise GatewayError("repository is read-only on this gateway")
    try:
        live_path = repository_path.resolve(strict=True)
        repository_root = config.repository_root.resolve(strict=True)
    except OSError as error:
        raise GatewayError("repository is unavailable") from error
    if (
        live_path != repository_path
        or repository_root not in live_path.parents
        or repository_path.is_symlink()
    ):
        raise GatewayError("repository is unavailable")
    limits = _limits(config)
    receive = operation == "git-receive-pack"
    with _acquire_slot(
        limits.capacity_directory,
        "process",
        maximum=limits.max_concurrent_processes,
        reserved_receive=limits.reserved_receive_processes,
        receive=receive,
    ):
        if not _bare_repository(repository_path):
            raise GatewayError("repository is unavailable")

    environment = {
        "PATH": "/usr/bin:/bin",
        "LANG": "C",
        "LC_ALL": "C",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_TERMINAL_PROMPT": "0",
        "GIT_PROTOCOL_FROM_USER": "0",
    }
    protocol = str(os.environ.get("GIT_PROTOCOL", "") or "")
    if protocol in ("version=1", "version=2"):
        environment["GIT_PROTOCOL"] = protocol
    git_command = [
        "git",
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
        # Never advertise, fetch, create, update, or delete ForkMesh's
        # owner-only merge recovery refs through an end-user SSH session.
        "-c",
        "uploadpack.hideRefs=" + INTERNAL_GIT_REF_NAMESPACE,
        "-c",
        "uploadpack.allowTipSHA1InWant=false",
        "-c",
        "uploadpack.allowReachableSHA1InWant=false",
        "-c",
        "uploadpack.allowAnySHA1InWant=false",
        "-c",
        "receive.hideRefs=" + INTERNAL_GIT_REF_NAMESPACE,
        "-c",
        "receive.fsckObjects=true",
        "-c",
        "pack.threads=" + str(limits.max_pack_threads),
        "-c",
        "safe.directory=" + str(repository_path),
    ]
    with ExitStack() as leases:
        leases.enter_context(
            _acquire_slot(
                limits.capacity_directory,
                "session",
                maximum=limits.max_concurrent_sessions,
                reserved_receive=limits.reserved_receive_sessions,
                receive=receive,
            )
        )
        receive_artifacts: dict[str, tuple[int, int]] = {}
        if receive:
            leases.enter_context(
                _acquire_repository_write_slot(
                    limits.capacity_directory,
                    repository_path,
                )
            )
            repository_bytes = _repository_size_bytes(
                repository_path,
                limits.storage_scan_deadline_seconds,
            )
            remaining_bytes = repository_quota - repository_bytes
            # Keep a fixed metadata reserve and a second input-sized reserve for
            # pack indexes/transaction files. Git independently rejects the
            # stream at this exact receive.maxInputSize boundary.
            receive_limit = min(
                limits.receive_max_input_bytes,
                max(0, (remaining_bytes - 1024 * 1024) // 2),
            )
            if receive_limit <= 0:
                raise GatewayError("repository storage quota exceeded")
            git_command.extend(
                [
                    "-c",
                    "receive.maxInputSize=" + str(receive_limit),
                    "-c",
                    # Keep accepted objects packed so a small compressed input
                    # cannot unexpectedly expand into many large loose files.
                    "receive.unpackLimit=0",
                ]
            )
            receive_artifacts = _receive_artifacts(
                repository_path,
                limits.cleanup_deadline_seconds,
            )
        git_command.extend(
            [
                operation.removeprefix("git-"),
                str(repository_path),
            ]
        )
        leases.enter_context(
            _acquire_slot(
                limits.capacity_directory,
                "process",
                maximum=limits.max_concurrent_processes,
                reserved_receive=limits.reserved_receive_processes,
                receive=receive,
            )
        )
        try:
            returncode = _run_git_service(
                git_command,
                environment,
                deadline_seconds=(
                    limits.receive_pack_deadline_seconds
                    if receive
                    else limits.upload_pack_deadline_seconds
                ),
                termination_grace_seconds=limits.termination_grace_seconds,
            )
        except BaseException:
            if receive:
                _cleanup_interrupted_receive(
                    repository_path,
                    receive_artifacts,
                    limits.cleanup_deadline_seconds,
                )
            raise
        if receive and returncode != 0:
            _cleanup_interrupted_receive(
                repository_path,
                receive_artifacts,
                limits.cleanup_deadline_seconds,
            )
        if receive and returncode == 0:
            final_bytes = _repository_size_bytes(
                repository_path,
                limits.storage_scan_deadline_seconds,
            )
            if final_bytes > repository_quota:
                # This should be unreachable with the input and metadata
                # reserve, but never publish an over-quota generation.
                raise GatewayError("repository storage quota exceeded")
    if returncode == 0 and receive:
        try:
            subprocess.run(
                [str(config.refresh_notifier)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env={"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"},
                timeout=5,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            # A committed push must not be rewritten as failed because the
            # coalescing publication notification was unavailable. The fixed
            # wrapper persists a marker on Python-level failure, and the
            # independent reconciliation timer repairs a missing invocation.
            pass
    return int(returncode)


def _check(config: Config) -> int:
    unavailable = 0
    writable = 0
    over_quota = 0
    limits = _limits(config)
    try:
        _capacity_directory(limits.capacity_directory)
        capacity_available = True
    except GatewayError:
        capacity_available = False
    for item in config.repositories.values():
        repository_path, is_writable, quota = _repository_entry(config, item)
        if not _bare_repository(repository_path):
            unavailable += 1
        else:
            try:
                if (
                    _repository_size_bytes(
                        repository_path,
                        limits.storage_scan_deadline_seconds,
                    )
                    > quota
                ):
                    over_quota += 1
            except GatewayError:
                unavailable += 1
        if is_writable:
            writable += 1
    print(
        json.dumps(
            {
                "ok": unavailable == 0 and over_quota == 0 and capacity_available,
                "capacityAvailable": capacity_available,
                "overQuotaRepositoryCount": over_quota,
                "repositoryCount": len(config.repositories),
                "readWriteRepositoryCount": writable,
                "unavailableRepositoryCount": unavailable,
                "privateKeysLoaded": False,
            },
            sort_keys=True,
        )
    )
    return 0 if unavailable == 0 and over_quota == 0 and capacity_available else 1


def _worker_allowlist(config: Config) -> int:
    """Print the exact fail-closed Worker repository configuration value."""

    entries = []
    for (owner, repository), item in sorted(config.repositories.items()):
        _path, writable, _quota = _repository_entry(config, item)
        entries.append(
            owner + "/" + repository + "=" + ("read-write" if writable else "read-only")
        )
    print(",".join(entries))
    return 0


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="ForkMesh node-side Git SSH forced-command gateway"
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(
            os.environ.get(
                "FORKMESH_SSH_GATEWAY_CONFIG",
                "/etc/forkmesh/ssh-gateway.json",
            )
        ),
    )
    commands = parser.add_subparsers(dest="command", required=True)
    authorized = commands.add_parser("authorized-key")
    authorized.add_argument("--key-type", required=True)
    authorized.add_argument("--key-blob", required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--key-id", required=True)
    commands.add_parser("check")
    commands.add_parser("worker-allowlist")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        config = Config(args.config)
        if args.command == "authorized-key":
            return _authorized_key(config, args.key_type, args.key_blob)
        if args.command == "serve":
            return _serve(config, args.key_id)
        if args.command == "worker-allowlist":
            return _worker_allowlist(config)
        return _check(config)
    except GatewayError as error:
        if getattr(args, "command", "") == "serve":
            print("ForkMesh SSH: repository access denied", file=sys.stderr)
        else:
            print("ForkMesh SSH: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
