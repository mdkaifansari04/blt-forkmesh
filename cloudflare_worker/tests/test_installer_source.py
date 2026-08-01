#!/usr/bin/env python3
"""Installer source-selection regression checks (stdlib only)."""

import os
from pathlib import Path
import subprocess
import tempfile


INSTALLER = Path(__file__).resolve().parents[1] / "public" / "install.sh"


def _base_env():
    env = os.environ.copy()



    env.pop("FORKMESH_REPO", None)
    return env


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
        env = _base_env()
        env["FORKMESH_TEST_RESPONSE"] = response
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        return subprocess.run(
            ["bash", "-c", _selection_prefix()], env=env,
            text=True, capture_output=True, check=False,
        )


def test_installer_uses_selected_online_node():
    result = _run_with_response(
        '{"ok":true,"node":"forkmesh","repo":"forkmesh","totalMinutes":486}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/forkmesh/forkmesh" in result.stdout


def test_installer_uses_first_of_ranked_node_list():


    result = _run_with_response(
        '{"ok":true,"node":"alpha","nodes":["alpha","bravo","charlie"],'
        '"repo":"forkmesh","totalMinutes":500}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/forkmesh/forkmesh" in result.stdout
    assert "CANDIDATES=https://forkmesh.com/forkmesh/forkmesh" in result.stdout


def test_installer_drops_malformed_and_duplicate_nodes():


    result = _run_with_response(
        '{"ok":true,"node":"alpha","nodes":["alpha","bad/../id","alpha","bravo"],'
        '"repo":"forkmesh","totalMinutes":1}'
    )
    assert result.returncode == 0, result.stderr
    assert "CANDIDATES=https://forkmesh.com/forkmesh/forkmesh" in result.stdout


def test_installer_falls_back_to_single_node_field():


    result = _run_with_response(
        '{"ok":true,"node":"solo","repo":"forkmesh","totalMinutes":7}'
    )
    assert result.returncode == 0, result.stderr
    assert "REPO=https://forkmesh.com/forkmesh/forkmesh" in result.stdout
    assert "CANDIDATES=https://forkmesh.com/forkmesh/forkmesh" in result.stdout


def _run_prefix(response, extra_env):



    with tempfile.TemporaryDirectory() as tmp:
        bindir = Path(tmp) / "bin"
        bindir.mkdir()
        curl = bindir / "curl"
        curl.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$FORKMESH_TEST_RESPONSE\"\n",
            encoding="utf-8",
        )
        curl.chmod(0o755)



        pgrep = bindir / "pgrep"
        pgrep.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        pgrep.chmod(0o755)
        pkill = bindir / "pkill"
        pkill.write_text(
            "#!/bin/sh\necho 'unexpected pkill in installer unit test' >&2\nexit 99\n",
            encoding="utf-8",
        )
        pkill.chmod(0o755)
        home = Path(tmp) / "home"
        home.mkdir()
        env = _base_env()
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



    result = _run_prefix(
        '{"ok":true,"node":"forkmesh","repo":"forkmesh","totalMinutes":9}',
        {"FORKMESH_REINSTALL": "1", "FORKMESH_NO_LAUNCH": "1"},
    )
    assert result.returncode == 0, result.stderr
    assert "Reinstall requested" in result.stdout

    assert "REPO=https://forkmesh.com/forkmesh/forkmesh" in result.stdout


def test_installer_echoes_owner_when_attached():


    result = _run_prefix(
        '{"ok":true,"node":"newnewnode","repo":"forkmesh","totalMinutes":9}',
        {"FORKMESH_OWNER": "alice", "FORKMESH_NODE_NAME": "vps-1"},
    )
    assert result.returncode == 0, result.stderr
    assert "Owner:  alice" in result.stdout
    assert "Node:   vps-1" in result.stdout


def test_installer_rejects_dangerous_source_targets():
    result = _run_prefix(
        '{"ok":true,"node":"forkmesh","repo":"forkmesh"}',
        {"FORKMESH_DIR": "/"},
    )
    assert result.returncode != 0
    assert "unsafe FORKMESH_DIR target" in result.stderr


def test_installer_refuses_unmanaged_existing_custom_source(tmp_path):
    source = tmp_path / "forkmesh" / "src"
    source.mkdir(parents=True)
    sentinel = source / "operator-data"
    sentinel.write_text("preserve me", encoding="utf-8")
    result = _run_prefix(
        '{"ok":true,"node":"forkmesh","repo":"forkmesh"}',
        {"FORKMESH_DIR": str(source)},
    )
    assert result.returncode != 0
    assert "not marked as an owner-matched ForkMesh install" in result.stderr
    assert sentinel.read_text(encoding="utf-8") == "preserve me"


def test_installer_refuses_symlink_source_target(tmp_path):
    real = tmp_path / "real"
    real.mkdir()
    source = tmp_path / "forkmesh" / "src"
    source.parent.mkdir()
    source.symlink_to(real, target_is_directory=True)
    result = _run_prefix(
        '{"ok":true,"node":"forkmesh","repo":"forkmesh"}',
        {"FORKMESH_DIR": str(source)},
    )
    assert result.returncode != 0
    assert "non-symlink path" in result.stderr or "may not be a symlink" in result.stderr


def test_installer_stops_when_no_mirror_is_online():
    result = _run_with_response(
        '{"ok":false,"error":"no_online_install_source"}'
    )
    assert result.returncode != 0
    assert "No online ForkMesh node is currently mirroring 'forkmesh'" in result.stderr
    assert "REPO=" not in result.stdout


def test_installer_skips_mirror_lookup_when_uploading_a_binary():




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
        env = _base_env()
        env["FORKMESH_TEST_RESPONSE"] = '{"ok":false,"error":"no_online_install_source"}'
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        env["FORKMESH_NO_DIAG"] = "1"
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
        env = _base_env()
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

        assert "REPO=\n" in result.stdout
        assert "CANDIDATES=\n" in result.stdout

        assert "REPO2=https://forkmesh.com/forkmesh/forkmesh" in result.stdout
        assert (
            "CANDIDATES2=https://forkmesh.com/forkmesh/forkmesh"
            in result.stdout
        )


def _clone_functions():


    script = INSTALLER.read_text(encoding="utf-8")
    start = script.index("# Extract the node segment")
    end = script.index("fetch_source() {")
    return script[start:end]


def test_clean_clone_falls_back_past_unreachable_mirror():


    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        bindir = tmp / "bin"
        bindir.mkdir()


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


            "if clone_ok=1 && clean_clone; then :; else clone_ok=0; fi\n"
            'printf "RESULT_REPO=%s\\n" "$REPO"\n'
            'printf "CLONE_OK=%s\\n" "$clone_ok"\n'
        )
        env = _base_env()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        result = subprocess.run(
            ["bash", "-c", harness], env=env, text=True,
            capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr + result.stdout
        assert "CLONE_OK=1" in result.stdout

        assert "RESULT_REPO=https://forkmesh.com/bravo/forkmesh" in result.stdout

        assert ("Mirror 'alpha' could not be cloned: "
                "mirror endpoint timed out (HTTP 504)") in result.stderr
        assert src.is_dir()


def test_clean_clone_reports_when_all_mirrors_time_out():


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
        env = _base_env()
        env["PATH"] = str(bindir) + os.pathsep + env.get("PATH", "")
        result = subprocess.run(
            ["bash", "-c", harness], env=env, text=True,
            capture_output=True, check=False,
        )
        assert result.returncode == 0, result.stderr + result.stdout
        assert "CLONE_FAILED" in result.stdout
        assert "REASON=mirror endpoint timed out (HTTP 504)" in result.stdout
        assert "PIN=0" in result.stdout
