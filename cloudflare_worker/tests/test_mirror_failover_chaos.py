#!/usr/bin/env python3
"""Chaos test for mirror failover (issue #355).

The presence/failover chain was fixed piecemeal after real outages (presence
self-refresh, phantom tunnels, stale presence windows). This test locks the
behaviour in by breaking things on purpose: it drives the REAL decision helpers
from ``entry.py`` -- ``select_clone_fallback`` (clone diversion), the
``browse_mirror_candidates`` ordering, and ``clone_state_pins`` (source-attested
integrity) -- through a full source-down/​source-back lifecycle.

Those helpers are pure; the wiring that calls them lives on Durable-Object
classes that only run under the Workers JS runtime (that wiring is pinned by
source-inspection in ``test_clone_fallback.py``). Here a small ``Cluster`` model
reproduces exactly the wiring contract those tests assert -- ground-truth
liveness via ``_source_has_live_host`` (NOT the lagging ``host_presence`` row),
in-place ``_forward_to_node`` serving with no client-visible redirect, a
``clone_sticky`` pin so ``info/refs`` and the ``git-upload-pack`` POST reach the
same node, and the browse retry that skips a presence-lagging dead node -- so it
exercises the helpers the way production does.

The scenarios follow the issue verbatim:

1. Source online, repo published, mirror synced -> the source serves.
2. Source disconnects mid-presence-window -> browse AND a full two-request clone
   (``info/refs`` then the ``git-upload-pack`` POST) are served from the mirror,
   with integrity pins intact.
3. Source returns -> it reclaims serving.
4. No mirror exists -> the no-live-host error (not a hang).
"""

import ast
import re
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"
# Mirror grouping / clone-selection helpers were extracted from entry.py into
# mirrors.py; parse both sources so the AST loaders below still find them.
_WORKER_SRC = ENTRY.read_text(encoding="utf-8") + "\n" + MIRRORS.read_text(encoding="utf-8")
# Route regexes (e.g. GIT_PACK_RE) live in the extracted urls.py module now.
URLS = Path(__file__).resolve().parents[1] / "src" / "urls.py"


def _load(names, extra_globals=None):
    """Exec the named top-level defs/constants out of entry.py in isolation."""
    tree = ast.parse(_WORKER_SRC, filename=str(ENTRY))
    urls_tree = ast.parse(URLS.read_text(encoding="utf-8"), filename=str(URLS))
    wanted = set(names)
    selected = []
    for node in list(urls_tree.body) + list(tree.body):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name in wanted:
                selected.append(node)
        elif isinstance(node, ast.Assign):
            if any(getattr(t, "id", None) in wanted for t in node.targets):
                selected.append(node)
    found = {getattr(n, "name", None) for n in selected}
    found |= {t.id for n in selected if isinstance(n, ast.Assign)
              for t in n.targets if isinstance(t, ast.Name)}
    missing = wanted - found
    assert not missing, "missing from entry.py: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


_NS = _load(
    [
        "select_clone_fallback",
        "browse_mirror_candidates",
        "clone_state_pins",
        "repo_mirror_same_group",
        "_mirror_ms",
        "GIT_PACK_RE",
        "HOST_PRESENCE_STALE_MS",
        "CLONE_STICKY_MS",
    ],
    extra_globals={"re": re},
)

select_clone_fallback = _NS["select_clone_fallback"]
browse_mirror_candidates = _NS["browse_mirror_candidates"]
clone_state_pins = _NS["clone_state_pins"]
GIT_PACK_RE = _NS["GIT_PACK_RE"]
STALE = _NS["HOST_PRESENCE_STALE_MS"]
STICKY = _NS["CLONE_STICKY_MS"]

NOW = 1_700_000_000_000


class NoLiveHost(Exception):
    """What a browse/clone surfaces when nothing can serve -- an explicit error,
    never a hang against a dead host DO."""


class Served:
    def __init__(self, node, pins, state):
        self.node = node
        self.pins = pins
        self.state = state

    @property
    def pins_ok(self):
        # Unpinned (legacy) repos fail open; otherwise the serving node's
        # attested state must be one the source-of-truth group actually signed.
        return self.pins is None or self.state in self.pins

    def __repr__(self):
        return "Served(node=%r, pins_ok=%r)" % (self.node, self.pins_ok)


