#!/usr/bin/env python3
"""Clone-fallback selection: serve a repo from a healthy mirror when its named
source-of-truth host is offline."""

import ast
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return [namespace[name] for name in names]


(
    select_clone_fallback,
    _mirror_ms,
    repo_mirror_same_group,
) = _load(
    "select_clone_fallback",
    "_mirror_ms",
    "repo_mirror_same_group",
)

NOW = 1_000_000_000_000
STALE = 10 * 60 * 1000  # HOST_PRESENCE_STALE_MS


def _row(key, owner, name, *, root="root1", visibility="public", synced=NOW):
    return {
        "key_bi": key,
        "is_private": 1 if visibility == "private" else 0,
        "data": {
            "owner": owner,
            "name": name,
            "visibility": visibility,
            "rootCommit": root,
            "lastSync": str(synced),
        },
    }


def _pick(owner, repo, rows, presence, *, source_online=False):
    return select_clone_fallback(
        owner, repo, rows, presence, NOW, STALE, source_online)


def test_online_source_never_redirects():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kM", "mirror", "forkmesh"),
    ]
    presence = {"kS": NOW, "kM": NOW}
    assert _pick("source", "forkmesh", rows, presence, source_online=True) is None


def test_offline_source_redirects_to_online_mirror():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kM", "mirror", "forkmesh"),
    ]
    # Source presence is stale (offline), mirror is fresh (online).
    presence = {"kS": NOW - 2 * STALE, "kM": NOW}
    assert _pick("source", "forkmesh", rows, presence) == "mirror"


def test_no_redirect_when_every_mirror_is_offline():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kM", "mirror", "forkmesh"),
    ]
    presence = {"kS": NOW - 2 * STALE, "kM": NOW - 2 * STALE}
    assert _pick("source", "forkmesh", rows, presence) is None


def test_never_redirects_to_the_source_owner_itself():
    # The source is the only published node and is offline -> no fallback target.
    rows = [_row("kS", "source", "forkmesh")]
    presence = {"kS": NOW}  # even if presence looks fresh, owner==source is skipped
    assert _pick("source", "forkmesh", rows, presence) is None


def test_private_mirror_is_not_a_fallback_target():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kM", "mirror", "forkmesh", visibility="private"),
    ]
    presence = {"kS": NOW - 2 * STALE, "kM": NOW}
    assert _pick("source", "forkmesh", rows, presence) is None


def test_fork_with_different_root_is_not_grouped():
    rows = [
        _row("kS", "source", "forkmesh", root="rootA"),
        _row("kM", "mirror", "forkmesh", root="rootB"),  # genuine fork
    ]
    presence = {"kS": NOW - 2 * STALE, "kM": NOW}
    assert _pick("source", "forkmesh", rows, presence) is None


def test_freshest_synced_online_mirror_wins():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kA", "alpha", "forkmesh", synced=NOW - 5000),
        _row("kB", "bravo", "forkmesh", synced=NOW),  # freshest
    ]
    presence = {"kS": NOW - 2 * STALE, "kA": NOW, "kB": NOW}
    assert _pick("source", "forkmesh", rows, presence) == "bravo"


def test_name_fallback_when_source_never_published():
    # The source has no catalog record at all (and is offline); a name-matching
    # mirror that DID publish can still serve the clone.
    rows = [_row("kM", "mirror", "forkmesh", root="")]
    presence = {"kM": NOW}
    assert _pick("source", "forkmesh", rows, presence) == "mirror"
