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
import time


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
ASSET_REL_PATH=".forkmesh/releases/${RELEASE_CHANNEL}/${ASSET_NAME}"
FORKMESH_HOST="https://relay.test"
FORKMESH_EXPECTED_BUILD_COMMIT="${FORKMESH_EXPECTED_BUILD_COMMIT:-}"
FORKMESH_EXPECTED_RELEASE_VERSION="${FORKMESH_EXPECTED_RELEASE_VERSION:-}"
DEFER_PINNED_REINSTALL=0
ensure_qt_runtime() { :; }
uninstall_forkmesh() { :; }
BIN_DIR="$OUT_DIR/bin"
BIN="$BIN_DIR/forkmesh"
REPO_CANDIDATES=("https://relay.test/alice/forkmesh")
"""


def _run(
    sums_line,
    payload,
    *,
    omit_curl=False,
    manifest=None,
    expected_build_commit="",
    expected_version="",
    existing_binary=None,
    no_checksum_tool=False,
):
    """Run install_prebuilt_release with a fake git+curl.

    Returns (rc, BIN bytes, stderr, blob_url). When `manifest` is given it is
    committed as .forkmesh/releases/latest/release.json in the fake repo, so the test can
    assert the blob URL is built from the manifest's canonical repo.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        fake_repo = tmp / "fake_repo" / ".forkmesh" / "releases" / "latest"
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
        if no_checksum_tool:
            (bindir / "sha256sum").write_text(
                "#!/bin/sh\nexit 127\n", encoding="utf-8"
            )
            (bindir / "sha256sum").chmod(0o755)
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
        if existing_binary is not None:
            existing = out_dir / "bin" / "forkmesh"
            existing.parent.mkdir()
            existing.write_bytes(existing_binary)
            existing.chmod(0o755)
        env = os.environ.copy()
        env["PATH"] = str(bindir) + os.pathsep + env["PATH"]
        env["OUT_DIR"] = str(out_dir)
        env["FAKE_REPO"] = str(tmp / "fake_repo")
        env["FORKMESH_TEST_PAYLOAD"] = str(payload_file)
        env["FORKMESH_TEST_URL_FILE"] = str(url_file)
        env["FORKMESH_EXPECTED_BUILD_COMMIT"] = expected_build_commit
        env["FORKMESH_EXPECTED_RELEASE_VERSION"] = expected_version

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


def test_unpinned_install_keeps_legacy_no_checksum_tool_fallback():
    payload = b"\x7fELF ordinary unpinned install" * 20
    digest = hashlib.sha256(payload).hexdigest()
    rc, installed, stderr, _ = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        no_checksum_tool=True,
    )
    assert rc == 0
    assert installed == payload
    assert "installed forkmesh-linux-x86_64 unverified" in stderr.lower()


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
    # release.json manifest records the canonical "forkmesh/forkmesh" — the only
    # node that holds the out-of-git blob. The blob URL MUST target the canonical
    # repo, otherwise the mirror 404s and a published binary falls back to source.
    payload = b"\x7fELF prebuilt" * 20
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"channel":"latest","assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % digest
    )
    rc, installed, _, blob_url = _run(
        "%s  forkmesh-linux-x86_64\n" % digest, payload, manifest=manifest)
    assert rc == 0
    assert installed == payload
    assert blob_url == (
        "https://relay.test/api/repo/forkmesh/forkmesh"
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


def test_commit_pinned_install_accepts_exact_release_revision():
    commit = "a1" * 20
    payload = (
        "#!/bin/sh\n"
        'case "$1" in\n'
        '  --version) printf "ForkMesh 0.7.0\\n" ;;\n'
        '  --build-commit) printf "%s\\n" ;;\n'
        "  *) exit 2 ;;\n"
        "esac\n" % commit
    ).encode()
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"tag":"v0.7.0","tag_commit":"%s","build_commit":"%s",'
        '"channel":"latest",'
        '"assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % ("f0" * 20, commit, digest)
    )
    rc, installed, stderr, _ = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        manifest=manifest,
        expected_build_commit=commit,
        expected_version="0.7.0",
        existing_binary=b"old-known-good-binary",
    )
    assert rc == 0, stderr
    assert installed == payload


