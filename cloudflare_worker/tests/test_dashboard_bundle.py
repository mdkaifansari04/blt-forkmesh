#!/usr/bin/env python3
"""Static contracts for the dashboard.js fragment build.

``public/dashboard.js`` (~5450 lines) is split into ordered fragment files under
``public/dashboard/js/``. ``tools/build_dashboard_assets.py`` builds the static
``public/dashboard.js`` served by Cloudflare, mirroring how the HTML shell is
composed from partials (see ``src/dashboard_bundle.py`` / ``src/dashboard_shell.py``).
"""

import sys
import tomllib
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

_ROOT = Path(__file__).resolve().parents[1]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

import dashboard_bundle

PUBLIC = _ROOT / "public"
JS_DIR = PUBLIC / "dashboard" / "js"
ENTRY_TEXT = (_SRC / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((_ROOT / "wrangler.toml").read_text(encoding="utf-8"))


def test_static_dashboard_js_is_built_from_fragments():
    assert (PUBLIC / "dashboard.js").is_file()
    assert (PUBLIC / "dashboard.js").read_text(encoding="utf-8") == assembled_dashboard_js()


def test_every_fragment_listed_in_order_exists_on_disk():
    assert dashboard_bundle.FRAGMENTS, "no fragments declared"
    for name in dashboard_bundle.FRAGMENTS:
        assert (JS_DIR / name).is_file(), "missing fragment %s" % name


    assert list(dashboard_bundle.FRAGMENTS) == sorted(dashboard_bundle.FRAGMENTS)


def test_no_orphan_fragments_on_disk():


    on_disk = sorted(p.name for p in JS_DIR.glob("*.js"))
    assert on_disk == sorted(dashboard_bundle.FRAGMENTS)


def test_composed_bundle_is_the_single_iife():
    js = assembled_dashboard_js()


    assert js.startswith("(() => {")
    assert js.rstrip().endswith("})();")
    assert js.count("(() => {") >= 1
    assert "\n  if (initSharedChrome()) {\n" in js
    assert "PAGE_INITS[currentPage()]" in js

    for marker in ("const state = {", "function renderRepoDetail(", "function initSharedChrome("):
        assert marker in js


def test_composition_matches_on_disk_fragments():
    expected = dashboard_bundle.assemble_bundle(
        (JS_DIR / name).read_text(encoding="utf-8")
        for name in dashboard_bundle.FRAGMENTS)
    assert assembled_dashboard_js() == expected


def test_dashboard_js_is_served_as_a_static_asset():
    assert "from dashboard_bundle import" not in ENTRY_TEXT
    assert 'if url.path == "/dashboard.js":' not in ENTRY_TEXT
    assert "async def _serve_dashboard_bundle" not in ENTRY_TEXT
    assert "/dashboard.js" not in WRANGLER["assets"]["run_worker_first"]


def test_shell_still_references_dashboard_js():
    shell = (PUBLIC / "dashboard" / "shell.html").read_text(encoding="utf-8")


    assert 'src="/dashboard.js" defer' in shell
    assert "?v=" not in shell
