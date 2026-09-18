#!/usr/bin/env python3
"""Peer mirror-request feature contracts (issue #385).

A repo owner can ask another node to mirror their repo; the target's holder
gets a notification and, on accept, that node starts mirroring. These tests
cover the pure request-lifecycle helpers, the worker route/handler/notification
wiring, the heartbeat delivery + ack rail, and the dashboard UI wiring.
"""
import sys
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import mirrors  # noqa: E402

ENTRY = SRC / "entry.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n"
    + (SRC / "schema.py").read_text(encoding="utf-8"))
QT_HEARTBEAT = ROOT.parent / "desktop" / "src" / "MainWindowSetup.cpp"
QT_HEADER = ROOT.parent / "desktop" / "src" / "MainWindow.h"


# --- pure helpers -----------------------------------------------------------

def test_request_id_is_stable_and_scoped_to_repo_and_target():
    a = mirrors.mirror_request_id("Alice", "Repo", "Bob")
    b = mirrors.mirror_request_id("alice", "repo", "bob")
    assert a == b == "alice/repo@bob"
    assert mirrors.mirror_request_id("alice", "repo", "carol") != a


def test_add_mirror_request_dedupes_by_id_and_is_bounded():
    reqs = []
    entry = {"id": "alice/r@bob", "owner": "alice", "repo": "r",
             "status": "pending", "ts": 1}
    reqs = mirrors.add_mirror_request(reqs, entry)
    # Re-adding the same id replaces (newest-first) rather than duplicating.
    reqs = mirrors.add_mirror_request(
        reqs, {**entry, "ts": 2, "status": "pending"})
    assert len(reqs) == 1
    assert reqs[0]["ts"] == 2
    # Bounded to MAX_MIRROR_REQUESTS newest entries.
    for i in range(mirrors.MAX_MIRROR_REQUESTS + 10):
        reqs = mirrors.add_mirror_request(
            reqs, {"id": "alice/r%d@bob" % i, "owner": "alice",
                   "repo": "r%d" % i, "status": "pending", "ts": 100 + i})
    assert len(reqs) == mirrors.MAX_MIRROR_REQUESTS


def test_set_status_and_accepted_and_ack_roundtrip():
    reqs = mirrors.add_mirror_request(
        [], {"id": "alice/r@bob", "owner": "alice", "repo": "r",
             "status": "pending", "ts": 1})
    # Pending requests are NOT delivered to the node.
    assert mirrors.accepted_mirror_requests(reqs) == []
    reqs, updated = mirrors.set_mirror_request_status(
        reqs, "alice/r@bob", "accepted", ts=5)
    assert updated["status"] == "accepted" and updated["resolvedAt"] == 5
    delivered = mirrors.accepted_mirror_requests(reqs)
    assert delivered == [{"id": "alice/r@bob", "owner": "alice", "repo": "r"}]
    # Acking the delivered id drops it so it isn't redelivered.
    reqs = mirrors.ack_mirror_requests(reqs, ["alice/r@bob"])
    assert mirrors.accepted_mirror_requests(reqs) == []
    # A rejected request is never delivered and is untouched by acks.
    reqs2 = mirrors.add_mirror_request(
        [], {"id": "alice/r@carol", "owner": "alice", "repo": "r",
             "status": "pending", "ts": 1})
    reqs2, _ = mirrors.set_mirror_request_status(
        reqs2, "alice/r@carol", "rejected", ts=9)
    assert mirrors.accepted_mirror_requests(reqs2) == []
    assert mirrors.ack_mirror_requests(reqs2, ["alice/r@carol"]) == reqs2


# --- worker wiring ----------------------------------------------------------

def test_worker_exposes_mirror_requests_route_and_handler():
    assert 'url.path in ("/api/mirror-requests", "/api/mirror-requests/")' in ENTRY_TEXT
    assert "return await mirror_requests_handler(self.env, request)" in ENTRY_TEXT
    assert "async def mirror_requests_handler" in ENTRY_TEXT


