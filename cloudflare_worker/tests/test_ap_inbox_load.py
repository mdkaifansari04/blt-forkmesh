#!/usr/bin/env python3
"""Inbox load-shedding + follow-Accept retry contracts (issue #416 round 3).

Two production symptoms remained after the read-side edge caching:

  * "Too many requests": every inbound activity — including the fediverse-wide
    Like/Announce/Update floods we acknowledge and drop, and Mastodon's
    broadcast account-deletes for actors we've never seen — ran the full
    signature dance: a signed GET back to the sender's server plus D1 traffic.
    The inbox now drops ignorable activities BEFORE verification and only
    issues the forget-remote D1 writes for actors it actually knows.

  * The Mastodon follow button spinning forever: the Accept for a Follow was
    delivered exactly once, synchronously, result ignored. Remote servers do
    not re-send a Follow we already 202'd, so one lost Accept left the follow
    pending permanently. A failed Accept delivery is now queued in ap_outbox
    for the cron drain's normal retry/backoff.

These load the real handlers out of src/entry.py (same pattern as
test_ap_edge_cache.py) with the real activitypub.py protocol module.

Run: python3 -m pytest cloudflare_worker/tests/test_ap_inbox_load.py
"""

import ast
import asyncio
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

spec = importlib.util.spec_from_file_location(
    "activitypub", ROOT / "src" / "activitypub.py")
ap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ap)


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


class FakeHeaders:
    def __init__(self, headers=None):
        self._headers = {k.lower(): v for k, v in (headers or {}).items()}

    def get(self, name):
        return self._headers.get((name or "").lower())


class FakeRequest:
    def __init__(self, url, body, method="POST", headers=None):
        self.url = url
        self.method = method
        self.headers = FakeHeaders(headers)
        self._body = body

    async def text(self):
        return self._body


# --- ap_inbox_handler load-shedding ------------------------------------------

def _inbox_env(log, known_remote=False):
    """Namespace for ap_inbox_handler with everything past the short-circuits
    exploding, so a test reaching the signature path fails loudly unless it
    expects to (signature_required has no crypto stubs here on purpose)."""

    async def ensure_schema(env):
        log.append("ensure_schema")

    async def _ap_enabled(env):
        return True

    async def _ap_domain_blocked(env, host):
        return False

    async def d1_first(env, sql, *args):
        log.append(("d1_first", sql))
        if known_remote and "ap_remote_actors" in sql:
            return {"actor_id": args[0]}
        return None

    async def _ap_forget_remote(env, actor_id):
        log.append(("forget", actor_id))

    async def blind_index(env, value):
        return "bi:" + value

    async def decrypt_row(env, value):
        return None

    return _load(
        "ap_inbox_handler", "_ap_note_repo_mention",
        "_ap_comment_by_remote_id", extra_globals={
        "json": json,
        "urlparse": urlparse,
        "method_name": lambda request: request.method,
        "json_response": _json_response,
        "ensure_schema": ensure_schema,
        "_ap_enabled": _ap_enabled,
        "_ap_disabled_response": lambda: FakeResponse(
            {"error": "not_found"}, status=404),
        "_ap_origin": lambda env, request=None: "https://forkmesh.com",
        "_ap_domain_blocked": _ap_domain_blocked,
        "_ap_forget_remote": _ap_forget_remote,
        "d1_first": d1_first,
        "blind_index": blind_index,
        "decrypt_row": decrypt_row,
        "ap": ap,
        "AP_MAX_INBOX_BYTES": 128 * 1024,
        "AP_DATE_SKEW_MS": 12 * 60 * 60 * 1000,
        "Date": SimpleNamespace(now=lambda: 1750000000000),
    })


def _post(activity, headers=None):
    return FakeRequest("https://forkmesh.com/ap/inbox", json.dumps(activity),
                       headers=headers)


