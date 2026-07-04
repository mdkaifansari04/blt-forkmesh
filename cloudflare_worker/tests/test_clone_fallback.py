#!/usr/bin/env python3
"""Clone-fallback selection: serve a repo from a healthy mirror when its named
source-of-truth host is offline."""

import ast
import asyncio
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"
# Mirror grouping / clone-selection helpers were extracted from entry.py into
# mirrors.py; parse both sources so the AST loaders below still find them.
_WORKER_SRC = ENTRY.read_text(encoding="utf-8") + "\n" + MIRRORS.read_text(encoding="utf-8")


def _load(*names, extra_globals=None):
    tree = ast.parse(_WORKER_SRC, filename=str(ENTRY))
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
    # online mirror (the source included) via _select_browse_mirror, and the
    # chosen node serves IN PLACE through the requested URL: the request is
    # dispatched to its host DO with the path rewritten (_forward_to_node),
    # never via a client-visible redirect.
    src = _route_source()
    browse = src.split("REPO_HOST_RE")[-1]
    assert "_select_browse_mirror(owner, repo)" in browse
    assert "_forward_to_node" in browse
    assert "/api/repo/%s/%s/%s" in browse
    assert "status=302" not in browse  # same-URL serving, no redirects


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


def _compile_method(class_name, method_name, extra_globals):
    # Extract one method off a DO class and compile it standalone so it can run
    # against a fake `self` — the class itself needs the Workers JS runtime.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == method_name):
                    module = ast.fix_missing_locations(
                        ast.Module(body=[item], type_ignores=[]))
                    namespace = dict(extra_globals)
                    exec(compile(module, str(ENTRY), "exec"), namespace)
                    return namespace[method_name]
    raise AssertionError(
        "%s.%s not found in entry.py" % (class_name, method_name))


def test_browse_route_retries_a_failed_host_on_a_live_mirror():
    # A downed source's host_presence row can lag reality for up to
    # HOST_PRESENCE_STALE_MS, so the rotation may still route a browse at a node
    # whose host DO answers no_host (503), times out (504), or answers but can't
    # build the reply (502 — e.g. a freshly-added mirror whose clone is
    # empty/still syncing). The router must then serve once more in place from
    # an online mirror of the same logical repo — excluding the node that just
    # failed — instead of surfacing the error while a live mirror sits unused.
    src = _route_source()
    browse = src.split("REPO_HOST_RE")[-1]
    assert "public_browse" in browse
    assert "(0, 502, 503, 504)" in browse  # rotated pick failed/errored -> named owner
    assert "(502, 503, 504)" in browse     # named owner failed -> remaining mirrors
    # The retry must skip BOTH the named owner and a mirror that already failed
    # this request's first hop, so it can't re-pick the flapping node and
    # surface its error while other live mirrors sit unused.
    assert "exclude=[owner, failed_mirror]" in browse
    assert "failed_mirror = served" in browse
    assert "_forward_to_node" in browse


def test_select_browse_mirror_drops_the_excluded_node():
    # The retry path passes exclude=[<failed owner>, <failed mirror>];
    # _select_browse_mirror must filter every named node out of the candidate
    # rotation or the retry could route straight back to a node it just came
    # from. `exclude` accepts a single name or an iterable of names.
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
    # A string exclude is normalised to a one-element list, an iterable is used
    # as-is, and every entry is dropped from the candidate rotation.
    assert "isinstance(exclude, str)" in src
    assert "c.lower() not in excluded" in src


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


def test_browse_traffic_never_refreshes_presence_without_a_connected_host():
    # The adhoc #68 outage: browse/clone traffic on a repo whose desktop host
    # had gone away kept refreshing host_presence, so the source never aged out
    # and the fallback refused to serve from a live mirror. _mark_present must
    # bail before writing whenever no host WebSocket is connected to this DO.
    touched = []

    class FakeDate:
        @staticmethod
        def now():
            return NOW

    async def fake_touch(env, repo_bi):
        touched.append(repo_bi)

    mark_present = _compile_method(
        "ForkMeshHost", "_mark_present",
        {"Date": FakeDate,
         "touch_host_presence": fake_touch,
         "HOST_PRESENCE_REFRESH_MS": 30_000})

    class Host:
        def __init__(self, hosts):
            self._hosts = hosts
            self._last_presence = 0
            self._repo_bi = "bi-repo"
            self.env = object()

        def _host_count(self):
            return self._hosts

    # No connected host: the request must not write presence (or advance the
    # throttle clock) — letting the row lapse is what flips traffic to mirrors.
    ghost = Host(0)
    asyncio.run(mark_present(ghost))
    assert touched == []
    assert ghost._last_presence == 0

    # A live host does refresh presence...
    live = Host(1)
    asyncio.run(mark_present(live))
    assert touched == ["bi-repo"]
    # ...throttled: a second hit inside the refresh window writes nothing new.
    asyncio.run(mark_present(live))
    assert touched == ["bi-repo"]

    # A blocked phantom identity never marks itself live.
    blocked = Host(1)
    blocked._blocked_presence = True
    asyncio.run(mark_present(blocked))
    assert touched == ["bi-repo"]


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
    # host_presence lags an unclean tunnel death by up to 10 minutes, so
    # _git_host must verify a host is really connected (_source_has_live_host)
    # and, when it isn't, serve the clone from a live mirror — otherwise the
    # clone hits the dead host DO and dies with "no host serving" (or an
    # isolate crash). The mirror serves IN PLACE through the same URL
    # (_forward_to_node), never via a redirect, and the pick is sticky
    # (_sticky_clone_fallback) so info/refs and the upload-pack POST reach the
    # same node.
    src = _worker_method_source("_git_host")
    assert "_source_has_live_host(owner, repo)" in src
    assert "_sticky_clone_fallback" in src
    assert "_forward_to_node" in src
    assert "git-upload-pack" in src
    assert "status=302" not in src  # same-URL serving, no redirects