def test_pinned_reinstall_defers_destruction_until_verified_stage():
    source = INSTALLER.read_text(encoding="utf-8")
    dispatch = source[source.index('if [ "$FORKMESH_REINSTALL" = "1" ]'): ]
    assert 'if [ -n "$FORKMESH_EXPECTED_BUILD_COMMIT" ]; then' in dispatch
    assert "DEFER_PINNED_REINSTALL=1" in dispatch
    install_start = source.index("_install_binary() {")
    install_end = source.index("\n}\n", install_start)
    install_fn = source[install_start:install_end]
    assert install_fn.index('staged="$(mktemp') < install_fn.index(
        "uninstall_forkmesh reinstall"
    )
    assert install_fn.index("uninstall_forkmesh reinstall") < install_fn.index(
        'mv -f "$staged" "$BIN"'
    )


def test_commit_pinned_install_rejects_stale_same_semver_release():
    # Version strings are deliberately absent from this gate: two v0.7.0
    # artifacts can be different.  Even with valid bytes/checksum, release.json
    # from an older commit must be rejected before installation.
    payload = b"\x7fELF stale but checksum-valid v0.7.0 artifact" * 20
    digest = hashlib.sha256(payload).hexdigest()
    current_commit = "b2" * 20
    stale_commit = "c3" * 20
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"tag":"v0.7.0","tag_commit":"%s","build_commit":"%s",'
        '"channel":"latest",'
        '"assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % ("f0" * 20, stale_commit, digest)
    )
    rc, installed, stderr, blob_url = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        manifest=manifest,
        expected_build_commit=current_commit,
        expected_version="0.7.0",
    )
    assert rc != 0
    assert installed is None
    assert blob_url is None
    assert "refusing the same-version artifact" in stderr


def test_commit_pinned_install_rejects_manifest_free_legacy_release():
    payload = b"\x7fELF unprovable legacy artifact" * 20
    digest = hashlib.sha256(payload).hexdigest()
    rc, installed, stderr, blob_url = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        expected_build_commit="d4" * 20,
        expected_version="0.7.0",
    )
    assert rc != 0
    assert installed is None
    assert blob_url is None
    assert "does not match required build commit" in stderr


def test_candidate_build_commit_mismatch_preserves_installed_binary():
    required = "e5" * 20
    payload = (
        "#!/bin/sh\n"
        'case "$1" in\n'
        '  --version) printf "ForkMesh 0.7.0\\n" ;;\n'
        '  --build-commit) printf "%s\\n" ;;\n'
        "  *) exit 2 ;;\n"
        "esac\n" % ("f6" * 20)
    ).encode()
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"tag":"v0.7.0","tag_commit":"%s","build_commit":"%s",'
        '"channel":"latest","assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % ("a7" * 20, required, digest)
    )
    old_binary = b"old-known-good-binary"
    rc, installed, stderr, _ = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        manifest=manifest,
        expected_build_commit=required,
        expected_version="0.7.0",
        existing_binary=old_binary,
    )
    assert rc != 0
    assert installed == old_binary
    assert "leaving the existing node untouched" in stderr


def test_candidate_version_mismatch_preserves_installed_binary():
    required = "a8" * 20
    payload = (
        "#!/bin/sh\n"
        'case "$1" in\n'
        '  --version) printf "ForkMesh 0.6.20\\n" ;;\n'
        '  --build-commit) printf "%s\\n" ;;\n'
        "  *) exit 2 ;;\n"
        "esac\n" % required
    ).encode()
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"tag":"v0.7.0","tag_commit":"%s","build_commit":"%s",'
        '"channel":"latest","assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % ("b9" * 20, required, digest)
    )
    old_binary = b"old-known-good-binary"
    rc, installed, stderr, _ = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        manifest=manifest,
        expected_build_commit=required,
        expected_version="0.7.0",
        existing_binary=old_binary,
    )
    assert rc != 0
    assert installed == old_binary
    assert "required ForkMesh version" in stderr


