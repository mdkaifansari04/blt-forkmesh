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


def _run(sums_line, payload, *, omit_curl=False):
    """Run install_prebuilt_release with a fake git+curl; return (rc, BIN bytes)."""
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        fake_repo = tmp / "fake_repo" / "releases" / "latest"
        fake_repo.mkdir(parents=True)
        (fake_repo / "SHASUMS256.txt").write_text(sums_line, encoding="utf-8")
        payload_file = tmp / "payload.bin"
        payload_file.write_bytes(payload)

        bindir = tmp / "stubbin"
        bindir.mkdir()
        # Fake git: clone makes the dir; sparse-checkout records the rel path;
        # checkout copies that file out of $FAKE_REPO if it exists.
        (bindir / "git").write_text(
            """#!/bin/sh
if [ "$1" = "clone" ]; then
  eval "dest=\\${$#}"; mkdir -p "$dest"; exit 0
fi
if [ "$1" = "-C" ]; then
  dir="$2"; shift 2
  if [ "$1" = "sparse-checkout" ]; then printf '%s' "$4" > "$dir/.rel"; exit 0; fi
  if [ "$1" = "checkout" ]; then
    rel="$(cat "$dir/.rel" 2>/dev/null)"
    if [ -n "$rel" ] && [ -f "$FAKE_REPO/$rel" ]; then
      mkdir -p "$dir/$(dirname "$rel")"; cp "$FAKE_REPO/$rel" "$dir/$rel"
    fi
    exit 0
  fi
fi
exit 0
""",
            encoding="utf-8",
        )
        (bindir / "git").chmod(0o755)
        if not omit_curl:
            # Fake curl: `curl -fsSL <url> -o <out>` → write the payload.
            (bindir / "curl").write_text(
                """#!/bin/sh
out=""; while [ $# -gt 0 ]; do [ "$1" = "-o" ] && { out="$2"; shift; }; shift; done
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

        script = PREAMBLE + _release_functions() + "\ninstall_prebuilt_release\n"
        proc = subprocess.run(
            ["bash", "-c", script], env=env, text=True, capture_output=True)
        bin_path = out_dir / "bin" / "forkmesh"
        installed = bin_path.read_bytes() if bin_path.exists() else None
        return proc.returncode, installed, proc.stderr


def test_installs_verified_blob():
    payload = b"\x7fELF fake forkmesh binary bytes" * 50
    digest = hashlib.sha256(payload).hexdigest()
    rc, installed, _ = _run("%s  forkmesh-linux-x86_64\n" % digest, payload)
    assert rc == 0
    assert installed == payload


def test_rejects_checksum_mismatch():
    # Manifest advertises one hash, the relay serves different bytes → no install.
    payload = b"tampered bytes that do not match the manifest hash"
    wrong = hashlib.sha256(b"the expected bytes").hexdigest()
    rc, installed, stderr = _run("%s  forkmesh-linux-x86_64\n" % wrong, payload)
    assert rc != 0
    assert installed is None
    assert "mismatch" in stderr.lower()


def test_ignores_other_platform_entries():
    # Manifest has no line for this platform → prebuilt path declines (rc != 0).
    payload = b"irrelevant"
    other = hashlib.sha256(payload).hexdigest()
    rc, installed, _ = _run("%s  forkmesh-windows-x86_64.exe\n" % other, payload)
    assert rc != 0
    assert installed is None
