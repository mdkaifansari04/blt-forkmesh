"""Keep Python Worker startup configuration within Cloudflare's limits."""

import re
import subprocess
import sys
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_runtime_sdk_is_not_uploaded_as_an_external_python_package():
    """The runtime supplies ``workers``; a lockfile uploads a duplicate SDK."""
    config = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
    project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))

    assert config["compatibility_flags"] == ["python_workers"]
    # Free Workers rejects deploy when [limits].cpu_ms is set (API code 100328).
    assert "limits" not in config
    assert project["project"]["dependencies"] == []
    assert not (ROOT / "pylock.toml").exists()


def test_worker_build_compacts_python_after_ast_equivalence_check():
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "build_worker_python.py"),
         "--check"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    match = re.fullmatch(
        r"worker python: (\d+) -> (\d+) bytes \(saved (\d+), ([\d.]+)%\)\n",
        result.stdout,
    )
    assert match is not None
    before, after, saved = map(int, match.group(1, 2, 3))
    assert before - after == saved
    assert after < before * 0.8
