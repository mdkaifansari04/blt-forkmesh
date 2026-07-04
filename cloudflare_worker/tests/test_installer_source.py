#!/usr/bin/env python3
"""Installer source-selection regression checks (stdlib only)."""

import os
from pathlib import Path
import subprocess
import tempfile


INSTALLER = Path(__file__).resolve().parents[1] / "public" / "install.sh"


def _selection_prefix():
    script = INSTALLER.read_text(encoding="utf-8")
    marker = "# --- privilege escalation"
    assert marker in script
    return script.split(marker, 1)[0] + (
        '\nprintf "REPO=%s\\n" "$REPO"\n'
        'printf "CANDIDATES=%s\\n" "${REPO_CANDIDATES[*]}"\n'
    )


def _run_with_response(response):
    with tempfile.TemporaryDirectory() as tmp:
        bindir = Path(tmp) / "bin"
        bindir.mkdir()
        curl = bindir / "curl"
        curl.write_text(
            """#!/bin/sh
printf '%s\\n' "$FORKMESH_TEST_RESPONSE"
""",
            encoding="utf-8",
        )
        curl.chmod(0o755)
        env = os.environ.copy()
        env["FORKMESH_TEST_RESPONSE"] = response
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        return subprocess.run(
            ["bash", "-c", _selection_prefix()], env=env,
            text=True, capture_output=True, check=False,
        )


