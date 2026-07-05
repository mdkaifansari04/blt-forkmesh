import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_pywrangler_wrapper_does_not_detect_itself_as_path_binary():
    script = """
        set -eu
        . ./pywrangler.sh
        if type -P pywrangler >/dev/null 2>&1; then
            exit 77
        fi
        command -v pywrangler
    """
    result = subprocess.run(
        ["bash", "-c", script],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "pywrangler"


def test_pywrangler_uses_local_venv_uvx_without_system_uv(tmp_path):
    venv = tmp_path / ".pywrangler"
    uvx = venv / "bin" / "uvx"
    uvx.parent.mkdir(parents=True)
    uvx.write_text(
        "#!/usr/bin/env sh\n"
        "printf 'uvx-call:%s\\n' \"$*\"\n"
    )
    uvx.chmod(0o755)

    script = f"""
        set -eu
        export PYWRANGLER_VENV='{venv.as_posix()}'
        PATH='/usr/bin:/bin'
        . ./pywrangler.sh
        pywrangler deploy --env \"\"
    """
    result = subprocess.run(
        ["bash", "-c", script],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "uvx-call:--from workers-py<1.14.0 pywrangler deploy --env "
