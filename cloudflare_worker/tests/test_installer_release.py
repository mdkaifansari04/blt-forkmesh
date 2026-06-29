#!/usr/bin/env python3
"""install.sh prebuilt fast path (issue #304).

Drives install_prebuilt_release() with mocked `git` (serves the committed
SHASUMS256.txt manifest over a fake clone) and `curl` (serves the
content-addressed blob), asserting the installer:

  * resolves the platform asset's sha256 from the manifest,
  * downloads it from the relay's release endpoint,
  * VERIFIES the sha256 before installing,
  * refuses to install a blob whose bytes don't match the manifest.
"""

import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


INSTALLER = Path(__file__).resolve().parents[1] / "public" / "install.sh"


def _release_functions():
    """Extract the contiguous prebuilt-install helper block from install.sh."""
    script = INSTALLER.read_text(encoding="utf-8")
    start = script.index("_sparse_fetch_file() {")
    # install_prebuilt_release is the last function in the block; cut at its
    # closing brace (first line that is exactly "}" after its definition).
    body_at = script.index("install_prebuilt_release() {", start)
    end = script.index("\n}\n", body_at) + len("\n}\n")
    return script[start:end]


PREAMBLE = """
set -u
say()  { :; }
warn() { printf 'WARN:%s\\n' "$1" >&2; }
RELEASE_CHANNEL="latest"
ASSET_NAME="forkmesh-linux-x86_64"
ASSET_OS="linux"
ASSET_ARCH="x86_64"
ASSET_REL_PATH="releases/${RELEASE_CHANNEL}/${ASSET_NAME}"
FORKMESH_HOST="https://relay.test"
BIN_DIR="$OUT_DIR/bin"
BIN="$BIN_DIR/forkmesh"
REPO_CANDIDATES=("https://relay.test/alice/forkmesh")
"""


def _run(sums_line, payload, *, omit_curl=False, manifest=None):
    """Run install_prebuilt_release with a fake git+curl.

    Returns (rc, BIN bytes, stderr, blob_url). When `manifest` is given it is
    committed as releases/latest/release.json in the fake repo, so the test can
    assert the blob URL is built from the manifest's canonical repo.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        fake_repo = tmp / "fake_repo" / "releases" / "latest"
        fake_repo.mkdir(parents=True)
        (fake_repo / "SHASUMS256.txt").write_text(sums_line, encoding="utf-8")
        if manifest is not None:
            (fake_repo / "release.json").write_text(manifest, encoding="utf-8")
        payload_file = tmp / "payload.bin"
        payload_file.write_bytes(payload)
        url_file = tmp / "curl_url"

        bindir = tmp / "stubbin"
        bindir.mkdir()
        # Fake git: clone makes the dir; sparse-checkout records the rel path(s)
        # (one per line); checkout copies each that exists out of $FAKE_REPO.
        (bindir / "git").write_text(
            """#!/bin/sh
if [ "$1" = "clone" ]; then
  eval "dest=\\${$#}"; mkdir -p "$dest"; exit 0
fi
if [ "$1" = "-C" ]; then
  dir="$2"; shift 2
  if [ "$1" = "sparse-checkout" ]; then
    shift 3; printf '%s\\n' "$@" > "$dir/.rel"; exit 0
  fi
  if [ "$1" = "checkout" ]; then
    while IFS= read -r rel; do
      [ -n "$rel" ] || continue
      if [ -f "$FAKE_REPO/$rel" ]; then
        mkdir -p "$dir/$(dirname "$rel")"; cp "$FAKE_REPO/$rel" "$dir/$rel"
      fi
    done < "$dir/.rel"
    exit 0
  fi
fi
exit 0
""",
            encoding="utf-8",
        )
        (bindir / "git").chmod(0o755)
        if not omit_curl:
            # Fake curl: `curl -fsSL <url> -o <out>` → record the URL and write
            # the payload to <out>.
            (bindir / "curl").write_text(
                """#!/bin/sh
