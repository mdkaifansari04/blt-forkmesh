#!/usr/bin/env python3
"""No-op profile/About saves must not touch the fediverse (issue #416 round 4).

The "profile update code" (About editor + account profile save) used to
broadcast Update(actor) to every follower on ANY field change — including
fields the actor document never surfaces (notification prefs, payout wallet)
and byte-identical re-saves. Every broadcast queued one delivery per follower
server and pulled a rel=me refetch storm back at the worker, burning the
free-plan request budget that, once exhausted, 429s every request (the
"Too many requests" Mastodon shows when viewing/following the repo actor).

These tests load the real handlers out of src/entry.py and pin:

  * repo About POST with unchanged description/website/media writes nothing,
    queues nothing for the desktop, and does not broadcast;
  * a real description change still does all of the above;
  * re-uploading identical branding bytes does not bump updated_at (which
    would rotate the ?v= buster and force every follower server to
    re-download the image) and does not broadcast;
  * the account-profile save only broadcasts when a federated field
    (bio / private flag) changed — source-level contract;
  * the outbox drain honours Retry-After from a throttling remote (429/503)
    as a floor under the normal backoff;
  * /.well-known/host-meta is answered without schema or D1 work and parks
    at the edge (Mastodon fetches it whenever webfinger fails — the
    self-amplifying half of the 429 loop).

Run: python3 -m pytest cloudflare_worker/tests/test_ap_noop_profile_updates.py
"""

import ast
import asyncio
import json
import re
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


def _json_response(data, status=200, cache_seconds=None, cache_control=None,
                   extra_headers=None):
    return FakeResponse(data, status=status, headers=dict(extra_headers or {}))


# --- repo About POST ---------------------------------------------------------

def _about_env(catalog_record, media_store, log):
    """Load the real repo_about_handler with an in-memory catalog of one
    matching record and a repo_media table backed by `media_store`."""

    async def ensure_schema(env):
        log.append("ensure_schema")

    async def _authed_account_name(env, request, data):
        return "alice"

    async def _account_owns_node(env, actor, owner):
        return True

    async def _is_admin(env, actor):
        return False

    async def _owner_pubkey(env, owner):
        return "PUB"

    async def d1_all(env, sql, *args):
        log.append(("d1_all", sql))
        if "FROM repositories" in sql:
            return [{"key_bi": "kb", "data": catalog_record, "is_private": 0}]
        if "FROM repo_media" in sql:
            return [{"kind": k} for k in media_store]
        return []

    async def d1_first(env, sql, *args):
        log.append(("d1_first", sql))
        if "FROM repo_media" in sql:
            kind = args[-1]
            if kind in media_store:
                return {"data": {"png": media_store[kind]}}
            return None
        return None

    async def d1_run(env, sql, *args):
        log.append(("d1_run", sql))

    async def decrypt_row(env, data):
        return dict(data) if isinstance(data, dict) else data

    async def encrypt_row(env, rec):
        return rec

    async def blind_index(env, value):
        return "bi:" + value

    async def notify_repo_host(env, owner, repo, topic):
        log.append(("notify", topic))

    async def purge_catalog_related_caches():
        log.append("purge_catalog")

    async def _ap_broadcast_actor_update(env, request, kind, handle):
        log.append(("broadcast", kind, handle))

    async def _best_effort_inbox_side_effect(coro):
        await coro

    def clean_string(value, cap):
        return str(value or "")[:cap]

    def clean_media_png(value, cap):
        return (str(value or ""), "")

    return _load("repo_about_handler", extra_globals={
        "json_response": _json_response,
        "method_name": lambda request: request.method,
        "ensure_schema": ensure_schema,
        "_authed_account_name": _authed_account_name,
        "_account_owns_node": _account_owns_node,
        "_is_admin": _is_admin,
        "_owner_pubkey": _owner_pubkey,
        "d1_all": d1_all,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "encrypt_row": encrypt_row,
        "blind_index": blind_index,
        "notify_repo_host": notify_repo_host,
        "purge_catalog_related_caches": purge_catalog_related_caches,
        "_ap_broadcast_actor_update": _ap_broadcast_actor_update,
        "_best_effort_inbox_side_effect": _best_effort_inbox_side_effect,
        "_catalog_record_matches_identity":
            lambda record, owner, repo: bool(record),
        "clean_string": clean_string,
        "clean_media_png": clean_media_png,
        "re": re,
        "MAX_REPO_LOGO_BYTES": 1 << 20,
        "MAX_REPO_BANNER_BYTES": 1 << 20,
        "AP_ACTOR_REPO": "repo",
        "ap": SimpleNamespace(
            repo_handle=lambda owner, repo: owner + "." + repo),
        "Date": SimpleNamespace(now=lambda: 1750000000000),
    })


