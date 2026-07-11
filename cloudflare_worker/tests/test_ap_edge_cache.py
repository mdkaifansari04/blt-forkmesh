#!/usr/bin/env python3
"""Edge-cache contracts for the unauthenticated ActivityPub read endpoints.

Fediverse servers and crawlers hit webfinger/nodeinfo/actor/collection/object
GETs in bursts, and every hit used to reach D1 — the pressure behind the
"Too many requests" failures on the federated repo (issue #416, following
adhoc #9). These tests load the real handlers out of src/entry.py and pin
the collapse behaviour:

  * a cold hit computes the document, stores it under its canonical public
    URL, and a warm hit is served from the edge cache without touching D1;
  * error responses (404 for a non-federating actor) are never stored;
  * webfinger keys on the parsed acct resource, so different spellings of
    the same handle share one entry;
  * the Update(actor) broadcast drops the cached actor doc so follower
    refetches see the new profile.

Run: python3 -m pytest cloudflare_worker/tests/test_ap_edge_cache.py
"""

import ast
import asyncio
import json
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {n.name for n in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


class FakeResponse:
    def __init__(self, data, status=200, headers=None):
        self.data = data
        self.status = status
        self.headers = headers or {}


class FakeEdgeCache:
    """In-memory stand-in for the edge_cache_* helpers."""

    def __init__(self):
        self.store = {}
        self.puts = []

    async def match(self, key):
        return self.store.get(key)

    async def put(self, key, response):
        self.puts.append(key)
        self.store[key] = response

    async def delete(self, key):
        self.store.pop(key, None)


def _json_response(data, status=200, cache_seconds=None, cache_control=None,
                   extra_headers=None):
    return FakeResponse(data, status=status, headers=dict(extra_headers or {}))


def _base_globals(edge, d1_log):
    async def ensure_schema(env):
        d1_log.append("ensure_schema")

    async def _ap_enabled(env):
        return True

    async def d1_first(env, sql, *args):
        d1_log.append(sql)
        return {"c": 3}

    def _ap_actor_url(origin, kind, handle):
        if kind == "instance":
            return origin + "/ap/actor"
        if kind == "user":
            return origin + "/ap/users/" + handle
        owner, _, repo = handle.partition(".")
        return origin + "/ap/repos/%s/%s" % (owner, repo)

    return {
        "asyncio": asyncio,
        "json": json,
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "method_name": lambda request: request.method,
        "json_response": _json_response,
        "ensure_schema": ensure_schema,
        "_ap_enabled": _ap_enabled,
        "_ap_disabled_response": lambda: FakeResponse(
            {"error": "not_found"}, status=404),
        "_ap_origin": lambda env, request=None: "https://forkmesh.com",
        "_ap_domain_of": lambda origin: "forkmesh.com",
        "_ap_actor_url": _ap_actor_url,
        "d1_first": d1_first,
        "edge_cache_match": edge.match,
        "edge_cache_put": edge.put,
        "edge_cache_delete": edge.delete,
        "AP_ACTOR_INSTANCE": "instance",
        "AP_ACTOR_USER": "user",
        "AP_ACTOR_REPO": "repo",
        "AP_INSTANCE_HANDLE": "forkmesh",
    }


def _fake_request(url, method="GET"):
    return SimpleNamespace(url=url, method=method)


# --- Collections ---------------------------------------------------------------

def _collection_env(edge, d1_log, federates=True):
    globs = _base_globals(edge, d1_log)

    async def _ap_user_federates(env, handle):
        return federates

    async def _ap_repo_federates(env, owner, repo):
        return federates

    async def _ap_actor_bi(env, kind, handle):
        return "bi:" + handle

    globs.update({
        "_ap_user_federates": _ap_user_federates,
        "_ap_repo_federates": _ap_repo_federates,
        "_ap_actor_bi": _ap_actor_bi,
        "ap": SimpleNamespace(
            collection_doc=lambda url, total: {
                "id": url, "totalItems": total},
            ACTIVITY_CONTENT_TYPE="application/activity+json"),
    })
    return _load("ap_collection_handler", extra_globals=globs)


def test_collection_cold_hit_computes_and_stores_by_canonical_url():
    edge, d1_log = FakeEdgeCache(), []
    ns = _collection_env(edge, d1_log)
    resp = _run(ns["ap_collection_handler"](
        None, _fake_request("https://forkmesh.com/ap/users/alice/followers"),
        "user", "alice", "followers"))
    assert resp.status == 200
    assert resp.data["totalItems"] == 3
    assert edge.puts == ["https://forkmesh.com/ap/users/alice/followers"]
    assert any("COUNT(*)" in q for q in d1_log)


def test_collection_warm_hit_is_served_without_touching_d1():
    edge, d1_log = FakeEdgeCache(), []
    ns = _collection_env(edge, d1_log)
    request = _fake_request("https://forkmesh.com/ap/users/alice/outbox")
    first = _run(ns["ap_collection_handler"](
        None, request, "user", "alice", "outbox"))
    d1_log.clear()
    second = _run(ns["ap_collection_handler"](
        None, request, "user", "alice", "outbox"))
    assert second is first  # the stored response, straight from the cache
    assert d1_log == []  # no schema check, no COUNT(*)


def test_collection_not_found_is_not_cached():
    edge, d1_log = FakeEdgeCache(), []
    ns = _collection_env(edge, d1_log, federates=False)
    resp = _run(ns["ap_collection_handler"](
        None, _fake_request("https://forkmesh.com/ap/users/ghost/followers"),
        "user", "ghost", "followers"))
    assert resp.status == 404
    assert edge.store == {}


# --- Actor documents -------------------------------------------------------------

def _actor_env(edge, d1_log, resolves=True):
    globs = _base_globals(edge, d1_log)

    async def _ap_resolve_local_target(env, origin, url):
        return ("user", "alice", "alice", "@alice") if resolves else None

    async def _ap_local_actor(env, kind, handle, create=False):
        d1_log.append("local_actor")
        return {"pubkeyPem": "PEM", "privkey": "K", "actorBi": "bi"}

    async def _ap_build_actor_doc(env, origin, kind, handle, rec):
        return {"id": globs["_ap_actor_url"](origin, kind, handle),
                "type": "Person"}

    globs.update({
        "_ap_resolve_local_target": _ap_resolve_local_target,
        "_ap_local_actor": _ap_local_actor,
        "_ap_build_actor_doc": _ap_build_actor_doc,
        "ap": SimpleNamespace(
            ACTIVITY_CONTENT_TYPE="application/activity+json"),
    })
    return _load("_ap_actor_doc_response", extra_globals=globs)


def test_actor_doc_cold_hit_stores_under_actor_url_and_warm_hit_skips_d1():
    edge, d1_log = FakeEdgeCache(), []
    ns = _actor_env(edge, d1_log)
    request = _fake_request("https://forkmesh.com/ap/users/alice")
    first = _run(ns["_ap_actor_doc_response"](None, request, "user", "alice"))
    assert first.status == 200
    assert edge.puts == ["https://forkmesh.com/ap/users/alice"]
    d1_log.clear()
    second = _run(ns["_ap_actor_doc_response"](None, request, "user", "alice"))
    assert second is first
    assert d1_log == []


def test_actor_doc_404_is_not_cached():
    edge, d1_log = FakeEdgeCache(), []
    ns = _actor_env(edge, d1_log, resolves=False)
    resp = _run(ns["_ap_actor_doc_response"](
        None, _fake_request("https://forkmesh.com/ap/users/ghost"),
        "user", "ghost"))
    assert resp.status == 404
    assert edge.store == {}


# --- WebFinger --------------------------------------------------------------------

def _webfinger_env(edge, d1_log):
    globs = _base_globals(edge, d1_log)

    async def _ap_user_federates(env, name):
        d1_log.append("user_federates")
        return True

    async def _ap_repo_federates(env, owner, repo):
        return True

    def parse_acct_resource(raw):
        raw = (raw or "").strip().lower()
        if raw.startswith("acct:"):
            raw = raw[5:]
        if "@" not in raw:
            return (None, None)
        handle, _, domain = raw.partition("@")
        return (handle or None, domain or None)

    globs.update({
        "_ap_user_federates": _ap_user_federates,
        "_ap_repo_federates": _ap_repo_federates,
        "ap": SimpleNamespace(
            parse_acct_resource=parse_acct_resource,
            split_handle=lambda handle: ("user", handle),
            repo_handle=lambda owner, repo: owner + "." + repo,
            webfinger_doc=lambda acct, actor_url: {
                "subject": "acct:" + acct, "actor": actor_url},
            JRD_CONTENT_TYPE="application/jrd+json"),
    })
    return _load("ap_webfinger_handler", extra_globals=globs)


def test_webfinger_spelling_variants_share_one_edge_entry():
    edge, d1_log = FakeEdgeCache(), []
    ns = _webfinger_env(edge, d1_log)
    base = "https://forkmesh.com/.well-known/webfinger?resource="
    first = _run(ns["ap_webfinger_handler"](
        None, _fake_request(base + "acct%3Aalice%40forkmesh.com")))
    assert first.status == 200
    d1_log.clear()
    # Same resource, different spelling (no acct: prefix, upper case) — the
    # normalized key must hit the entry stored by the first request.
    second = _run(ns["ap_webfinger_handler"](
        None, _fake_request(base + "Alice%40ForkMesh.com")))
    assert second is first
    assert d1_log == []
    assert list(edge.store) == [
        "https://forkmesh.com/.well-known/webfinger"
        "?resource=acct:alice@forkmesh.com"]


def test_webfinger_invalid_resource_is_rejected_without_caching():
    edge, d1_log = FakeEdgeCache(), []
    ns = _webfinger_env(edge, d1_log)
    resp = _run(ns["ap_webfinger_handler"](
        None,
        _fake_request("https://forkmesh.com/.well-known/webfinger?resource=x")))
    assert resp.status == 400
    assert edge.store == {}
    assert d1_log == []


# --- Update broadcast invalidation --------------------------------------------------

def test_broadcast_actor_update_drops_cached_actor_doc():
    edge, d1_log = FakeEdgeCache(), []
    globs = _base_globals(edge, d1_log)
    actor_url = "https://forkmesh.com/ap/users/alice"
    edge.store[actor_url] = FakeResponse({"stale": True})

    async def _ap_actor_bi(env, kind, handle):
        return "bi"

    async def _ap_local_actor(env, kind, handle, create=False):
        return {"pubkeyPem": "PEM", "privkey": "K"}

    async def d1_all(env, sql, *args):
        return []  # no followers — bail after the invalidation

    async def _ap_build_actor_doc(env, origin, kind, handle, rec):
        return {"id": actor_url}

    globs.update({
        "_ap_actor_bi": _ap_actor_bi,
        "_ap_local_actor": _ap_local_actor,
        "_ap_build_actor_doc": _ap_build_actor_doc,
        "d1_all": d1_all,
    })
    ns = _load("_ap_broadcast_actor_update", extra_globals=globs)
    _run(ns["_ap_broadcast_actor_update"](
        None, _fake_request(actor_url), "user", "alice"))
    assert actor_url not in edge.store
