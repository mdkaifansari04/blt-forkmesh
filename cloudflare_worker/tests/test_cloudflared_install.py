from __future__ import annotations

from contextlib import AbstractContextManager
import hashlib
import io
from pathlib import Path
import tarfile

import pytest

import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import cloudflared_install as installer


class _Response(io.BytesIO, AbstractContextManager):
    def __init__(self, payload: bytes, url: str) -> None:
        super().__init__(payload)
        self._url = url
        self.headers = {"Content-Length": str(len(payload))}

    def geturl(self) -> str:
        return self._url

    def __exit__(self, *_args):
        self.close()
        return False


def _artifact(payload: bytes, *, archived: bool = False, binary: bytes | None = None):
    executable = binary if binary is not None else payload
    return installer.Artifact(
        "fixture.tgz" if archived else "fixture",
        hashlib.sha256(payload).hexdigest(),
        hashlib.sha256(executable).hexdigest(),
        archived=archived,
    )


def test_release_matrix_is_exactly_versioned_and_checksum_pinned():
    assert installer.VERSION == "2026.7.2"
    assert installer.selected_artifact("Linux", "amd64").name == (
        "cloudflared-linux-amd64"
    )
    assert installer.selected_artifact("Darwin", "aarch64").archived is True
    assert installer.selected_artifact("Windows", "x86_64").name.endswith(".exe")
    for artifact in installer.ARTIFACTS.values():
        assert artifact.url.startswith(
            "https://github.com/cloudflare/cloudflared/releases/download/"
            + installer.VERSION
            + "/"
        )
        assert len(artifact.archive_sha256) == 64
        assert len(artifact.binary_sha256) == 64
    with pytest.raises(installer.InstallError):
        installer.selected_artifact("Windows", "arm64")


def test_standalone_download_is_verified_and_installed_atomically(
    monkeypatch, tmp_path
):
    payload = b"verified cloudflared fixture"
    fixture = _artifact(payload)
    monkeypatch.setattr(
        installer, "ARTIFACTS", {("linux", "x86_64"): fixture}
    )
    opened = []

    def open_fixture(request, *, timeout):
        opened.append((request.full_url, timeout))
        return _Response(payload, fixture.url)

    destination = tmp_path / "managed" / "cloudflared"
    result = installer.install(
        destination,
        system="linux",
        machine="x86_64",
        opener=open_fixture,
    )
    assert result == fixture
    assert destination.read_bytes() == payload
    assert destination.stat().st_mode & 0o777 == 0o700
    assert opened == [(fixture.url, 60)]
    assert list(destination.parent.glob(".cloudflared-*")) == []


def test_checksum_mismatch_never_replaces_existing_managed_binary(
    monkeypatch, tmp_path
):
    expected = b"expected"
    fixture = _artifact(expected)
    monkeypatch.setattr(
        installer, "ARTIFACTS", {("linux", "x86_64"): fixture}
    )
    destination = tmp_path / "cloudflared"
    destination.write_bytes(b"existing but stale")

    def open_tampered(_request, *, timeout):
        assert timeout == 60
        return _Response(b"tampered", fixture.url)

    with pytest.raises(installer.InstallError, match="checksum"):
        installer.install(
            destination,
            system="linux",
            machine="amd64",
            opener=open_tampered,
        )
    assert destination.read_bytes() == b"existing but stale"


def test_redirect_to_untrusted_origin_is_rejected(monkeypatch, tmp_path):
    payload = b"payload"
    fixture = _artifact(payload)
    monkeypatch.setattr(
        installer, "ARTIFACTS", {("linux", "x86_64"): fixture}
    )

    def open_untrusted(_request, *, timeout):
        assert timeout == 60
        return _Response(payload, "https://attacker.invalid/cloudflared")

    with pytest.raises(installer.InstallError, match="untrusted"):
        installer.install(
            tmp_path / "cloudflared",
            system="linux",
            machine="x86_64",
            opener=open_untrusted,
        )


def test_verified_single_member_darwin_archive_is_safely_extracted(
    monkeypatch, tmp_path
):
    executable = b"darwin cloudflared"
    archive_buffer = io.BytesIO()
    with tarfile.open(fileobj=archive_buffer, mode="w:gz") as archive:
        info = tarfile.TarInfo("cloudflared")
        info.size = len(executable)
        archive.addfile(info, io.BytesIO(executable))
    payload = archive_buffer.getvalue()
    fixture = _artifact(payload, archived=True, binary=executable)
    monkeypatch.setattr(
        installer, "ARTIFACTS", {("darwin", "arm64"): fixture}
    )

    def open_fixture(_request, *, timeout):
        assert timeout == 60
        return _Response(payload, fixture.url)

    destination = tmp_path / "cloudflared"
    installer.install(
        destination,
        system="darwin",
        machine="arm64",
        opener=open_fixture,
    )
    assert destination.read_bytes() == executable


def test_archive_rejects_path_traversal_or_extra_members(monkeypatch, tmp_path):
    archive_buffer = io.BytesIO()
    with tarfile.open(fileobj=archive_buffer, mode="w:gz") as archive:
        for name in ("cloudflared", "../outside"):
            info = tarfile.TarInfo(name)
            info.size = 1
            archive.addfile(info, io.BytesIO(b"x"))
    payload = archive_buffer.getvalue()
    fixture = _artifact(payload, archived=True, binary=b"x")
    monkeypatch.setattr(
        installer, "ARTIFACTS", {("darwin", "x86_64"): fixture}
    )

    def open_fixture(_request, *, timeout):
        assert timeout == 60
        return _Response(payload, fixture.url)

    with pytest.raises(installer.InstallError, match="unexpected layout"):
        installer.install(
            tmp_path / "cloudflared",
            system="darwin",
            machine="x86_64",
            opener=open_fixture,
        )
    assert not (tmp_path.parent / "outside").exists()