def _post(payload):
    async def read_json():
        return payload
    return SimpleNamespace(method="POST", json=read_json)


def _writes(log):
    return [sql for op, sql in
            [e for e in log if isinstance(e, tuple) and e[0] == "d1_run"]]


def test_noop_about_save_writes_nothing_and_stays_off_the_fediverse():
    log = []
    ns = _about_env({"owner": "alice", "name": "repo",
                     "description": "same", "website": "https://x.dev"},
                    media_store={}, log=log)
    resp = _run(ns["repo_about_handler"](
        None, _post({"description": "same", "website": "https://x.dev"}),
        "alice", "repo"))
    assert resp.status == 200 and resp.data["ok"] is True
    assert _writes(log) == []
    assert "purge_catalog" not in log
    assert not any(isinstance(e, tuple) and e[0] in ("notify", "broadcast")
                   for e in log)


def test_changed_description_still_updates_queues_and_broadcasts():
    log = []
    ns = _about_env({"owner": "alice", "name": "repo",
                     "description": "old", "website": ""},
                    media_store={}, log=log)
    resp = _run(ns["repo_about_handler"](
        None, _post({"description": "new"}), "alice", "repo"))
    assert resp.status == 200
    writes = _writes(log)
    assert any(sql.startswith("UPDATE repositories") for sql in writes)
    assert any("INSERT INTO about_inbox" in sql for sql in writes)
    assert ("notify", "about") in log
    assert "purge_catalog" in log
    assert ("broadcast", "repo", "alice.repo") in log


def test_identical_logo_bytes_keep_updated_at_and_skip_broadcast():
    log = []
    ns = _about_env({"owner": "alice", "name": "repo",
                     "description": "same", "website": ""},
                    media_store={"logo": "PNGB64"}, log=log)
    resp = _run(ns["repo_about_handler"](
        None, _post({"description": "same", "logoPng": "PNGB64"}),
        "alice", "repo"))
    assert resp.status == 200
    assert not any("INSERT INTO repo_media" in sql for sql in _writes(log))
    assert not any(isinstance(e, tuple) and e[0] == "broadcast" for e in log)


def test_media_only_change_broadcasts_without_desktop_roundtrip():
    log = []
    ns = _about_env({"owner": "alice", "name": "repo",
                     "description": "same", "website": ""},
                    media_store={"logo": "OLD"}, log=log)
    resp = _run(ns["repo_about_handler"](
        None, _post({"description": "same", "logoPng": "NEW"}),
        "alice", "repo"))
    assert resp.status == 200
    writes = _writes(log)
    assert any("INSERT INTO repo_media" in sql for sql in writes)
    # The desktop only owns the textual About (info.json): a branding change
    # must not queue an about_inbox round-trip, but followers must hear it.
    assert not any("about_inbox" in sql for sql in writes)
    assert ("broadcast", "repo", "alice.repo") in log


# --- account profile save: broadcast gated on federated fields ---------------

def test_account_profile_broadcast_is_gated_on_bio_and_privacy():
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _account_profile"):
        ENTRY_TEXT.index("async def _account_claim_node")
    ]
    # The fingerprint is captured before the field mutations...
    fed_before = profile_body.index(
        'fed_before = (rec.get("profile_bio", ""),')
    assert fed_before < profile_body.index('if "solana" in data:')
    # ...and the broadcast only fires when it moved.
    assert 'if fed_after != fed_before:' in profile_body
    guard = profile_body.index('if fed_after != fed_before:')
    call = profile_body.index('_ap_broadcast_actor_update', guard)
    assert call - guard < 200  # the call sits inside the guard


