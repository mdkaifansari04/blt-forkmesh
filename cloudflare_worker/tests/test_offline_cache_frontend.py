#!/usr/bin/env python3
"""Offline-browse cache + mirror-node visibility contract tests.

Locks in two website behaviours:
  * the "cached · host offline" badge shows a cache-expiry countdown and an
    Expire-cache button that drops the snapshot and reloads live; and
  * the repo detail page lists every hosting node — even a lone or offline one —
    instead of hiding the section when only one node is present.
"""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
CATALOG = (PUBLIC / "catalog.js").read_text(encoding="utf-8")
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")


def test_cache_entries_are_timestamped_with_a_ttl():
    # New entries carry a write time so the UI can show a countdown, and a stale
    # snapshot is dropped on read instead of resurfacing as fresh.
    assert "CACHE_TTL_MS" in CATALOG
    assert 'JSON.stringify({ v: value, t: Date.now() })' in CATALOG
    assert "Date.now() - cachedAt > CACHE_TTL_MS" in CATALOG


def test_cached_badge_shows_countdown_and_expire_button():
    assert "function formatCacheCountdown" in CATALOG
    assert "expires in" in CATALOG
    assert "function expireRepoCache" in CATALOG
    assert "cache-expire-btn" in CATALOG
    assert "Expire cache" in CATALOG
    assert ".cache-expire-btn" in STYLES


def test_expire_button_drops_snapshot_then_reloads_live():
    render = CATALOG[
        CATALOG.index("function renderCachedMeta")
        : CATALOG.index("async function pullPath")
    ]
    assert "expireRepoCache(owner, name);" in render
    assert "reload();" in render


def test_mirror_nodes_section_shows_a_lone_or_offline_node():
    render = CATALOG[
        CATALOG.index("function renderMirrorNodes")
        : CATALOG.index("function openRepoPage")
    ]
    # The section is hidden only when no node hosts the repo at all — a single,
    # offline node must still render with its status pill.
    assert "members.length < 1" in render
    assert "members.length <= 1" not in render
    assert 'm.liveHost ? "status-pill online" : "status-pill offline"' in render
