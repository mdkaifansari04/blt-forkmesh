#!/usr/bin/env python3
"""Install ForkMesh's pinned cloudflared connector without a shell pipeline.

The desktop invokes this helper only when neither a system cloudflared nor a
previously verified managed copy is available.  Every supported download is an
exact Cloudflare GitHub release asset with a repository-pinned SHA-256 digest.
The verified payload is written atomically to an owner-controlled destination.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import stat
import sys
import tarfile
import tempfile
from typing import BinaryIO, Callable
from urllib.parse import urlparse
from urllib.request import Request, urlopen


VERSION = "2026.7.2"
RELEASE_BASE = (
    "https://github.com/cloudflare/cloudflared/releases/download/" + VERSION
)
MAX_DOWNLOAD_BYTES = 100 * 1024 * 1024
ALLOWED_DOWNLOAD_HOSTS = frozenset(
    {
        "github.com",
        "objects.githubusercontent.com",
        "release-assets.githubusercontent.com",
    }
)


class InstallError(RuntimeError):
    """A safe-to-display installation failure."""


@dataclass(frozen=True)
class Artifact:
    name: str
    archive_sha256: str
    binary_sha256: str
    archived: bool = False

    @property
    def url(self) -> str:
        return f"{RELEASE_BASE}/{self.name}"





ARTIFACTS: dict[tuple[str, str], Artifact] = {
    ("linux", "x86_64"): Artifact(
        "cloudflared-linux-amd64",
        "ec905ea7b7e327ff8abdde8cb64697a2152de74dbcdbf6aec9db8364eb3886cd",
        "ec905ea7b7e327ff8abdde8cb64697a2152de74dbcdbf6aec9db8364eb3886cd",
    ),
    ("linux", "arm64"): Artifact(
        "cloudflared-linux-arm64",
        "405df476437e027fc6d18729a5a77155c0a33a6082aeee60a799a688f3052e66",
        "405df476437e027fc6d18729a5a77155c0a33a6082aeee60a799a688f3052e66",
    ),
    ("darwin", "x86_64"): Artifact(
        "cloudflared-darwin-amd64.tgz",
        "4ee0d3b48a990a2f9b5faec5838f73ec1f400aa8e0a4864be576adfafec406cb",
        "a5afb0ba3da859da47bebc9a918d5b196bf7e4aec23589419b46356731bcc75f",
        archived=True,
    ),
    ("darwin", "arm64"): Artifact(
        "cloudflared-darwin-arm64.tgz",
        "2086e51c61d6565781d84117a5007d0c826d03ffdc74acb91c08c167f9f8cd7c",
        "0588df58494a6cadd38b9deb6078908a5054063c80784d92fdb8d4a5f3de1c67",
        archived=True,
    ),
    ("windows", "x86_64"): Artifact(
        "cloudflared-windows-amd64.exe",
        "cdb5d4432f6ae1595654a692a51308b69d2bf7af961f5578d9391837cf072df9",
        "cdb5d4432f6ae1595654a692a51308b69d2bf7af961f5578d9391837cf072df9",
    ),
}


def _normalized_platform(
    system: str | None = None, machine: str | None = None
) -> tuple[str, str]:
    raw_system = (system or platform.system()).strip().lower()
    raw_machine = (machine or platform.machine()).strip().lower()
    systems = {
        "linux": "linux",
        "darwin": "darwin",
        "windows": "windows",
    }
    machines = {
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "arm64": "arm64",
        "aarch64": "arm64",
    }
    key = (systems.get(raw_system, raw_system), machines.get(raw_machine, raw_machine))
    if key not in ARTIFACTS:
        raise InstallError(
            f"cloudflared {VERSION} is not pinned for {raw_system}/{raw_machine}"
        )
    return key


def selected_artifact(
    system: str | None = None, machine: str | None = None
) -> Artifact:
    return ARTIFACTS[_normalized_platform(system, machine)]


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _checked_response(
    response: BinaryIO, *, expected_url: str
) -> None:
    final_url = getattr(response, "geturl", lambda: expected_url)()
    parsed = urlparse(str(final_url))
    if parsed.scheme != "https" or parsed.hostname not in ALLOWED_DOWNLOAD_HOSTS:
        raise InstallError("cloudflared download redirected to an untrusted origin")
    length = getattr(response, "headers", {}).get("Content-Length")
    if length:
        try:
            value = int(length)
        except ValueError as exc:
            raise InstallError("cloudflared download has an invalid size") from exc
        if value <= 0 or value > MAX_DOWNLOAD_BYTES:
            raise InstallError("cloudflared download is outside the safe size limit")


def _download(
    artifact: Artifact,
    destination: Path,
    *,
    opener: Callable[..., BinaryIO] = urlopen,
) -> None:
    request = Request(
        artifact.url,
        headers={
            "Accept": "application/octet-stream",
            "User-Agent": f"ForkMesh-cloudflared-installer/{VERSION}",
        },
    )
    try:
        response = opener(request, timeout=60)
        with response:
            _checked_response(response, expected_url=artifact.url)
            total = 0
            digest = hashlib.sha256()
            with destination.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    total += len(chunk)
                    if total > MAX_DOWNLOAD_BYTES:
                        raise InstallError(
                            "cloudflared download exceeded the safe size limit"
                        )
                    digest.update(chunk)
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
    except InstallError:
        raise
    except OSError as exc:
        raise InstallError("cloudflared download failed") from exc
    if total <= 0 or digest.hexdigest() != artifact.archive_sha256:
        raise InstallError("cloudflared release checksum verification failed")


def _extract_binary(artifact: Artifact, archive_path: Path, output_path: Path) -> None:
    if not artifact.archived:
        with archive_path.open("rb") as source, output_path.open("wb") as output:
            while chunk := source.read(1024 * 1024):
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        return
    try:
        with tarfile.open(archive_path, mode="r:gz") as archive:
            members = archive.getmembers()
            if (
                len(members) != 1
                or members[0].name != "cloudflared"
                or not members[0].isfile()
                or members[0].size <= 0
                or members[0].size > MAX_DOWNLOAD_BYTES
            ):
                raise InstallError("cloudflared archive has an unexpected layout")
            source = archive.extractfile(members[0])
            if source is None:
                raise InstallError("cloudflared archive member is unreadable")
            with source, output_path.open("wb") as output:
                while chunk := source.read(1024 * 1024):
                    output.write(chunk)
                output.flush()
                os.fsync(output.fileno())
    except (tarfile.TarError, OSError) as exc:
        raise InstallError("cloudflared archive extraction failed") from exc


def verify_binary(
    path: Path,
    *,
    system: str | None = None,
    machine: str | None = None,
) -> Artifact:
    artifact = selected_artifact(system, machine)
    if (
        not path.is_file()
        or path.is_symlink()
        or _sha256_file(path) != artifact.binary_sha256
    ):
        raise InstallError(
            f"managed cloudflared is not the verified ForkMesh {VERSION} build"
        )
    return artifact


def install(
    destination: Path,
    *,
    system: str | None = None,
    machine: str | None = None,
    opener: Callable[..., BinaryIO] = urlopen,
) -> Artifact:
    artifact = selected_artifact(system, machine)
    destination = destination.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    if destination.exists():
        try:
            verify_binary(destination, system=system, machine=machine)
            return artifact
        except InstallError:
            pass

    with tempfile.TemporaryDirectory(
        prefix=".cloudflared-", dir=destination.parent
    ) as temp:
        temp_root = Path(temp)
        archive = temp_root / artifact.name
        candidate = temp_root / ("cloudflared.exe" if os.name == "nt" else "cloudflared")
        _download(artifact, archive, opener=opener)
        _extract_binary(artifact, archive, candidate)
        if _sha256_file(candidate) != artifact.binary_sha256:
            raise InstallError("cloudflared executable checksum verification failed")
        candidate.chmod(
            stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR
        )
        os.replace(candidate, destination)
    verify_binary(destination, system=system, machine=machine)
    return artifact


def _json_result(destination: Path, artifact: Artifact) -> str:
    result = {
        "ok": True,
        "version": VERSION,
        "path": str(destination.expanduser().resolve()),
        "asset": artifact.name,
        "binarySha256": artifact.binary_sha256,
        "verified": True,
    }
    return json.dumps(result, sort_keys=True, separators=(",", ":"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Install or verify ForkMesh's exact SHA-256-pinned cloudflared "
            "connector release."
        )
    )
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--json-stdout", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        artifact = (
            verify_binary(args.destination)
            if args.verify_only
            else install(args.destination)
        )
        if args.json_stdout:
            print(_json_result(args.destination, artifact))
        else:
            print(
                f"Verified cloudflared {VERSION} at "
                f"{args.destination.expanduser().resolve()}"
            )
        return 0
    except (InstallError, OSError, ValueError) as exc:
        safe = re.sub(r"(?i)(token|authorization)\\s*[:=]\\s*\\S+", r"\\1=<redacted>", str(exc))
        print(f"cloudflared install failed: {safe}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