# --- outbox drain: Retry-After from a throttling remote ----------------------

def test_deliver_body_parses_retry_after_only_when_throttled():
    async def _ap_signed_request(key_id, priv, method, url, body_str=None):
        return SimpleNamespace(status=429, headers={"retry-after": "600"})

    ns = _load("_ap_deliver_body", extra_globals={
        "_ap_signed_request": _ap_signed_request,
        "ap": SimpleNamespace(parse_http_date_ms=lambda raw: 0),
        "Date": SimpleNamespace(now=lambda: 1750000000000),
    })
    status, retry_ms = _run(ns["_ap_deliver_body"](
        None, "https://forkmesh.com/ap/users/a", "K",
        "https://mastodon.social/inbox", "{}"))
    assert (status, retry_ms) == (429, 600000)

    async def ok_request(key_id, priv, method, url, body_str=None):
        return SimpleNamespace(status=202, headers={"retry-after": "600"})

    ns["_ap_signed_request"] = ok_request
    status, retry_ms = _run(ns["_ap_deliver_body"](
        None, "https://forkmesh.com/ap/users/a", "K",
        "https://mastodon.social/inbox", "{}"))
    assert (status, retry_ms) == (202, 0)


def test_drain_reschedules_no_sooner_than_retry_after():
    updates = []
    now = 1750000000000

    async def _ap_enabled(env):
        return True

    async def d1_all(env, sql, *args):
        return [{"id": 7, "inbox": "https://mastodon.social/inbox",
                 "data": "blob", "attempts": 0}]

    async def d1_run(env, sql, *args):
        updates.append((sql, args))

    async def decrypt_row(env, data):
        return {"body": "{}", "actorKind": "repo", "actorHandle": "a.r",
                "actorUrl": "https://forkmesh.com/ap/repos/a/r"}

    async def _ap_local_actor(env, kind, handle, create=False):
        return {"privkey": "K"}

    async def _ap_domain_blocked(env, host):
        return False

    retry_after_ms = 3600 * 1000  # remote asked for an hour

    async def _ap_deliver_body(env, actor_url, priv, inbox, body):
        return 429, retry_after_ms

    ns = _load("_ap_drain_outbox", extra_globals={
        "_ap_enabled": _ap_enabled,
        "d1_all": d1_all,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "_ap_local_actor": _ap_local_actor,
        "_ap_domain_blocked": _ap_domain_blocked,
        "_ap_deliver_body": _ap_deliver_body,
        "urlparse": urlparse,
        "ap": SimpleNamespace(MAX_DELIVERY_ATTEMPTS=8,
                              retry_backoff_ms=lambda attempts: 300000),
        "Date": SimpleNamespace(now=lambda: now),
    })
    _run(ns["_ap_drain_outbox"](None, 5))
    assert len(updates) == 1
    sql, args = updates[0]
    assert sql.startswith("UPDATE ap_outbox")
    # next_ts honours the remote's Retry-After, not just our 5m backoff.
    assert args[1] == now + retry_after_ms


# --- host-meta: constant, schema-free, edge-parked ---------------------------

def test_hostmeta_is_schema_free_and_edge_cached():
    store, puts = {}, []

    async def edge_cache_match(key):
        return store.get(key)

    async def edge_cache_put(key, response):
        puts.append(key)
        store[key] = response

    class FakePageResponse:
        def __init__(self, body, status=200, headers=None):
            self.body = body
            self.status = status
            self.headers = headers or {}

    # Deliberately NO ensure_schema / d1_* in the namespace: if the handler
    # ever grows a schema or D1 dependency this test fails with NameError.
    ns = _load("ap_hostmeta_handler", extra_globals={
        "method_name": lambda request: request.method,
        "json_response": _json_response,
        "_ap_origin": lambda env, request=None: "https://forkmesh.com",
        "edge_cache_match": edge_cache_match,
        "edge_cache_put": edge_cache_put,
        "Response": FakePageResponse,
    })
    request = SimpleNamespace(method="GET",
                              url="https://forkmesh.com/.well-known/host-meta")
    first = _run(ns["ap_hostmeta_handler"](None, request))
    assert first.status == 200
    assert "xrd+xml" in first.headers["content-type"]
    assert ("template=\"https://forkmesh.com/.well-known/webfinger"
            "?resource={uri}\"" in first.body)
    assert puts == ["https://forkmesh.com/.well-known/host-meta"]
    second = _run(ns["ap_hostmeta_handler"](None, request))
    assert second is first