out=""; url=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) out="$2"; shift ;;
    -*) ;;
    *) url="$1" ;;
  esac
  shift
done
[ -n "$url" ] && printf '%s\\n' "$url" > "$FORKMESH_TEST_URL_FILE"
[ -n "$out" ] && cp "$FORKMESH_TEST_PAYLOAD" "$out"
exit 0
""",
                encoding="utf-8",
            )
            (bindir / "curl").chmod(0o755)

        out_dir = tmp / "out"
        out_dir.mkdir()
        env = os.environ.copy()
        env["PATH"] = str(bindir) + os.pathsep + env["PATH"]
        env["OUT_DIR"] = str(out_dir)
        env["FAKE_REPO"] = str(tmp / "fake_repo")
        env["FORKMESH_TEST_PAYLOAD"] = str(payload_file)
        env["FORKMESH_TEST_URL_FILE"] = str(url_file)

        script = PREAMBLE + _release_functions() + "\ninstall_prebuilt_release\n"
        proc = subprocess.run(
            ["bash", "-c", script], env=env, text=True, capture_output=True)
        bin_path = out_dir / "bin" / "forkmesh"
        installed = bin_path.read_bytes() if bin_path.exists() else None
        blob_url = url_file.read_text().strip() if url_file.exists() else None
        return proc.returncode, installed, proc.stderr, blob_url


def test_installs_verified_blob():
    payload = b"\x7fELF fake forkmesh binary bytes" * 50
    digest = hashlib.sha256(payload).hexdigest()
    rc, installed, _, _ = _run("%s  forkmesh-linux-x86_64\n" % digest, payload)
    assert rc == 0
    assert installed == payload


def test_rejects_checksum_mismatch():
    # Manifest advertises one hash, the relay serves different bytes → no install.
    payload = b"tampered bytes that do not match the manifest hash"
    wrong = hashlib.sha256(b"the expected bytes").hexdigest()
    rc, installed, stderr, _ = _run("%s  forkmesh-linux-x86_64\n" % wrong, payload)
    assert rc != 0
    assert installed is None
    assert "mismatch" in stderr.lower()


def test_ignores_other_platform_entries():
    # Manifest has no line for this platform → prebuilt path declines (rc != 0).
    payload = b"irrelevant"
    other = hashlib.sha256(payload).hexdigest()
    rc, installed, _, _ = _run("%s  forkmesh-windows-x86_64.exe\n" % other, payload)
    assert rc != 0
    assert installed is None


def test_blob_url_uses_manifest_repo_not_mirror():
    # The clone came from the mirror "alice/forkmesh" (REPO_CANDIDATES), but the
    # release.json manifest records the canonical "newnewnode/forkmesh" — the only
    # node that holds the out-of-git blob. The blob URL MUST target the canonical
    # repo, otherwise the mirror 404s and a published binary falls back to source.
    payload = b"\x7fELF prebuilt" * 20
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"newnewnode/forkmesh",'
        '"channel":"latest","assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % digest
    )
    rc, installed, _, blob_url = _run(
        "%s  forkmesh-linux-x86_64\n" % digest, payload, manifest=manifest)
    assert rc == 0
    assert installed == payload
    assert blob_url == (
        "https://relay.test/api/repo/newnewnode/forkmesh"
        "/releases/blob/sha256/%s" % digest)


def test_blob_url_falls_back_to_mirror_without_manifest():
    # Legacy release with no release.json → fall back to the mirror's own
    # owner/repo so older publishes keep working.
    payload = b"\x7fELF legacy" * 20
    digest = hashlib.sha256(payload).hexdigest()
    rc, installed, _, blob_url = _run(
        "%s  forkmesh-linux-x86_64\n" % digest, payload)
    assert rc == 0
    assert installed == payload
    assert blob_url == (
        "https://relay.test/api/repo/alice/forkmesh"
        "/releases/blob/sha256/%s" % digest)
