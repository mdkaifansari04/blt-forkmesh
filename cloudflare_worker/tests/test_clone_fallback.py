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
    browse_mirror_candidates,
    _mirror_ms,
    repo_mirror_same_group,
) = _load(
    "select_clone_fallback",
    "browse_mirror_candidates",
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


def _pick(owner, repo, rows, presence, *, source_online=False, rotate=0):
    return select_clone_fallback(
        owner, repo, rows, presence, NOW, STALE, source_online, rotate)


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


def test_round_robin_spreads_clones_across_online_mirrors():
    # Three equally-fresh online mirrors of the same repo. Advancing the rotation
    # cursor walks through every mirror in turn (sorted order alpha/bravo/charlie),
    # so clone traffic spreads across all of them instead of one.
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kA", "alpha", "forkmesh"),
        _row("kB", "bravo", "forkmesh"),
        _row("kC", "charlie", "forkmesh"),
    ]
    presence = {"kS": NOW - 2 * STALE, "kA": NOW, "kB": NOW, "kC": NOW}
    order = ["alpha", "bravo", "charlie"]
    picks = [
        _pick("source", "forkmesh", rows, presence, rotate=i) for i in range(6)
    ]
    # rotate=0 -> first, then it cycles, wrapping cleanly past the candidate count.
    assert picks == order + order
    assert set(picks) == set(order)  # every mirror gets served


def test_round_robin_wraps_modulo_candidate_count():
    # A large cursor still lands on a valid mirror (modulo the candidate count),
    # so the counter can grow unbounded without ever indexing out of range.
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kA", "alpha", "forkmesh"),
        _row("kB", "bravo", "forkmesh"),
    ]
    presence = {"kS": NOW - 2 * STALE, "kA": NOW, "kB": NOW}
    assert _pick("source", "forkmesh", rows, presence, rotate=1001) == "bravo"


def _browse(owner, repo, rows, presence):
    return browse_mirror_candidates(owner, repo, rows, presence, NOW, STALE)


def test_browse_lists_the_online_source_itself():
    # Website browse rotates across EVERY live mirror, the named source included,
    # so a lone online source is a valid (single) serving node.
    rows = [_row("kS", "source", "forkmesh")]
    assert _browse("source", "forkmesh", rows, {"kS": NOW}) == ["source"]


def test_browse_orders_source_and_mirrors_freshest_first():
    rows = [
        _row("kS", "source", "forkmesh", synced=NOW - 5000),
        _row("kM", "mirror", "forkmesh", synced=NOW),  # freshest
    ]
    presence = {"kS": NOW, "kM": NOW}
    # Both online; freshest-synced leads so the round-robin order is stable.
    assert _browse("source", "forkmesh", rows, presence) == ["mirror", "source"]


def test_browse_skips_offline_nodes():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kM", "mirror", "forkmesh"),
    ]
    presence = {"kS": NOW, "kM": NOW - 2 * STALE}  # mirror is stale/offline
    assert _browse("source", "forkmesh", rows, presence) == ["source"]


def test_browse_excludes_private_and_forked_mirrors():
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kP", "priv", "forkmesh", visibility="private"),
        _row("kF", "fork", "forkmesh", root="rootB"),  # rewritten history
    ]
    presence = {"kS": NOW, "kP": NOW, "kF": NOW}
    assert _browse("source", "forkmesh", rows, presence) == ["source"]


def test_browse_empty_when_nothing_online():
    rows = [_row("kS", "source", "forkmesh")]
    assert _browse("source", "forkmesh", rows, {"kS": NOW - 2 * STALE}) == []


def test_browse_round_robin_walks_every_online_mirror():
    # The router picks candidates[rotate % len]; walking rotate cycles through the
    # whole ordered list so consecutive page loads hit a different node each time.
    rows = [
        _row("kS", "source", "forkmesh"),
        _row("kA", "alpha", "forkmesh"),
        _row("kB", "bravo", "forkmesh"),
    ]
    presence = {"kS": NOW, "kA": NOW, "kB": NOW}
    cands = _browse("source", "forkmesh", rows, presence)
    assert sorted(cands) == ["alpha", "bravo", "source"]
    picks = [cands[i % len(cands)] for i in range(6)]
    assert set(picks) == set(cands)  # every online node gets served


def _route_source():
    # The browse fallback lives in the per-repo router (_route), a Durable Object
    # method that can't be exec'd in isolation like the pure helpers above. Lock
    # the wiring in via source inspection so the redirect can't silently regress.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "_route":
            return ast.unparse(node)
    raise AssertionError("_route not found in entry.py")