def test_mirror_request_notification_kind_is_registered():
    # In the allowlist (else notification_payload coerces it away) and email map.
    assert '"mirror_request",\n})' in ENTRY_TEXT or '"mirror_request"' in ENTRY_TEXT
    assert '"mirror_request": True' in ENTRY_TEXT


def test_create_requires_owner_session_and_public_repo():
    # The asker must speak for the repo - proven by the session, never a body
    # field: its owner, an account whose fleet holds that node, or an
    # owner/admin of an org the repo is linked under. Public repos only.
    handler = ENTRY_TEXT.split("async def mirror_requests_handler", 1)[1]
    handler = handler.split("async def subscribe_handler", 1)[0]
    assert "_authed_account_name(env, request, data)" in handler
    assert "if (owner != actor" in handler
    assert "_account_owns_node(env, actor, owner)" in handler
    assert "_org_repo_mirror_allowed(" in handler
    assert "_repo_is_private(env, owner, repo)" in handler
    assert 'enqueue_notification(\n            env, target, "mirror_request"' in handler
    assert "add_mirror_request(" in handler
    # Accept/reject path resolves the parked request and flips its status.
    assert 'action in ("accept", "reject")' in handler
    assert "set_mirror_request_status(" in handler


def test_heartbeat_delivers_accepted_requests_and_consumes_acks():
    hb = ENTRY_TEXT.split("async def _account_heartbeat", 1)[1]
    hb = hb.split("async def ", 2)[0]
    assert "accepted_mirror_requests(rec.get(\"mirror_requests\"))" in hb
    assert 'response["mirrorRequests"] = pending_mirrors' in hb
    assert "ack_mirror_requests(rec.get(\"mirror_requests\"), ack_ids)" in hb
    assert 'data.get("mirrorRequestsAck")' in hb


# --- dashboard wiring -------------------------------------------------------

def test_dashboard_wires_ask_and_accept_reject_controls():
    js = assembled_dashboard_js()
    # Owner-only "ask a node to mirror" control + its POST.
    assert "data-mirror-request-send" in js
    assert "async function askNodeToMirror" in js
    assert "renderMirrorRequestForm" in js
    assert 'fetch("/api/mirror-requests"' in js
    # Accept/Reject on the incoming notification.
    assert "function mirrorRequestActionsHtml" in js
    assert "data-mirror-request-accept" in js
    assert "data-mirror-request-reject" in js
    assert "async function resolveMirrorRequest" in js
    # New notification kind gets an icon (else it falls back to the bell).
    assert "mirror_request:" in js


# --- desktop node wiring ----------------------------------------------------

def test_desktop_node_mirrors_accepted_requests_and_acks_them():
    cpp = QT_HEARTBEAT.read_text(encoding="utf-8")
    assert 'resp.value(QStringLiteral("mirrorRequests")).toArray()' in cpp
    assert "mirrorNetworkRepo(owner, repo, QString(), false)" in cpp
    assert 'body.insert(QStringLiteral("mirrorRequestsAck")' in cpp
    header = QT_HEADER.read_text(encoding="utf-8")
    assert "m_handledMirrorRequests" in header
    assert "m_pendingMirrorRequestAcks" in header


if __name__ == "__main__":
    for test in (
        test_request_id_is_stable_and_scoped_to_repo_and_target,
        test_add_mirror_request_dedupes_by_id_and_is_bounded,
        test_set_status_and_accepted_and_ack_roundtrip,
        test_worker_exposes_mirror_requests_route_and_handler,
        test_mirror_request_notification_kind_is_registered,
        test_create_requires_owner_session_and_public_repo,
        test_heartbeat_delivers_accepted_requests_and_consumes_acks,
        test_dashboard_wires_ask_and_accept_reject_controls,
        test_desktop_node_mirrors_accepted_requests_and_acks_them,
    ):
        test()
        print("PASS", test.__name__)


# --- Organization mirror rights ---------------------------------------------

import ast  # noqa: E402
import asyncio  # noqa: E402