def test_installer_uses_selected_online_node():
    result = _run_with_response(
        '{"ok":true,"node":"newnewnode","repo":"forkmesh","totalMinutes":486}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/newnewnode/forkmesh" in result.stdout


def test_installer_uses_first_of_ranked_node_list():
    # Newer mainnode returns a ranked "nodes" list; the installer clones from the
    # best (first) and keeps the rest as ordered fallback candidates.
    result = _run_with_response(
        '{"ok":true,"node":"alpha","nodes":["alpha","bravo","charlie"],'
        '"repo":"forkmesh","totalMinutes":500}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/alpha/forkmesh" in result.stdout
    assert (
        "CANDIDATES=https://forkmesh.com/alpha/forkmesh "
        "https://forkmesh.com/bravo/forkmesh "
        "https://forkmesh.com/charlie/forkmesh"
    ) in result.stdout


def test_installer_drops_malformed_and_duplicate_nodes():
    # A bad node id (slashes) is rejected and a repeated id collapses, but the
    # server-ranked order of the survivors is preserved.
    result = _run_with_response(
        '{"ok":true,"node":"alpha","nodes":["alpha","bad/../id","alpha","bravo"],'
        '"repo":"forkmesh","totalMinutes":1}'
    )
    assert result.returncode == 0, result.stderr
    assert (
        "CANDIDATES=https://forkmesh.com/alpha/forkmesh "
        "https://forkmesh.com/bravo/forkmesh"
    ) in result.stdout


def test_installer_falls_back_to_single_node_field():
    # An older mainnode without a "nodes" array still resolves via "node", and
    # that single mirror becomes the only clone candidate.
    result = _run_with_response(
        '{"ok":true,"node":"solo","repo":"forkmesh","totalMinutes":7}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/solo/forkmesh" in result.stdout
    assert "CANDIDATES=https://forkmesh.com/solo/forkmesh" in result.stdout


def _run_prefix(response, extra_env):
    # Like _run_with_response but sandboxes HOME/XDG so uninstall_forkmesh can be
    # exercised (it rm's under $HOME) without touching the real machine, and lets
    # a test set extra env (FORKMESH_REINSTALL, FORKMESH_OWNER, ...).
    with tempfile.TemporaryDirectory() as tmp:
        bindir = Path(tmp) / "bin"
        bindir.mkdir()
        curl = bindir / "curl"
        curl.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$FORKMESH_TEST_RESPONSE\"\n",
            encoding="utf-8",
        )
        curl.chmod(0o755)
        home = Path(tmp) / "home"
        home.mkdir()
        env = os.environ.copy()
        env["FORKMESH_TEST_RESPONSE"] = response
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["HOME"] = str(home)
        env["XDG_DATA_HOME"] = str(home / ".local/share")
        env["XDG_CONFIG_HOME"] = str(home / ".config")
        env["XDG_CACHE_HOME"] = str(home / ".cache")
        env.update(extra_env)
        return subprocess.run(
            ["bash", "-c", _selection_prefix()], env=env,
            text=True, capture_output=True, check=False,
        )


def test_installer_reinstall_wipes_then_continues():
    # --reinstall / FORKMESH_REINSTALL=1 runs the uninstall wipe first, then falls
    # through to the normal install (the source resolution still happens), rather
    # than exiting after the uninstall (adhoc #258).
    result = _run_prefix(
        '{"ok":true,"node":"newnewnode","repo":"forkmesh","totalMinutes":9}',
        {"FORKMESH_REINSTALL": "1", "FORKMESH_NO_LAUNCH": "1"},
    )
    assert result.returncode == 0, result.stderr
    assert "Reinstall requested" in result.stdout
    # It carried on into the install (resolved a source) instead of exiting.
    assert "REPO=https://forkmesh.com/newnewnode/forkmesh" in result.stdout


def test_installer_echoes_owner_when_attached():
    # The Hosts panel passes FORKMESH_OWNER so the operator can confirm which
    # account the fresh node is attached to (adhoc #258); the installer echoes it.
    result = _run_prefix(
        '{"ok":true,"node":"newnewnode","repo":"forkmesh","totalMinutes":9}',
        {"FORKMESH_OWNER": "alice", "FORKMESH_NODE_NAME": "vps-1"},
    )
    assert result.returncode == 0, result.stderr
    assert "Owner:  alice" in result.stdout
    assert "Node:   vps-1" in result.stdout


def test_installer_stops_when_no_mirror_is_online():
    result = _run_with_response(
        '{"ok":false,"error":"no_online_install_source"}'
    )
    assert result.returncode != 0
    assert "No online ForkMesh node is currently mirroring 'forkmesh'" in result.stderr
    assert "REPO=" not in result.stdout


def test_installer_skips_mirror_lookup_when_uploading_a_binary():
    # Direct-upload installs (adhoc #67/#70) stream the binary over the same SSH
    # session, so a missing/offline mirror must not block them: resolve_install_node
    # (and its curl call to the mainnode) must not run at all when
    # FORKMESH_LOCAL_BINARY is set, even if no mirror is actually online.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        bindir = tmp / "bin"
        bindir.mkdir()
        marker = tmp / "resolve_curl_called"
        curl = bindir / "curl"
        curl.write_text(
            f"#!/bin/sh\ntouch {marker}\n"
            'printf \'%s\\n\' "$FORKMESH_TEST_RESPONSE"\n',
            encoding="utf-8",
        )
        curl.chmod(0o755)
        env = os.environ.copy()
        env["FORKMESH_TEST_RESPONSE"] = '{"ok":false,"error":"no_online_install_source"}'
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["FORKMESH_NO_DIAG"] = "1"  # isolate resolve_install_node's curl use
        env["FORKMESH_LOCAL_BINARY"] = "/tmp/fake-forkmesh-upload"
        result = subprocess.run(
            ["bash", "-c", _selection_prefix()], env=env,
            text=True, capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr
        assert "REPO=\n" in result.stdout
        assert "CANDIDATES=\n" in result.stdout
        assert not marker.exists()


def test_installer_still_resolves_a_mirror_lazily_if_upload_is_unusable():
    # install_prebuilt_release calls ensure_mirror_candidates itself, so a local
    # upload that turns out to be unusable (bad platform, empty file, ...) still
    # falls back to a real mirror lookup instead of leaving REPO_CANDIDATES empty.
    prefix = _selection_prefix() + (
        "\nensure_mirror_candidates\n"
        'printf "REPO2=%s\\n" "$REPO"\n'
        'printf "CANDIDATES2=%s\\n" "${REPO_CANDIDATES[*]}"\n'
    )
    with tempfile.TemporaryDirectory() as tmp:
        bindir = Path(tmp) / "bin"
        bindir.mkdir()
        curl = bindir / "curl"
        curl.write_text(
            """#!/bin/sh
printf '%s\\n' "$FORKMESH_TEST_RESPONSE"
""",
            encoding="utf-8",
        )
        curl.chmod(0o755)
        env = os.environ.copy()
        env["FORKMESH_TEST_RESPONSE"] = (
            '{"ok":true,"node":"fallback-node","repo":"forkmesh","totalMinutes":1}'
        )
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["FORKMESH_LOCAL_BINARY"] = "/tmp/fake-forkmesh-upload"
        result = subprocess.run(
            ["bash", "-c", prefix], env=env, text=True,
            capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr
        # Skipped up front (matches the previous test) ...
        assert "REPO=\n" in result.stdout
        assert "CANDIDATES=\n" in result.stdout
        # ... but resolves once something actually needs a mirror.
        assert "REPO2=https://forkmesh.com/fallback-node/forkmesh" in result.stdout
        assert "CANDIDATES2=https://forkmesh.com/fallback-node/forkmesh" in result.stdout


def _clone_functions():
    # repo_node + classify_clone_failure + clean_clone, sliced out so the
    # multi-mirror fallback can be driven directly with a fake `git`.
    script = INSTALLER.read_text(encoding="utf-8")
    start = script.index("# Extract the node segment")
    end = script.index("fetch_source() {")
    return script[start:end]


def test_clean_clone_falls_back_past_unreachable_mirror():
    # The whole point of the fix: when the best mirror's git tunnel times out
    # (504), the installer must try the next online mirror instead of failing.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        bindir = tmp / "bin"
        bindir.mkdir()
        # Fake git: the first mirror (alpha) returns the relay's 504; the second
        # (bravo) "clones" by creating the destination directory.
        git = bindir / "git"
        git.write_text(
            "#!/bin/sh\n"
            'url=$4; dest=$5\n'
            'case "$url" in\n'
            '  *alpha*)\n'
            '    echo "remote: Host timed out."\n'
            "    echo \"fatal: unable to access '$url': The requested URL returned error: 504\"\n"
            '    exit 1 ;;\n'
            '  *) mkdir -p "$dest"; exit 0 ;;\n'
            'esac\n',
            encoding="utf-8",
        )
        git.chmod(0o755)
        src = tmp / "share" / "forkmesh" / "src"
        harness = (
            "set -euo pipefail\n"
            'say()  { printf "==> %s\\n" "$1"; }\n'
            'warn() { printf "Warning: %s\\n" "$1" >&2; }\n'
            'dbg()  { :; }\n'
            'MANAGED_MARKER=".forkmesh-managed"\n'
            "PIN_FAILURE=0\n"
            'CLONE_FAIL_REASON=""\n'
            f'SRC="{src}"\n'
            'REPO=""\n'
            'REPO_CANDIDATES=("https://forkmesh.com/alpha/forkmesh" '
            '"https://forkmesh.com/bravo/forkmesh")\n'
            + _clone_functions()
            + 'mkdir -p "$(dirname "$SRC")"\n'
            # Call via `if` like the real fetch_source does, so `set -e` is
            # disabled inside clean_clone (a failing clone is handled, not fatal).
            "if clone_ok=1 && clean_clone; then :; else clone_ok=0; fi\n"
            'printf "RESULT_REPO=%s\\n" "$REPO"\n'
            'printf "CLONE_OK=%s\\n" "$clone_ok"\n'
        )
        env = os.environ.copy()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        result = subprocess.run(
            ["bash", "-c", harness], env=env, text=True,
            capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr + result.stdout
        assert "CLONE_OK=1" in result.stdout
        # Fell through to the second mirror and recorded it as the working repo.
        assert "RESULT_REPO=https://forkmesh.com/bravo/forkmesh" in result.stdout
        # Named the unreachable mirror and the 504 reason for the operator.
        assert ("Mirror 'alpha' could not be cloned: "
                "mirror host timed out (HTTP 504)") in result.stderr
        assert src.is_dir()


def test_clean_clone_reports_when_all_mirrors_time_out():
    # Every mirror down: clean_clone fails and leaves a 504 reason for the final
    # error/diagnostics, without falsely flagging an integrity-pin failure.
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        bindir = tmp / "bin"
        bindir.mkdir()
        git = bindir / "git"
        git.write_text(
            "#!/bin/sh\n"
            'echo "remote: Host timed out."\n'
            'echo "fatal: ... The requested URL returned error: 504"\n'
            "exit 1\n",
            encoding="utf-8",
        )
        git.chmod(0o755)
        src = tmp / "src"
        harness = (
            "set -euo pipefail\n"
            'say()  { :; }\n'
            'warn() { :; }\n'
            'dbg()  { :; }\n'
            'MANAGED_MARKER=".forkmesh-managed"\n'
            "PIN_FAILURE=0\n"
            'CLONE_FAIL_REASON=""\n'
            f'SRC="{src}"\n'
            'REPO=""\n'
            'REPO_CANDIDATES=("https://forkmesh.com/a/forkmesh" '
            '"https://forkmesh.com/b/forkmesh")\n'
            + _clone_functions()
            + 'mkdir -p "$(dirname "$SRC")"\n'
            "if clean_clone; then echo UNEXPECTED_OK; else echo CLONE_FAILED; fi\n"
            'printf "REASON=%s\\n" "$CLONE_FAIL_REASON"\n'
            'printf "PIN=%s\\n" "$PIN_FAILURE"\n'
        )
        env = os.environ.copy()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        result = subprocess.run(
            ["bash", "-c", harness], env=env, text=True,
            capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr + result.stdout
        assert "CLONE_FAILED" in result.stdout
        assert "REASON=mirror host timed out (HTTP 504)" in result.stdout
        assert "PIN=0" in result.stdout