def test_browse_route_round_robins_across_online_mirrors():
    # A tree/blob/commits browse of a public repo must round-robin across every
    # online mirror (the source included) via _select_browse_mirror and 302 to the
    # chosen node when it isn't the named owner. The `fmserved` pin makes that
    # redirect target serve in place instead of re-rotating (which would loop).
    src = _route_source()
    browse = src.split("REPO_HOST_RE")[-1]
    assert "_select_browse_mirror(owner, repo)" in browse
    assert "/api/repo/%s/%s/%s" in browse
    assert "status=302" in browse
    assert "fmserved" in browse


def _method_source(class_name, method_name):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == method_name):
                    return ast.unparse(item)
    raise AssertionError(
        "%s.%s not found in entry.py" % (class_name, method_name))


def test_browse_route_retries_a_failed_host_on_a_live_mirror():
    # A downed source's host_presence row can lag reality for up to
    # HOST_PRESENCE_STALE_MS, so the rotation may still route a browse at a node
    # whose host DO answers no_host (503) or times out (504). The router must
    # then re-rotate once to an online mirror of the same logical repo —
    # excluding the node that just failed — instead of surfacing the error while
    # a live mirror sits unused. `fmretry` caps the redirect at one extra hop.
    src = _route_source()
    browse = src.split("REPO_HOST_RE")[-1]
    assert "public_browse" in browse
    assert "(503, 504)" in browse
    assert "exclude=owner" in browse
    assert "already_retried" in browse
    # The retry pin replaces (never appends to) the failed hop's fmserved, or
    # the retried route would read the stale pin first and re-rotate.
    assert "('fmserved', fallback)" in browse
    assert "('fmretry', '1')" in browse
    assert "not in ('fmserved', 'fmretry')" in browse


def test_select_browse_mirror_drops_the_excluded_node():
    # The retry path passes exclude=<failed owner>; _select_browse_mirror must
    # filter that node out of the candidate rotation or the retry could 302
    # straight back to the dead node it just came from.
    # _select_browse_mirror lives on the worker entrypoint class, whose name we
    # don't want to hard-code; find it by scanning every class.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    src = None
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef):
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == "_select_browse_mirror"):
                    src = ast.unparse(item)
    assert src, "_select_browse_mirror not found in entry.py"
    assert "exclude=None" in src
    assert "c.lower() != exclude.lower()" in src


def test_host_disconnect_expires_presence_immediately():
    # When the LAST host socket closes, the DO must delete the repo's
    # host_presence row right away (plus the deduped offline notification) so
    # browse/clone fallback flips to a live mirror immediately instead of after
    # the 10-minute staleness window — the source going down is exactly when the
    # mirrors must take over.
    src = _method_source("ForkMeshHost", "_host_disconnected")
    assert "DELETE FROM host_presence" in src
    assert "notify_host_status" in src
    assert "host_offline" in src
    assert "_live_host_count" in src
    for handler in ("webSocketClose", "webSocketError"):
        assert "_host_disconnected" in _method_source("ForkMeshHost", handler)


def _worker_method_source(name):
    # _git_host / _source_has_live_host live on the WorkerEntrypoint class, whose
    # name we don't hard-code; scan every class for the method.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef):
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == name):
                    return ast.unparse(item)
    raise AssertionError("%s not found in entry.py" % name)


def test_clone_falls_back_when_source_has_no_live_host_despite_fresh_presence():
    # The presence-based redirect only fires after the source's host_presence row
    # ages out (10 min). A tunnel that dies uncleanly looks "online" that whole
    # window, so _git_host must ALSO verify a host is really connected
    # (_source_has_live_host) and, when it isn't, force a redirect to a live
    # mirror regardless of the stale presence row — otherwise the clone hits the
    # dead host DO and dies with "no host serving" (or an isolate crash).
    src = _worker_method_source("_git_host")
    assert "_source_has_live_host(owner, repo)" in src
    assert "force=True" in src
    # The forced fallback still routes via a 302 to the mirror's info/refs.
    assert "status=302" in src
    # One-hop cap: two stale-presence, no-live-host peers must not 302 forever.
    assert "fmretry" in src
    assert "already_forced" in src


def test_source_has_live_host_probes_the_host_do_not_presence():
    # Ground-truth liveness = the DO's connected host count, not the lagging
    # host_presence row. One subrequest to the non-WebSocket /host endpoint;
    # any failure counts as "not live" so a flapping/dead source never strands
    # the clone.
    src = _worker_method_source("_source_has_live_host")
    assert "/host" in src
    assert '"hosts"' in src or "'hosts'" in src
    assert "return False" in src  # fail-closed on any error/non-200


def test_select_clone_fallback_force_overrides_stale_presence():
    # The DO wrapper must thread `force` into the source_online computation so a
    # forced call (source confirmed not serving) ignores a still-fresh presence
    # row and actually returns a mirror.
    src = _worker_method_source("_select_clone_fallback")
    assert "force=False" in src
    assert "not force" in src  # source_online is ANDed with `not force`