def _load_entry(*names, extra_globals=None):
    # entry.py alone; ENTRY_TEXT above concatenates schema.py for the
    # source-contract assertions and is not independently parseable.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {n.name for n in selected}
    assert not set(names) - found, "missing: %s" % sorted(set(names) - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    ns = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


def _run_coro(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


def _mirror_allowed_ns(linked_orgs, roles):
    async def noop(*args, **kwargs):
        return None

    async def d1_all(env, sql, *params):
        assert "FROM org_repos" in sql
        return [{"org_bi": org} for org in linked_orgs]

    async def org_role(env, org_bi, actor):
        return roles.get((org_bi, actor), "")

    return _load_entry(
        "_org_repo_mirror_allowed",
        extra_globals={
            "ensure_schema": noop,
            "d1_all": d1_all,
            "_org_role": org_role,
        },
    )["_org_repo_mirror_allowed"]


def test_org_admins_may_mirror_org_linked_repos_they_do_not_own():
    # An org owner/admin acts for repos the org fronts, even though the repo
    # lives in another account's node namespace.
    allowed = _mirror_allowed_ns(
        ["org-a"], {("org-a", "admin-user"): "admin"})
    assert _run_coro(allowed(None, "admin-user", "kaif-blt-04", "blt")) is True

    owner_ok = _mirror_allowed_ns(
        ["org-a"], {("org-a", "boss"): "owner"})
    assert _run_coro(owner_ok(None, "boss", "kaif-blt-04", "blt")) is True


def test_plain_members_and_outsiders_may_not_act_for_the_org():
    member = _mirror_allowed_ns(["org-a"], {("org-a", "alice"): "member"})
    assert _run_coro(member(None, "alice", "kaif-blt-04", "blt")) is False

    outsider = _mirror_allowed_ns(["org-a"], {})
    assert _run_coro(outsider(None, "mallory", "kaif-blt-04", "blt")) is False

    unlinked = _mirror_allowed_ns([], {("org-a", "boss"): "owner"})
    assert _run_coro(unlinked(None, "boss", "kaif-blt-04", "blt")) is False


def test_org_mirror_authorization_fails_closed_and_rejects_blanks():
    async def boom(env, sql, *params):
        raise RuntimeError("d1 down")

    async def noop(*args, **kwargs):
        return None

    ns = _load_entry(
        "_org_repo_mirror_allowed",
        extra_globals={
            "ensure_schema": noop,
            "d1_all": boom,
            "_org_role": noop,
        },
    )["_org_repo_mirror_allowed"]
    assert _run_coro(ns(None, "boss", "kaif-blt-04", "blt")) is False
    assert _run_coro(ns(None, "", "kaif-blt-04", "blt")) is False
    assert _run_coro(ns(None, "boss", "", "blt")) is False
    assert _run_coro(ns(None, "boss", "kaif-blt-04", "")) is False


def test_member_self_serve_mirror_targets_the_canonical_node_namespace():
    # A member mirroring an org repo must park an entry whose `owner` is the
    # hosting NODE, never the org alias: the desktop clones /<owner>/<repo>
    # and catalog publishes verify against that account's key.
    handler = ENTRY_TEXT.split("async def mirror_requests_handler", 1)[1]
    handler = handler.split("async def subscribe_handler", 1)[0]
    mirror_branch = handler.split('action == "mirror"', 1)[1]
    assert "_org_repo_node_strict(" in mirror_branch
    assert "_org_repo_node(" not in mirror_branch.split(
        "_org_repo_node_strict(")[0]
    assert "MAX_MIRROR_REQUESTS" in mirror_branch


def test_dashboard_wires_the_org_member_mirror_optin():
    # Members get a one-click opt-in on an org-aliased repo URL; it posts the
    # mirror action with the org from the path and the member's own node.
    js = assembled_dashboard_js()
    assert "data-org-mirror-optin-send" in js
    assert "orgAliasFromPath(" in js
    assert 'action: "mirror"' in js
    # Hidden for the repo's own owner and for signed-out visitors.
    assert "!isRepoOwner(repo)" in js
