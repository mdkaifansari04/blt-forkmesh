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
import json
import os
from pathlib import Path
import pwd
import re
import shlex
import socket
import stat
import struct
import subprocess
import sys
from typing import Any


CONFIG_VERSION = 1
MAX_CONFIG_BYTES = 1024 * 1024
MAX_BROKER_FRAME_BYTES = 64 * 1024
BROKER_TIMEOUT_SECONDS = 12
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
        if set(source) != {
            "schemaVersion",
            "authorizationSocket",
            "authorizationBrokerUser",
            "gatewayExecutable",
            "refreshNotifier",
            "repositoryRoot",
            "repositories",
        }:
            raise GatewayError("invalid gateway configuration")
        if source.get("schemaVersion") != CONFIG_VERSION:
            raise GatewayError("unsupported gateway configuration")
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
        self.repositories: dict[tuple[str, str], tuple[Path, bool]] = {}
        for item in raw_repositories:
            if not isinstance(item, dict) or set(item) != {
                "owner",
                "name",
                "path",
                "access",
            }:
                raise GatewayError("invalid repository allowlist")
            owner = str(item.get("owner") or "").strip().lower()
            name = str(item.get("name") or "").strip()
            access = str(item.get("access") or "read-only").strip().lower()
            if (
                not OWNER_RE.fullmatch(owner)
                or not REPO_RE.fullmatch(name)
                or access not in ("read-only", "read-write")
            ):
                raise GatewayError("invalid repository allowlist")
            relative = _safe_relative_path(item.get("path"))
            target = self.repository_root / relative
            key = (owner, name.lower())
            if key in self.repositories:
                raise GatewayError("duplicate repository allowlist entry")
            self.repositories[key] = (target, access == "read-write")


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
    repository_path, writable = entry
    requested_path, requested_writable = requested_entry
    if requested_path != repository_path:
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
        "safe.directory=" + str(repository_path),
        operation.removeprefix("git-"),
        str(repository_path),
    ]
    try:
        completed = subprocess.run(
            git_command,
            env=environment,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise GatewayError("Git service could not start") from error
    if completed.returncode == 0 and operation == "git-receive-pack":
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
            # coalescing publication notification was unavailable.
            pass
    return int(completed.returncode)


def _check(config: Config) -> int:
    unavailable = 0
    writable = 0
    for repository_path, is_writable in config.repositories.values():
        if not _bare_repository(repository_path):
            unavailable += 1
        if is_writable:
            writable += 1
    print(
        json.dumps(
            {
                "ok": unavailable == 0,
                "repositoryCount": len(config.repositories),
                "readWriteRepositoryCount": writable,
                "unavailableRepositoryCount": unavailable,
                "privateKeysLoaded": False,
            },
            sort_keys=True,
        )
    )
    return 0 if unavailable == 0 else 1


def _worker_allowlist(config: Config) -> int:
    """Print the exact fail-closed Worker repository configuration value."""

    entries = []
    for (owner, repository), (_path, writable) in sorted(config.repositories.items()):
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
