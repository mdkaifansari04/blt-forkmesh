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