def test_clone_falls_back_when_a_live_source_stalls_info_refs():
    # A connected-but-stalled host answers info/refs with a 504 (GIT_TIMEOUT_MS
    # elapses in its host DO). The upfront liveness check sees the live socket
    # and never fails over, so without a post-fetch retry the clone dead-ends on
    # exactly the reported "504 on /owner/repo/info/refs". _git_host must inspect
    # the response status and, on 503/504 for the (idempotent, bodyless) info/refs
    # GET, serve the advertisement from a live mirror and pin it so the paired
    # upload-pack POST follows the same node.
    src = _worker_method_source("_git_host")
    assert "response = await host_object.fetch(request)" in src
    assert "response.status" in src
    assert "(503, 504)" in src
    assert "is_info" in src
    assert "_fresh_clone_pin" in src  # POST follows the mirror info/refs pinned
    assert "return response" in src   # normal path still returns the source's reply


def test_fresh_clone_pin_is_read_only_and_live_checked():
    # The upload-pack POST follows whatever info/refs pinned even when the named
    # source's tunnel is back up. The lookup must never select/rotate/write a pin
    # (that's info/refs' job) — a plain fresh, non-owner, still-live pin or None.
    src = _worker_method_source("_fresh_clone_pin")
    assert "clone_sticky" in src
    assert "CLONE_STICKY_MS" in src
    assert "_source_has_live_host(pick, repo)" in src
    assert "ON CONFLICT" not in src and "INSERT" not in src  # read-only
    assert "!= owner.lower()" in src or "!= owner" in src


def test_sticky_clone_pick_is_pinned_and_live_checked():
    # The sticky pick: reuse a stored mirror while it's fresh (or on the POST leg
    # regardless of age), verify it still has a live host before trusting it, and
    # only re-pick/rotate (force=True past the stale presence row) on info/refs.
    src = _worker_method_source("_sticky_clone_fallback")
    assert "clone_sticky" in src
    assert "CLONE_STICKY_MS" in src
    assert "_source_has_live_host(pick, repo)" in src
    assert "force=True" in src
    assert "ON CONFLICT(repo_bi)" in src


def test_forward_to_node_serves_through_the_original_url():
    # In-place serving: the request is re-dispatched to the chosen node's host
    # DO with the path rewritten into its namespace — the client never sees a
    # redirect. The forwarded request is rebuilt from PRIMITIVES: a bare URL
    # string for GETs, url + a plain init dict (method/headers/body bytes) for
    # the upload-pack POST. It must NEVER be constructed around the incoming
    # Python-wrapped request object — JsRequest.new(target, request) crashed
    # the isolate (Cloudflare error 1101) on every forwarded browse/clone.
    src = _worker_method_source("_forward_to_node")
    assert "host:{node}/{repo}" in src
    assert "url.query" in src
    assert "host_object.fetch(target)" in src        # GET: bare URL string
    assert "JsRequest.new(target, request)" not in src
    assert "to_js" in src and "'body'" in src        # POST: primitive init dict
    assert "content-encoding" in src                 # DO decodes the pack body


def test_forward_failures_degrade_instead_of_erroring():
    # Every forward call site is guarded: a mirror hop that throws (or answers
    # 503/504 on the rotation leg) falls back to the named owner's own route —
    # a broken forward must degrade to the old behaviour, never 500 the page
    # or the clone.
    route = _route_source().split("REPO_HOST_RE")[-1]
    git_host = _worker_method_source("_git_host")
    for src in (route, git_host):
        assert "_forward_to_node" in src
        assert "except Exception" in src


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
