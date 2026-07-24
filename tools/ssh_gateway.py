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
import re
import shlex
import stat
import subprocess
import sys
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


CONFIG_VERSION = 1
MAX_CONFIG_BYTES = 1024 * 1024
MAX_API_RESPONSE_BYTES = 64 * 1024
API_TIMEOUT_SECONDS = 8
OWNER_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
KEY_ID_RE = re.compile(r"^sk_[A-Za-z0-9_-]{16,80}$")
KEY_TYPE_RE = re.compile(r"^[A-Za-z0-9@._+-]{1,80}$")
EXECUTABLE_RE = re.compile(r"^/[A-Za-z0-9_./+-]{1,511}$")
GATEWAY_TOKEN_RE = re.compile(r"^[A-Za-z0-9_-]{32,512}$")
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
ORIGINAL_COMMAND_RE = re.compile(
    r"^git-(upload|receive)-pack\s+(.+)$"
)


class GatewayError(RuntimeError):
    """A safe, generic gateway failure."""


def _read_json(path: Path) -> dict[str, Any]:
    descriptor = -1
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        info = os.fstat(descriptor)
        if (
            not stat.S_ISREG(info.st_mode)
            or info.st_uid not in (0, os.geteuid())
            or info.st_mode & 0o022
            or info.st_size > MAX_CONFIG_BYTES
        ):
            raise GatewayError("invalid gateway configuration")
        raw = os.read(descriptor, MAX_CONFIG_BYTES + 1)
        value = json.loads(raw)
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


def _https_origin(value: object) -> str:
    try:
        parsed = urlsplit(str(value or "").strip())
        parsed_port = parsed.port
    except ValueError as error:
        raise GatewayError("invalid Worker API origin") from error
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.path not in ("", "/")
    ):
        raise GatewayError("invalid Worker API origin")
    port = "" if parsed_port in (None, 443) else ":" + str(parsed_port)
    return "https://" + parsed.hostname.lower() + port


def _gateway_command(value: object) -> str:
    command = str(value or "").strip()
    if (
        not EXECUTABLE_RE.fullmatch(command)
        or any(part in ("", ".", "..") for part in command.split("/")[1:])
    ):
        raise GatewayError("invalid gateway executable")
    return command


def _load_token(source: dict[str, Any]) -> str:
    token = str(os.environ.get("FORKMESH_SSH_GATEWAY_TOKEN", "") or "").strip()
    if not token:
        token_path = Path(str(source.get("gatewayTokenFile") or "").strip())
        try:
            info = token_path.lstat()
            if (
                not stat.S_ISREG(info.st_mode)
                or stat.S_ISLNK(info.st_mode)
                or info.st_mode & 0o077
                or info.st_size > 4096
            ):
                raise GatewayError("gateway token is unavailable")
            token = token_path.read_text(encoding="utf-8").strip()
        except (OSError, ValueError) as error:
            raise GatewayError("gateway token is unavailable") from error
    if not GATEWAY_TOKEN_RE.fullmatch(token):
        raise GatewayError("gateway token is unavailable")
    return token


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
        if source.get("schemaVersion") != CONFIG_VERSION:
            raise GatewayError("unsupported gateway configuration")
        self.api_origin = _https_origin(source.get("apiOrigin"))
        self.gateway_command = _gateway_command(source.get("gatewayExecutable"))
        root = Path(str(source.get("repositoryRoot") or "").strip())
        if not root.is_absolute():
            raise GatewayError("invalid repository root")
        self.repository_root = root.resolve()
        self.token = _load_token(source)
        raw_repositories = source.get("repositories")
        if not isinstance(raw_repositories, list):
            raise GatewayError("invalid repository allowlist")
        self.repositories: dict[tuple[str, str], tuple[Path, bool]] = {}
        for item in raw_repositories:
            if not isinstance(item, dict):
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
            target = (self.repository_root / relative).resolve()
            if self.repository_root not in target.parents:
                raise GatewayError("invalid repository allowlist")
            key = (owner, name.lower())
            if key in self.repositories:
                raise GatewayError("duplicate repository allowlist entry")
            self.repositories[key] = (target, access == "read-write")


def _api(config: Config, payload: dict[str, Any]) -> dict[str, Any]:
    body = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    request = Request(
        config.api_origin + "/api/ssh/authorize",
        data=body,
        method="POST",
        headers={
            "accept": "application/json",
            "authorization": "Bearer " + config.token,
            "content-type": "application/json",
        },
    )
    try:
        with urlopen(request, timeout=API_TIMEOUT_SECONDS) as response:
            if int(response.status) != 200:
                raise GatewayError("authorization service denied access")
            raw = response.read(MAX_API_RESPONSE_BYTES + 1)
    except (HTTPError, URLError, OSError, TimeoutError) as error:
        raise GatewayError("authorization service unavailable") from error
    if len(raw) > MAX_API_RESPONSE_BYTES:
        raise GatewayError("authorization service response is invalid")
    try:
        value = json.loads(raw)
    except (TypeError, ValueError) as error:
        raise GatewayError("authorization service response is invalid") from error
    if not isinstance(value, dict) or value.get("authorized") is not True:
        raise GatewayError("authorization service denied access")
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
            env={"PATH": "/usr/bin:/bin", "LANG": "C"},
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
    if not OWNER_RE.fullmatch(owner) or not REPO_RE.fullmatch(repository):
        raise GatewayError("authorization service response is invalid")
    entry = config.repositories.get((owner, repository.lower()))
    if not entry:
        raise GatewayError("repository is not available on this gateway")
    repository_path, writable = entry
    if operation == "git-receive-pack" and not writable:
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

    environment = {"PATH": "/usr/bin:/bin", "LANG": "C"}
    protocol = str(os.environ.get("GIT_PROTOCOL", "") or "")
    if protocol in ("version=1", "version=2"):
        environment["GIT_PROTOCOL"] = protocol
    os.execvpe(
        "git",
        ["git", operation.removeprefix("git-"), str(repository_path)],
        environment,
    )
    raise GatewayError("Git service could not start")


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
    for (owner, repository), (_path, writable) in sorted(
        config.repositories.items()
    ):
        entries.append(
            owner
            + "/"
            + repository
            + "="
            + ("read-write" if writable else "read-only")
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
        print("ForkMesh SSH: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