def test_ignored_activity_types_drop_before_signature_verification():
    # No Signature header at all: reaching the verify path would 401, so a
    # 202 proves the Like was shed before the signature dance (and its
    # remote-actor fetch back to the sender's server).
    log = []
    ns = _inbox_env(log)
    for activity_type in ("Like", "Announce", "Update", "Accept", "Move"):
        resp = _run(ns["ap_inbox_handler"](None, _post({
            "type": activity_type,
            "actor": "https://mastodon.example/users/alice",
            "object": "https://forkmesh.com/ap/o/" + "a" * 32,
        })))
        assert resp.status == 202, activity_type
    assert not any(isinstance(e, tuple) and e[0] == "d1_first" for e in log)


def test_create_not_replying_to_local_object_drops_before_verification():
    log = []
    ns = _inbox_env(log)
    resp = _run(ns["ap_inbox_handler"](None, _post({
        "type": "Create",
        "actor": "https://mastodon.example/users/alice",
        "object": {
            "id": "https://mastodon.example/notes/1",
            "type": "Note",
            "content": "hello @forkmesh.forkmesh",
            "inReplyTo": "https://mastodon.example/notes/0",
        },
    })))
    assert resp.status == 202


def test_create_replying_to_local_object_still_requires_signature():
    log = []
    ns = _inbox_env(log)
    resp = _run(ns["ap_inbox_handler"](None, _post({
        "type": "Create",
        "actor": "https://mastodon.example/users/alice",
        "object": {
            "id": "https://mastodon.example/notes/1",
            "type": "Note",
            "content": "real reply",
            "inReplyTo": "https://forkmesh.com/ap/o/" + "a" * 32,
        },
    })))
    assert resp.status == 401
    assert resp.data["error"] == "signature_required"


def test_follow_still_requires_signature():
    log = []
    ns = _inbox_env(log)
    resp = _run(ns["ap_inbox_handler"](None, _post({
        "type": "Follow",
        "actor": "https://mastodon.example/users/alice",
        "object": "https://forkmesh.com/ap/repos/forkmesh/forkmesh",
    })))
    assert resp.status == 401
    assert resp.data["error"] == "signature_required"


def test_self_delete_of_unknown_actor_issues_no_writes():
    log = []
    ns = _inbox_env(log, known_remote=False)
    actor = "https://mastodon.example/users/deleted"
    resp = _run(ns["ap_inbox_handler"](None, _post({
        "type": "Delete", "actor": actor, "object": actor,
    })))
    assert resp.status == 202
    assert not any(e[0] == "forget" for e in log if isinstance(e, tuple))
    # Existence was checked in both edge tables (reads, not writes).
    reads = [e for e in log if isinstance(e, tuple) and e[0] == "d1_first"]
    assert any("ap_remote_actors" in sql for _, sql in reads)
    assert any("ap_followers" in sql for _, sql in reads)


def test_self_delete_of_known_actor_still_forgets():
    log = []
    ns = _inbox_env(log, known_remote=True)
    actor = "https://mastodon.example/users/known"
    resp = _run(ns["ap_inbox_handler"](None, _post({
        "type": "Delete", "actor": actor, "object": actor,
    })))
    assert resp.status == 202
    assert ("forget", actor) in log


# --- _ap_handle_follow: Accept delivery must survive a failure -----------------