class Cluster:
    """A break-things-on-purpose model of the failover wiring.

    ``connected`` is ground truth (a live host WebSocket on the node's DO -- what
    ``_source_has_live_host`` probes). ``presence`` is the ``host_presence`` row,
    which can lag reality by up to ``HOST_PRESENCE_STALE_MS`` after an unclean
    tunnel death -- the exact window this test attacks.
    """

    def __init__(self):
        self.rows = []          # decrypted catalog records [{key_bi,is_private,data}]
        self.presence = {}      # key_bi -> host_presence ts
        self.connected = set()  # node names with a live host WS (ground truth)
        self.history = {}       # key_bi -> recent attested state hashes
        self.sticky = {}        # repo_bi -> {"node","ts"}  (clone_sticky table)
        self.rr = {}            # repo_bi -> round-robin cursor
        self.now = NOW

    # --- topology --------------------------------------------------------
    def publish(self, node, owner, repo, *, source="local-node", root="root1",
                state="s1", private=False, sync_offset=0):
        key = "bi:" + node
        rec = {
            "owner": owner, "name": repo,
            "visibility": "private" if private else "public",
            "rootCommit": root, "lastSync": str(self.now - sync_offset),
            "source": source, "stateHash": state,
        }
        self.rows = [r for r in self.rows if r["key_bi"] != key]
        self.rows.append(
            {"key_bi": key, "is_private": 1 if private else 0, "data": rec})

    def attest(self, node, *hashes):
        self.history.setdefault("bi:" + node, []).extend(hashes)

    def connect(self, node):
        # Host comes up: live host WS + touch_host_presence refreshes the row.
        self.connected.add(node)
        self.presence["bi:" + node] = self.now

    def disconnect(self, node, *, clean=False):
        # clean=True models _host_disconnected deleting the presence row the
        # instant the last socket closes. clean=False is the nastier case: the
        # tunnel dies uncleanly, the row lingers "fresh" for the staleness
        # window, and only the liveness probe (ground truth) knows the truth.
        self.connected.discard(node)
        if clean:
            self.presence.pop("bi:" + node, None)

    # --- ground-truth liveness (models _source_has_live_host) ------------
    def _live(self, node):
        return node in self.connected

    def _repo_bi(self, owner, repo):
        return "repo:%s/%s" % (owner.lower(), repo.lower())

    def _serve(self, node, owner, repo):
        # In-place serving (_forward_to_node): the SAME url is answered by the
        # chosen node with no redirect. Enforce the integrity gate the host DO
        # applies -- the serving node's attested state must be in the accepted
        # (source-attested) pin set.
        key = "bi:" + node
        target = next(r["data"] for r in self.rows if r["key_bi"] == key)
        pins = clone_state_pins(target, key, self.rows, self.history)
        state = str(target.get("stateHash") or "").strip().lower()
        return Served(node=node, pins=pins, state=state)

    # --- website browse --------------------------------------------------
    def browse(self, owner, repo):
        # browse_mirror_candidates lists every ONLINE node (source included),
        # freshest-synced first. The router forwards in place to the first, and
        # on a 503/504 (a presence-lagging node whose host is really dead)
        # retries the next, excluding the failed owner. Model that by walking the
        # ordered candidates and serving from the first whose host is truly live.
        cands = browse_mirror_candidates(
            owner, repo, self.rows, self.presence, self.now, STALE)
        for node in cands:
            if self._live(node):
                return self._serve(node, owner, repo)
        raise NoLiveHost("no live desktop host for %s/%s" % (owner, repo))

    # --- clone: the two-request handshake --------------------------------
    def clone_info_refs(self, owner, repo):
        # Leg 1 (GET info/refs). _git_host verifies the named source really has a
        # live host; if not it force-picks a mirror and pins the choice in
        # clone_sticky so leg 2 lands on the same node.
        repo_bi = self._repo_bi(owner, repo)
        if self._live(owner):
            self.sticky.pop(repo_bi, None)
            return self._serve(owner, owner, repo)
        cursor = self.rr.get(repo_bi, 0)
        pick = select_clone_fallback(
            owner, repo, self.rows, self.presence, self.now, STALE,
            False, cursor)  # source_online=False -> forced fallback
        # _sticky_clone_fallback re-checks the pick's ground-truth liveness.
        if pick is None or not self._live(pick):
            raise NoLiveHost("no live host or mirror for %s/%s" % (owner, repo))
        self.sticky[repo_bi] = {"node": pick, "ts": self.now}
        self.rr[repo_bi] = cursor + 1  # next fallback rotates
        return self._serve(pick, owner, repo)

    def clone_upload_pack(self, owner, repo, path):
        # Leg 2 (POST git-upload-pack). The path must be the real upload-pack
        # route, and the leg reuses the sticky pick regardless of age while its
        # host stays live, so the pack comes from the node that answered leg 1.
        m = GIT_PACK_RE.match(path)
        assert m, "not a git-upload-pack path: %r" % path
        assert (m.group(1), m.group(2)) == (owner, repo)
        repo_bi = self._repo_bi(owner, repo)
        pin = self.sticky.get(repo_bi)
        if pin and self._live(pin["node"]):
            return self._serve(pin["node"], owner, repo)
        if self._live(owner):
            return self._serve(owner, owner, repo)
        raise NoLiveHost("no live host or mirror for %s/%s" % (owner, repo))

    def clone(self, owner, repo):
        """A full two-request clone; asserts both legs hit the SAME node."""
        refs = self.clone_info_refs(owner, repo)
        pack = self.clone_upload_pack(
            owner, repo, "/%s/%s/git-upload-pack" % (owner, repo))
        assert refs.node == pack.node, (
            "clone split across nodes: info/refs=%s upload-pack=%s"
            % (refs.node, pack.node))
        return pack


