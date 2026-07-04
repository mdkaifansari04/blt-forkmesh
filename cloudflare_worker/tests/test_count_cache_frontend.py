#!/usr/bin/env python3
"""Counts render instantly from a cached last-known value (issue #218).

The home and network pages used to show blank placeholders ("Loading",
"Checking…", "—") until the first network fetch returned. These contract tests
pin the stale-while-revalidate behavior: the last-known counts are persisted to
localStorage and rendered immediately on load, then refreshed by the live poll.
"""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
STATIC = (PUBLIC / "static-page.js").read_text(encoding="utf-8")


def test_static_page_defines_and_primes_cached_stats():
    assert "function readCachedStat(" in STATIC
    assert "function writeCachedStat(" in STATIC
    assert "function primeCachedStats(" in STATIC
    # Priming happens before the first poll kicks off.
    assert STATIC.index("primeCachedStats();") < STATIC.rindex("startStats();")


def test_static_page_persists_network_counts():
    poll = STATIC[STATIC.index("async function pollStats") : STATIC.index("function startStats")]
    assert 'writeCachedStat("clients", clients);' in poll
    assert 'writeCachedStat("hosts", hosts);' in poll
    assert 'writeCachedStat("repos", repos);' in poll


def test_static_page_skips_priming_in_file_preview():
    prime = STATIC[STATIC.index("function primeCachedStats") : STATIC.index("// Don't poll")]
    assert 'location.protocol === "file:"' in prime