def _follow_env(log, deliver_status):
    async def _ap_resolve_local_target(env, origin, url):
        return ("repo", "forkmesh.forkmesh", "forkmesh", "forkmesh/forkmesh")

    async def _ap_local_actor(env, kind, handle, create=False):
        return {"actorBi": "bi:" + handle, "privkey": "PRIV"}

    async def d1_first(env, sql, *args):
        return {"c": 1}

    async def d1_run(env, sql, *args):
        log.append(("d1_run", sql, args))

    async def _ap_deliver_body(env, actor_url, priv, inbox, body):
        log.append(("deliver", inbox, body))
        return deliver_status, 0

    async def encrypt_row(env, rec):
        return json.dumps(rec)

    async def enqueue_notification(env, *args, **kwargs):
        return None

    async def _best_effort_inbox_side_effect(coro):
        await coro

    def _ap_actor_url(origin, kind, handle):
        owner, _, repo = handle.partition(".")
        return origin + "/ap/repos/%s/%s" % (owner, repo)

    return _load("_ap_handle_follow", extra_globals={
        "json": json,
        "json_response": _json_response,
        "_ap_origin": lambda env, request=None: "https://forkmesh.com",
        "_ap_resolve_local_target": _ap_resolve_local_target,
        "_ap_local_actor": _ap_local_actor,
        "_ap_actor_url": _ap_actor_url,
        "_ap_deliver_body": _ap_deliver_body,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "encrypt_row": encrypt_row,
        "enqueue_notification": enqueue_notification,
        "_best_effort_inbox_side_effect": _best_effort_inbox_side_effect,
        "ap": ap,
        "AP_MAX_FOLLOWERS_PER_ACTOR": 5000,
        "Date": SimpleNamespace(now=lambda: 1750000000000),
    })


_FOLLOW_ACTIVITY = {
    "id": "https://mastodon.example/follows/1",
    "type": "Follow",
    "actor": "https://mastodon.example/users/alice",
    "object": "https://forkmesh.com/ap/repos/forkmesh/forkmesh",
}

_REMOTE = {
    "actor_id": "https://mastodon.example/users/alice",
    "inbox": "https://mastodon.example/users/alice/inbox",
    "shared_inbox": "https://mastodon.example/inbox",
    "handle": "alice@mastodon.example",
}


def _outbox_inserts(log):
    return [e for e in log if isinstance(e, tuple) and e[0] == "d1_run"
            and "INSERT INTO ap_outbox" in e[1]]


def test_accepted_follow_delivers_accept_and_queues_nothing():
    log = []
    ns = _follow_env(log, deliver_status=202)
    resp = _run(ns["_ap_handle_follow"](
        None, None, dict(_FOLLOW_ACTIVITY), dict(_REMOTE)))
    assert resp.status == 202
    delivered = [e for e in log if e[0] == "deliver"]
    assert len(delivered) == 1
    body = json.loads(delivered[0][2])
    assert body["type"] == "Accept"
    assert body["object"]["id"] == _FOLLOW_ACTIVITY["id"]
    assert _outbox_inserts(log) == []


def test_failed_accept_delivery_is_queued_for_cron_retry():
    log = []
    ns = _follow_env(log, deliver_status=0)  # fetch failed / network error
    resp = _run(ns["_ap_handle_follow"](
        None, None, dict(_FOLLOW_ACTIVITY), dict(_REMOTE)))
    assert resp.status == 202  # the follow itself is stored either way
    inserts = _outbox_inserts(log)
    assert len(inserts) == 1
    _, _, args = inserts[0]
    assert args[0] == _REMOTE["inbox"]  # Accept goes to the personal inbox
    queued = json.loads(args[1])
    assert queued["actorKind"] == "repo"
    assert queued["actorHandle"] == "forkmesh.forkmesh"
    assert json.loads(queued["body"])["type"] == "Accept"


def test_rate_limited_accept_delivery_is_queued_for_cron_retry():
    log = []
    ns = _follow_env(log, deliver_status=429)  # Mastodon throttled us
    _run(ns["_ap_handle_follow"](
        None, None, dict(_FOLLOW_ACTIVITY), dict(_REMOTE)))
    assert len(_outbox_inserts(log)) == 1


def test_gone_inbox_is_not_queued():
    log = []
    ns = _follow_env(log, deliver_status=410)
    _run(ns["_ap_handle_follow"](
        None, None, dict(_FOLLOW_ACTIVITY), dict(_REMOTE)))
    assert _outbox_inserts(log) == []