def _base_cluster():
    # source/app published + hosted; two faithful mirrors syncing the same state.
    # The source syncs freshest so browse would list it first -- which makes the
    # failover retry skip a dead-but-fresh-presence source, not a convenience.
    c = Cluster()
    c.publish("source", "source", "app", state="s1", sync_offset=0)
    c.publish("mirra", "mirra", "app", source="remote-clone", state="s1",
              sync_offset=1000)
    c.publish("mirrb", "mirrb", "app", source="remote-clone", state="s1",
              sync_offset=1000)
    c.connect("source")
    c.connect("mirra")
    c.connect("mirrb")
    return c


# ---------------------------------------------------------------------------
# 1. Source online: the source serves both browse and clone.
# ---------------------------------------------------------------------------
def test_online_source_serves_everything():
    c = _base_cluster()
    assert c.browse("source", "app").node == "source"
    served = c.clone("source", "app")
    assert served.node == "source"
    assert served.pins_ok


# ---------------------------------------------------------------------------
# 2. Source disconnects mid-presence-window -> mirror takes over, pins intact.
# ---------------------------------------------------------------------------
def test_midwindow_disconnect_fails_over_to_mirror_with_pins_intact():
    c = _base_cluster()
    # Unclean death: host WS gone, but the presence row is still fresh (we are
    # WELL inside HOST_PRESENCE_STALE_MS). Only the liveness probe knows.
    c.disconnect("source", clean=False)
    assert c.presence["bi:source"] == c.now  # row still looks online
    assert c.now - c.presence["bi:source"] < STALE

    # Browse: the freshest-synced candidate IS the dead source; the router must
    # skip it (retry past the 503) and serve in place from a live mirror.
    top = browse_mirror_candidates(
        "source", "app", c.rows, c.presence, c.now, STALE)[0]
    assert top == "source"  # the presence row still ranks it first...
    browsed = c.browse("source", "app")
    assert browsed.node in ("mirra", "mirrb")  # ...but a live mirror served
    assert browsed.pins_ok

    # Clone: both legs served from the SAME mirror (sticky pin), pins intact.
    served = c.clone("source", "app")
    assert served.node in ("mirra", "mirrb")
    assert served.pins_ok
    assert served.state == "s1"  # the real, source-attested state


def test_clean_disconnect_expires_presence_then_fails_over():
    c = _base_cluster()
    c.disconnect("source", clean=True)  # _host_disconnected drops the row
    assert "bi:source" not in c.presence
    served = c.clone("source", "app")
    assert served.node in ("mirra", "mirrb")
    assert served.pins_ok


