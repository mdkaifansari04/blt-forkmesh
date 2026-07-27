#!/usr/bin/env python3
"""Site-wide contract: one place computes every ``?v=`` cache-buster.

``dashboard_shell.CACHE_BUSTED_BUNDLES`` names the first-party client bundles,
``asset_versions`` content-hashes them, and ``tools/build_dashboard_assets.py``
stamps that one map into every document that links them — composed dashboard
pages and authored site/docs/blog pages alike. No page hand-writes a version, so
editing a bundle can never leave a stale query behind on the page someone forgot
to bump.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import dashboard_shell  # noqa: E402

from _dashboard_bundle import assembled_dashboard_js  # noqa: E402

# Served from public/ but deliberately unversioned.
UNVERSIONED_SCRIPTS = (
    # A loader shim; the real payload comes from PostHog's own CDN.
    "posthog.js",
    # ES module entry. Its static imports (chat-crypto.js, chat-attachments.js,
    # chat-room-transport.js) carry their own URLs inside the JS, so stamping
    # the entry alone would bust it while its graph kept serving stale — busting
    # it properly needs a module-graph hash, which this pass does not do.
    "chat.js",
)

SCRIPT_SRC_RE = re.compile(r'<script[^>]*\ssrc="/([\w.-]+\.js)(\?v=([^"]*))?"')


def _read(rel):
    return (PUBLIC / rel).read_text(encoding="utf-8")


def _versions():
    return dashboard_shell.asset_versions(
        _read, {"dashboard.js": assembled_dashboard_js()})


def _stamped_documents():
    """Every built/authored document the build stamps, ``public/``-relative."""
    authored = dashboard_shell.stamped_site_pages(
        p.relative_to(PUBLIC).as_posix() for p in PUBLIC.rglob("*.html"))
    return authored + [meta["asset"] for meta in dashboard_shell.PAGES.values()]


def test_every_first_party_script_carries_the_shared_content_hash():
    versions = _versions()
    seen = set()
    for rel in _stamped_documents():
        for name, query, version in SCRIPT_SRC_RE.findall(_read(rel)):
            if name in UNVERSIONED_SCRIPTS:
                assert not query, (rel, name)
                continue
            # A bundle referenced by a page but missing from the single source
            # of truth would silently ship uncacheable-busted.
            assert name in dashboard_shell.CACHE_BUSTED_BUNDLES, (rel, name)
            assert version == versions[name], (rel, name)
            seen.add(name)
    # Every listed bundle is actually referenced somewhere — no dead entries
    # quietly hashing a file nothing loads.
    assert seen == set(dashboard_shell.CACHE_BUSTED_BUNDLES)


def test_no_document_hand_writes_a_version_query():
    versions = set(_versions().values())
    for path in sorted(PUBLIC.rglob("*.html")):
        for _, query, version in SCRIPT_SRC_RE.findall(
                path.read_text(encoding="utf-8")):
            assert not query or version in versions, (path, version)


def test_stamping_is_idempotent_and_the_pages_are_current():
    versions = _versions()
    for rel in _stamped_documents():
        html = _read(rel)
        assert dashboard_shell.stamp_asset_versions(html, versions) == html, (
            "%s is stale — run tools/build_dashboard_assets.py" % rel)


def test_dashboard_sources_are_excluded_from_in_place_stamping():
    authored = dashboard_shell.stamped_site_pages(
        p.relative_to(PUBLIC).as_posix() for p in PUBLIC.rglob("*.html"))
    assert "dashboard/shell.html" not in authored
    assert "dashboard/partials/header.html" not in authored
    for meta in dashboard_shell.PAGES.values():
        assert meta["asset"] not in authored, meta["asset"]
    assert "login.html" in authored
    assert "docs/index.html" in authored
