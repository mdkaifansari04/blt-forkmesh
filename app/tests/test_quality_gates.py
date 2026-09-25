import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "quality_gate.py"


def test_quality_gate_config_validates_from_repository_root():
    result = subprocess.run(
        [sys.executable, str(TOOL), "validate"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    assert "configuration is valid" in result.stdout