def test_commit_pinned_install_requires_a_working_checksum_tool():
    required = "ca" * 20
    payload = (
        "#!/bin/sh\n"
        'case "$1" in\n'
        '  --version) printf "ForkMesh 0.7.0\\n" ;;\n'
        '  --build-commit) printf "%s\\n" ;;\n'
        "  *) exit 2 ;;\n"
        "esac\n" % required
    ).encode()
    digest = hashlib.sha256(payload).hexdigest()
    manifest = (
        '{"schema":"forkmesh-release-v1","repo":"forkmesh/forkmesh",'
        '"tag":"v0.7.0","tag_commit":"%s","build_commit":"%s",'
        '"channel":"latest","assets":[{"name":"forkmesh-linux-x86_64",'
        '"blob_sha256":"%s"}]}' % ("da" * 20, required, digest)
    )
    old_binary = b"old-known-good-binary"
    rc, installed, stderr, _ = _run(
        "%s  forkmesh-linux-x86_64\n" % digest,
        payload,
        manifest=manifest,
        expected_build_commit=required,
        expected_version="0.7.0",
        existing_binary=old_binary,
        no_checksum_tool=True,
    )
    assert rc != 0
    assert installed == old_binary
    assert "working sha256 tool is required" in stderr


def test_exact_staged_copy_is_checked_before_atomic_swap(tmp_path):
    out_dir = tmp_path / "out"
    bin_dir = out_dir / "bin"
    bin_dir.mkdir(parents=True)
    old_binary = b"old-known-good-binary"
    (bin_dir / "forkmesh").write_bytes(old_binary)
    candidate = tmp_path / "candidate"
    candidate.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    candidate.chmod(0o755)
    digest = hashlib.sha256(candidate.read_bytes()).hexdigest()

    stub_bin = tmp_path / "stubbin"
    stub_bin.mkdir()
    # Simulate bytes changing during the candidate -> destination-filesystem
    # copy. Verification must run on the staged path, not only on candidate.
    (stub_bin / "install").write_text(
        """#!/bin/sh
dest=""
for arg in "$@"; do dest="$arg"; done
printf '#!/bin/sh\\nexit 99\\n' > "$dest"
chmod 0755 "$dest"
""",
        encoding="utf-8",
    )
    (stub_bin / "install").chmod(0o755)
    env = os.environ.copy()
    env.update(
        {
            "OUT_DIR": str(out_dir),
            "CANDIDATE": str(candidate),
            "EXPECTED_HASH": digest,
            "FORKMESH_EXPECTED_BUILD_COMMIT": "ab" * 20,
            "FORKMESH_EXPECTED_RELEASE_VERSION": "0.7.0",
            "PATH": str(stub_bin) + os.pathsep + env["PATH"],
        }
    )
    script = (
        PREAMBLE
        + _release_functions()
        + '\nif _install_binary "$CANDIDATE" "$EXPECTED_HASH"; then exit 9; fi\n'
    )
    proc = subprocess.run(
        ["bash", "-c", script], env=env, text=True, capture_output=True
    )
    assert proc.returncode == 0, proc.stderr
    assert (bin_dir / "forkmesh").read_bytes() == old_binary
    assert "Staged ForkMesh binary failed its sha256 check" in proc.stderr


