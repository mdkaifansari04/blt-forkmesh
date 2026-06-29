#!/usr/bin/env python3
"""Feature-section routing contracts for the public homepage."""

from pathlib import Path
import tomllib


ROOT = Path(__file__).resolve().parents[1]
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
INDEX_HTML = (ROOT / "public" / "index.html").read_text(encoding="utf-8")


def test_features_content_lives_on_homepage():
    assert '<section\n        id="features"' in INDEX_HTML
    assert "Open source should not depend on one company staying online forever" in INDEX_HTML
    assert not (ROOT / "public" / "feature.html").is_file()


def test_features_path_uses_spa_fallback_instead_of_deleted_feature_page():
    assert 'if url.path == "/features":' not in ENTRY_TEXT
    assert 'headers={"location": "/feature.html"}' not in ENTRY_TEXT


def test_features_path_does_not_run_worker_before_spa_fallback():
    run_worker_first = WRANGLER["assets"]["run_worker_first"]

    assert "/features" not in run_worker_first