def test_hostmeta_route_is_registered():
    assert 'url.path == "/.well-known/host-meta"' in ENTRY_TEXT


# --- public pages: edge-cached rel=me targets ---------------------------------

def test_public_history_browse_is_edge_cached_keyed_on_state_hash():
    # The contribution graph's per-repo /history fetches carry
    # fmv=<attested state hash>; the host route parks public history
    # responses at the edge keyed on the full URL so an entry
    # self-invalidates when the repo moves (git_advert_cache pattern).
    route_body = ENTRY_TEXT[
        ENTRY_TEXT.index("public_browse = False"):
        ENTRY_TEXT.index("room = room_key_from_path(url.path)")
    ]
    assert 'host_match.group(3) == "history"' in route_body
    assert 'params.get("fmv", [""])[0]' in route_body
    assert "edge_cache_match_media(" in route_body
    assert "git_advert_cache_put(" in route_body


def test_guest_account_lookup_is_edge_cached_and_purged_on_actor_update():
    lookup_start = ENTRY_TEXT.index("match = ACCOUNTS_RE.match(url.path)")
    lookup_body = ENTRY_TEXT[lookup_start:lookup_start + 6000]
    assert "ACCOUNT_LOOKUP_CACHE_PREFIX" in lookup_body
    assert 'viewer_less = "viewer" not in' in lookup_body
    broadcast_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _ap_broadcast_actor_update"):
        ENTRY_TEXT.index("async def ap_collection_handler")
    ]
    assert "ACCOUNT_LOOKUP_CACHE_PREFIX" in broadcast_body


def test_profile_and_repo_pages_are_edge_cached_before_any_d1_work():
    profile_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _serve_profile_page"):
        ENTRY_TEXT.index("async def _serve_repo_page")
    ]
    repo_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _serve_repo_page"):
        ENTRY_TEXT.index("async def _serve_dashboard_asset")
    ]
    for body in (profile_body, repo_body):
        assert "edge_cache_match(cache_key)" in body
        assert "edge_cache_put(cache_key, page)" in body
    # The profile page checks the cache before ensure_schema/_account_row.
    assert (profile_body.index("edge_cache_match")
            < profile_body.index("ensure_schema"))
    # The repo page still checks the edge cache before any D1 work, even though
    # a cache miss now looks up the repo's logo for the OpenGraph card.
    assert (repo_body.index("edge_cache_match")
            < repo_body.index("ensure_schema"))


def test_repo_page_injects_opengraph_card_with_repo_logo():
    # A share of the repo URL (a federated "new pull request" post, a Slack
    # unfurl) must render the repo's logo instead of a blank document icon:
    # _serve_repo_page injects an og:image pointing at the owner-uploaded repo
    # logo, falling back to the ForkMesh mark at the site root.
    repo_body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _serve_repo_page"):
        ENTRY_TEXT.index("async def _serve_dashboard_asset")
    ]
    assert 'og_image = origin + "/assets/logo.png"' in repo_body
    assert "WHERE repo_bi=? AND kind='logo'" in repo_body
    assert "/api/repo/%s/%s/media/logo.png?v=%d" in repo_body
    assert '<meta property=\\"og:image\\" content=\\"%s\\">' in repo_body
    assert '<meta property=\\"og:title\\" content=\\"%s\\">' in repo_body
    assert '<meta name=\\"twitter:image\\" content=\\"%s\\">' in repo_body