def test_two_request_clone_sticks_to_one_mirror_despite_rotation():
    # Stickiness is load-bearing: without it the round-robin cursor bumped by
    # leg 1 would send leg 2 to a DIFFERENT mirror and split the pack stream.
    c = _base_cluster()
    c.disconnect("source", clean=True)
    refs = c.clone_info_refs("source", "app")
    # A fresh pick with the now-advanced cursor would rotate to the other mirror.
    other = select_clone_fallback(
        "source", "app", c.rows, c.presence, c.now, STALE, False,
        c.rr[c._repo_bi("source", "app")])
    assert other != refs.node  # rotation genuinely moved on
    pack = c.clone_upload_pack(
        "source", "app", "/source/app/git-upload-pack")
    assert pack.node == refs.node  # ...yet the POST stuck to leg 1's node


# ---------------------------------------------------------------------------
# 3. Source returns -> it reclaims serving.
# ---------------------------------------------------------------------------
def test_source_returns_and_reclaims_serving():
    c = _base_cluster()
    c.disconnect("source", clean=False)
    assert c.clone("source", "app").node in ("mirra", "mirrb")  # mirror serving

    c.connect("source")  # host comes back, presence refreshed
    assert c.browse("source", "app").node == "source"
    served = c.clone("source", "app")
    assert served.node == "source"  # reclaimed -- no redirect to a mirror
    assert served.pins_ok
    # The stale sticky pin from the outage is discarded on reclaim.
    assert c.sticky.get(c._repo_bi("source", "app")) is None


# ---------------------------------------------------------------------------
# 4. No mirror exists -> a clean no-live-host error, never a hang.
# ---------------------------------------------------------------------------
def test_no_mirror_yields_no_live_host_error_not_a_hang():
    c = Cluster()
    c.publish("source", "source", "app", state="s1")
    c.connect("source")
    c.disconnect("source", clean=False)  # down, presence still fresh, no mirror

    # Browse: no live node -> explicit error (the "no live desktop host" page),
    # not a forward at a dead host DO that would hang / 1101.
    try:
        c.browse("source", "app")
        assert False, "expected NoLiveHost"
    except NoLiveHost:
        pass
    # Clone: same -- select_clone_fallback finds no candidate, so we surface the
    # no-host error instead of pinning and looping.
    assert select_clone_fallback(
        "source", "app", c.rows, c.presence, c.now, STALE, False, 0) is None
    try:
        c.clone("source", "app")
        assert False, "expected NoLiveHost"
    except NoLiveHost:
        pass


# ---------------------------------------------------------------------------
# Integrity chaos: a tampered mirror cannot serve a state the source never
# attested, even while it is the only node online.
# ---------------------------------------------------------------------------
def test_tampered_mirror_fails_the_source_attested_integrity_pin():
    c = _base_cluster()
    # The mirror self-publishes a forged state; the source attested "s1".
    c.publish("mirra", "mirra", "app", source="remote-clone", state="evil",
              sync_offset=1000)
    c.disconnect("source", clean=False)
    c.disconnect("mirrb", clean=True)  # leave the tampered mirror the only one up

    served = c.browse("source", "app")
    assert served.node == "mirra"
    assert served.state == "evil"
    assert served.pins == {"s1"}       # only the source-attested hash is accepted
    assert not served.pins_ok          # so the forged state is rejected


def test_lagging_mirror_still_clones_via_recent_pin_history():
    # An honest mirror a publish behind the source must still clone: the source's
    # recent attested history covers the state it is serving.
    c = _base_cluster()
    c.publish("source", "source", "app", state="s2", sync_offset=0)  # source moved on
    c.attest("source", "s1")  # ...but recently attested s1 too
    c.publish("mirra", "mirra", "app", source="remote-clone", state="s1",
              sync_offset=1000)  # mirror still on the previous state
    c.disconnect("source", clean=True)
    c.disconnect("mirrb", clean=True)

    served = c.clone("source", "app")
    assert served.node == "mirra"
    assert served.pins == {"s2", "s1"}
    assert served.pins_ok  # the lagging state is in-window, so the clone succeeds
