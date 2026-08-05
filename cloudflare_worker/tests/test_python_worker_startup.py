"""Keep Python Worker startup configuration within Cloudflare's limits."""

import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_runtime_sdk_is_not_uploaded_as_an_external_python_package():
    """The runtime supplies ``workers``; a lockfile uploads a duplicate SDK."""
    config = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
    project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))

    assert config["compatibility_flags"] == ["python_workers"]
    assert project["project"]["dependencies"] == []
    assert not (ROOT / "pylock.toml").exists()
