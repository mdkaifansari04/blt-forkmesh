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
    return script.split(marker, 1)[0] + '\nprintf "REPO=%s\\n" "$REPO"\n'


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


def test_installer_stops_when_no_mirror_is_online():
    result = _run_with_response(
        '{"ok":false,"error":"no_online_install_source"}'
    )
    assert result.returncode != 0
    assert "No online ForkMesh node is currently mirroring 'forkmesh'" in result.stderr
    assert "REPO=" not in result.stdout