def test_probe_force_kills_a_candidate_that_ignores_sigterm(tmp_path):
    out_dir = tmp_path / "out"
    out_dir.mkdir()
    candidate = tmp_path / "ignores-term"
    candidate.write_text(
        "#!/bin/sh\ntrap '' TERM\nwhile :; do :; done\n", encoding="utf-8"
    )
    candidate.chmod(0o755)
    stub_bin = tmp_path / "stubbin"
    stub_bin.mkdir()
    # Collapse the 50 polling sleeps so this regression proves SIGKILL behavior
    # without adding five seconds to every test run.
    (stub_bin / "sleep").write_text("#!/bin/sh\n:\n", encoding="utf-8")
    (stub_bin / "sleep").chmod(0o755)
    env = os.environ.copy()
    env.update(
        {
            "OUT_DIR": str(out_dir),
            "CANDIDATE": str(candidate),
            "PATH": str(stub_bin) + os.pathsep + env["PATH"],
        }
    )
    script = (
        PREAMBLE
        + _release_functions()
        + '\nif _bounded_binary_probe "$CANDIDATE" --version; then exit 9; fi\n'
    )
    started = time.monotonic()
    proc = subprocess.run(
        ["bash", "-c", script],
        env=env,
        text=True,
        capture_output=True,
        timeout=3,
    )
    assert proc.returncode == 0, proc.stderr
    assert time.monotonic() - started < 2


# --- direct-upload install (adhoc #67) ---------------------------------------
# The desktop app's Hosts panel can stream the release binary over the SSH
# session and hand it to the installer as FORKMESH_LOCAL_BINARY (with the
# uploader's platform in FORKMESH_LOCAL_OS/ARCH). install_local_binary() must
# install a matching upload as-is and decline — falling back to the relay
# download — on a platform mismatch or a missing/empty upload.


def _local_binary_function():
    """Extract install_local_binary() from install.sh."""
    script = INSTALLER.read_text(encoding="utf-8")
    start = script.index("install_local_binary() {")
    end = script.index("\n}\n", start) + len("\n}\n")
    return script[start:end]


def _run_local(payload, *, os_decl="", arch_decl="", missing=False):
    """Run install_local_binary against an uploaded temp file.

    Returns (rc, installed BIN bytes or None, stderr). The fake environment
    matches _run's PREAMBLE: the target machine is linux/x86_64.
    """
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        upload = tmp / "upload.bin"
        if not missing:
            upload.write_bytes(payload)
        out_dir = tmp / "out"
        out_dir.mkdir()
        env = os.environ.copy()
        env["OUT_DIR"] = str(out_dir)
        script = (
            PREAMBLE
            + 'FORKMESH_LOCAL_BINARY="%s"\n' % upload
            + 'FORKMESH_LOCAL_OS="%s"\n' % os_decl
            + 'FORKMESH_LOCAL_ARCH="%s"\n' % arch_decl
            + _release_functions()  # provides _install_binary
            + _local_binary_function()
            + "\ninstall_local_binary\n")
        proc = subprocess.run(
            ["bash", "-c", script], env=env, text=True, capture_output=True)
        bin_path = out_dir / "bin" / "forkmesh"
        installed = bin_path.read_bytes() if bin_path.exists() else None
        return proc.returncode, installed, proc.stderr


def test_local_binary_installs_matching_upload():
    payload = b"\x7fELF uploaded forkmesh binary" * 40
    rc, installed, _ = _run_local(payload, os_decl="linux", arch_decl="x86_64")
    assert rc == 0
    assert installed == payload


def test_local_binary_installs_without_declared_platform():
    # An uploader that declares no platform is trusted (older/manual uploads).
    payload = b"\x7fELF undeclared platform upload"
    rc, installed, _ = _run_local(payload)
    assert rc == 0
    assert installed == payload


def test_local_binary_rejects_wrong_os():
    payload = b"\xcf\xfa\xed\xfe mach-o bytes"
    rc, installed, stderr = _run_local(
        payload, os_decl="macos", arch_decl="x86_64")
    assert rc != 0
    assert installed is None
    assert "falling back" in stderr


def test_local_binary_rejects_wrong_arch():
    payload = b"\x7fELF arm bytes"
    rc, installed, stderr = _run_local(
        payload, os_decl="linux", arch_decl="arm64")
    assert rc != 0
    assert installed is None
    assert "falling back" in stderr


def test_local_binary_missing_or_empty_upload_declines():
    rc, installed, stderr = _run_local(b"", missing=True)
    assert rc != 0
    assert installed is None
    assert "falling back" in stderr
    rc, installed, _ = _run_local(b"")  # exists but empty
    assert rc != 0
    assert installed is None
