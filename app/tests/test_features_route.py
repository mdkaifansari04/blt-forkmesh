#!/usr/bin/env python3
"""Feature-section routing contracts for the public homepage."""

from pathlib import Path
import tomllib
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import static_routes  # noqa: E402

ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))


def test_features_path_uses_spa_fallback_instead_of_deleted_feature_page():
    assert 'if url.path == "/features":' not in ENTRY_TEXT
    assert 'headers={"location": "/feature.html"}' not in ENTRY_TEXT


def test_app_features_path_runs_worker_first_to_reach_its_www_owner():
    run_worker_first = WRANGLER["assets"]["run_worker_first"]

    assert "/features" in run_worker_first
    assert static_routes.external_site_route("/features") == "www"
    assert '"WWW_ORIGIN"' in ENTRY_TEXT
    assert 'return Response("", status=308, headers={"location": destination})' in ENTRY_TEXT
