#!/usr/bin/env python3
"""Static contracts for the dashboard.js -> fragment split.

``public/dashboard.js`` (~5450 lines) is split into ordered fragment files under
``public/dashboard/js/`` that the Worker concatenates into one ``/dashboard.js``
response at request time, mirroring how the HTML shell is composed from partials
(see ``src/dashboard_bundle.py`` / ``src/dashboard_shell.py``).
"""

import sys
import tomllib
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

_ROOT = Path(__file__).resolve().parents[1]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

import dashboard_bundle  # noqa: E402

PUBLIC = _ROOT / "public"
JS_DIR = PUBLIC / "dashboard" / "js"
ENTRY_TEXT = (_SRC / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((_ROOT / "wrangler.toml").read_text(encoding="utf-8"))


def test_monolithic_dashboard_js_is_gone():
    # The single 274KB file was replaced by the composed fragments; leaving it in
    # place would let it drift out of sync with what the Worker actually serves.
    assert not (PUBLIC / "dashboard.js").exists()


def test_every_fragment_listed_in_order_exists_on_disk():
    assert dashboard_bundle.FRAGMENTS, "no fragments declared"
    for name in dashboard_bundle.FRAGMENTS:
        assert (JS_DIR / name).is_file(), "missing fragment %s" % name
    # Names are numeric-prefixed so lexical order == source order; the declared
    # order must match a sorted listing so nothing is silently reordered.
    assert list(dashboard_bundle.FRAGMENTS) == sorted(dashboard_bundle.FRAGMENTS)


def test_no_orphan_fragments_on_disk():
    # Every *.js under dashboard/js/ must be declared in FRAGMENTS, or it would be
    # a dead file the Worker never serves (and never re-couples into the bundle).
    on_disk = sorted(p.name for p in JS_DIR.glob("*.js"))
    assert on_disk == sorted(dashboard_bundle.FRAGMENTS)


def test_composed_bundle_is_the_single_iife():
    js = assembled_dashboard_js()
    # The fragments are contiguous slices of ONE closure, so the concatenation
    # must open and close exactly one IIFE and still call init() at the end.
    assert js.startswith("(() => {")
    assert js.rstrip().endswith("})();")
    assert js.count("(() => {") >= 1
    assert "\n  init();\n" in js
    # A couple of load-bearing symbols that must survive the split intact.
    for marker in ("const state = {", "function renderRepoDetail(", "async function init("):
        assert marker in js


def test_composition_matches_on_disk_fragments():
    expected = dashboard_bundle.assemble_bundle(
        (JS_DIR / name).read_text(encoding="utf-8")
        for name in dashboard_bundle.FRAGMENTS)
    assert assembled_dashboard_js() == expected


def test_worker_routes_dashboard_js_through_python():
    assert "from dashboard_bundle import" in ENTRY_TEXT
    assert 'if url.path == "/dashboard.js":' in ENTRY_TEXT
    assert "async def _serve_dashboard_bundle" in ENTRY_TEXT
    # /dashboard.js must hit the Worker (not be served as a plain static asset)
    # so the composition runs; the fragments themselves ride /dashboard/*.
    assert "/dashboard.js" in WRANGLER["assets"]["run_worker_first"]


def test_shell_still_references_dashboard_js():
    shell = (PUBLIC / "dashboard" / "index.html").read_text(encoding="utf-8")
    assert 'src="/dashboard.js"' in shell
