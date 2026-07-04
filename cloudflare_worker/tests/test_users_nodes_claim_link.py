#!/usr/bin/env python3
"""Users-vs-nodes claim + installer link-code flows (adhoc #53).

Two link paths are covered end to end against an in-memory store:
 - website claim: claim-node parks a confirmation code on the node's record,
   the node's signed heartbeat reply carries it, claim-confirm links the two;
 - installer link code: the fresh node's registration and the installing
   desktop's signed offer rendezvous in link_codes, in either order.
"""

import ast
import asyncio
import re
from pathlib import Path

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
QT_SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
QT_CHAT = ROOT / "qt_client" / "src" / "MainWindowChat.cpp"
INSTALL_SH = ROOT / "cloudflare_worker" / "public" / "install.sh"

FUNCS = {
    "_account_kind", "_owned_nodes", "_generate_confirm_code", "_claim_pending",
    "_resolve_user_by_password", "_link_node_to_user", "_account_claim_node",
    "_account_claim_confirm", "_redeem_or_park_link_code", "_account_link_node",
    "_account_link_self", "_account_link_grant", "_account_heartbeat",
    "_resolve_claimable_node", "_account_row_by_pubkey", "valid_node_pubkey",
    "_transfer_pending", "_admin_authorized", "_admin_request_ownership",
    "_account_ownership_transfer_confirm", "_park_ownership_transfer",
}


def _load_functions(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCS
    ]
    assert {node.name for node in selected} == FUNCS, "missing claim/link functions"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Request:
    def __init__(self, body):
        self._body = body

    async def json(self):
        return self._body


NODE_NAME_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
NODE_PUBKEY_RE = re.compile(r"^[A-Za-z0-9_-]{43}$")


def _harness(accounts):
    """In-memory accounts (name -> rec) plus a link_codes table double.

    Records are stored as plain dicts; encrypt/decrypt are identity-shaped and
    blind_index is 'bi:' + value so lookups stay readable in assertions.
    """
    link_rows = {}
    now = [1_000_000_000]

    class _DateStub:
        @staticmethod
        def now():
            return now[0]

    def clean_string(value, _max_length=240):
        return str(value or "")[:_max_length]

    def valid_node_name(value):
        value = (value or "").strip()
        return bool(value) and bool(NODE_NAME_RE.match(value))

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def _account_row(_env, name):
        rec = accounts.get(name)
        return "bi:" + name, (dict(rec) if rec is not None else None)

    async def _save_account(_env, name_bi, rec, email_bi=None, ip_bi=None):
        accounts[name_bi[3:]] = dict(rec)

    async def d1_all(_env, sql, *args):
        if "FROM accounts" in sql:
            return [{"name_bi": "bi:" + name, "data": dict(rec)}
                    for name, rec in accounts.items()]
        raise AssertionError("unexpected d1_all: " + sql)

    async def d1_first(_env, sql, *args):
        if "FROM accounts WHERE email_bi" in sql:
            email = args[0][3:]
            for name, rec in accounts.items():
                if rec.get("email") == email:
                    return {"name_bi": "bi:" + name, "data": dict(rec)}
            return None
        if "FROM link_codes" in sql:
            row = link_rows.get(args[0])
            return dict(row) if row else None
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO link_codes"):
            link_rows[args[0]] = {"data": args[1], "ts": args[2]}
            return
        if sql.startswith("DELETE FROM link_codes"):
            link_rows.pop(args[0], None)
            return
        if "account_presence" in sql:
            return
        raise AssertionError("unexpected d1_run: " + sql)

    async def decrypt_row(_env, stored, key=None):
        return dict(stored)

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def verify_password(password, _salt, _hash):
        return password == "correct horse battery staple"

    async def ed25519_verify(_pubkey, _sig, _canonical):
        return True

    async def _is_admin(_env, name):
        return bool(accounts.get(name, {}).get("is_admin"))

    def clean_avatar_png(_value):
        return "", ""

    async def _solana_balance_lamports(_env, _wallet):
        return None

    def _random_bytes(n):
        return bytes([7] * n)  # deterministic -> code "434247"

    def _ts_ok(_ts):
        return True

    namespace = _load_functions({
        "Date": _DateStub,
        "clean_string": clean_string,
        "valid_node_name": valid_node_name,
        "json_response": json_response,
        "blind_index": blind_index,
        "_account_row": _account_row,
        "_save_account": _save_account,
        "d1_all": d1_all,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "encrypt_row": encrypt_row,
        "verify_password": verify_password,
        "ed25519_verify": ed25519_verify,
        "_is_admin": _is_admin,
        "clean_avatar_png": clean_avatar_png,
        "_solana_balance_lamports": _solana_balance_lamports,
        "_random_bytes": _random_bytes,
        "_ts_ok": _ts_ok,
        "MAX_NODE_NAME": 63,
        "CLAIM_CODE_TTL_MS": 10 * 60 * 1000,
        "CLAIM_CODE_MAX_ATTEMPTS": 5,
        "OWNERSHIP_TRANSFER_TTL_MS": 24 * 60 * 60 * 1000,
        "LINK_CODE_TTL_MS": 30 * 60 * 1000,
        "LINK_CODE_RE": re.compile(r"^[0-9]{6}$"),
        "SOLANA_RE": re.compile(r"^solana-[a-z0-9]{4,60}$"),
        "NODE_PUBKEY_RE": NODE_PUBKEY_RE,
    })
    namespace["_now"] = now
    namespace["_link_rows"] = link_rows
    return namespace


def _user_rec(name="alice", email="alice@example.com"):
    return {
        "name": name, "email": email, "status": "active",
        "pass_salt": "salt", "pass_hash": "hash", "pubkey": "PK-" + name,
    }


def _node_rec(name="mirror1"):
    return {"name": name, "status": "active", "pubkey": "PK-" + name}


def _auth(extra):
    return {"identifier": "alice@example.com",
            "password": "correct horse battery staple", **extra}


def test_claim_flow_links_node_via_heartbeat_code():
    accounts = {"alice": _user_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    started = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "mirror1"}))))
    assert started["status"] == 201
    assert started["data"]["pending"] is True
    # The code never leaves via the website response.
    assert "code" not in str(started["data"])
    pending = accounts["mirror1"]["claim_pending"]
    assert pending["user"] == "alice"
    assert re.fullmatch(r"[0-9]{6}", pending["code"])

    # Only the node (signed heartbeat) is shown the code.
    beat = asyncio.run(ns["_account_heartbeat"](env, _Request(
        {"nodeName": "mirror1", "ts": "1", "sig": "s"})))
    assert beat["status"] == 200
    assert beat["data"]["claim"] == {"user": "alice", "code": pending["code"]}

    # A wrong code is rejected and counted.
    wrong = asyncio.run(ns["_account_claim_confirm"](
        env, _Request(_auth({"nodeId": "mirror1", "code": "000000"}))))
    assert wrong["status"] == 401
    assert wrong["data"]["error"] == "bad_code"
    assert accounts["mirror1"]["claim_pending"]["attempts"] == 1

    confirmed = asyncio.run(ns["_account_claim_confirm"](
        env, _Request(_auth({"nodeId": "mirror1", "code": pending["code"]}))))
    assert confirmed["status"] == 200
    assert confirmed["data"]["linked"] is True
    assert confirmed["data"]["nodes"] == ["mirror1"]
    assert accounts["mirror1"]["owner"] == "alice"
    assert "claim_pending" not in accounts["mirror1"]
    assert accounts["alice"]["nodes"] == ["mirror1"]


def test_claim_node_accepts_pubkey_as_node_id():
    # issue #351: the desktop app's own profile card calls the node's Ed25519
    # public key its "Node ID", so a user pasting that value into the
    # website's claim form must resolve to the node's actual account name,
    # not be rejected as malformed.
    pubkey = "A" * 43
    accounts = {"alice": _user_rec(), "mirror1": dict(_node_rec(), pubkey=pubkey)}
    ns = _harness(accounts)
    env = object()

    started = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": pubkey}))))
    assert started["status"] == 201
    assert started["data"]["nodeId"] == "mirror1"
    pending = accounts["mirror1"]["claim_pending"]

    confirmed = asyncio.run(ns["_account_claim_confirm"](
        env, _Request(_auth({"nodeId": pubkey, "code": pending["code"]}))))
    assert confirmed["status"] == 200
    assert confirmed["data"]["nodeId"] == "mirror1"
    assert accounts["mirror1"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["mirror1"]

    # A key with no matching account is a 404, same as an unknown name.
    missing = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "B" * 43}))))
    assert missing["status"] == 404
    assert missing["data"]["error"] == "no_such_node"

    # Something that's neither a name nor a key-shaped value is still 400.
    malformed = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "!!!not-valid!!!"}))))
    assert malformed["status"] == 400
    assert malformed["data"]["error"] == "invalid_node_id"


def test_claim_rejections():
    accounts = {
        "alice": _user_rec(),
        "bob": _user_rec("bob", "bob@example.com"),
        "mirror1": dict(_node_rec(), owner="bob"),
    }
    ns = _harness(accounts)
    env = object()

    bad_pass = asyncio.run(ns["_account_claim_node"](env, _Request(
        {"identifier": "alice@example.com", "password": "wrong",
         "nodeId": "mirror1"})))
    assert bad_pass["status"] == 401

    not_node = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "bob"}))))
    assert not_node["status"] == 403
    assert not_node["data"]["error"] == "not_a_node"

    owned = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "mirror1"}))))
    assert owned["status"] == 409
    assert owned["data"]["error"] == "node_already_owned"

    self_claim = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "alice"}))))
    assert self_claim["status"] == 400
    assert self_claim["data"]["error"] == "cannot_claim_self"

    missing = asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "ghost"}))))
    assert missing["status"] == 404


def test_expired_claim_is_cleaned_up_by_heartbeat():
    accounts = {"alice": _user_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "mirror1"}))))
    ns["_now"][0] += 11 * 60 * 1000  # past CLAIM_CODE_TTL_MS

    confirm = asyncio.run(ns["_account_claim_confirm"](
        env, _Request(_auth({"nodeId": "mirror1",
                             "code": accounts["mirror1"]["claim_pending"]["code"]}))))
    assert confirm["status"] == 404
    assert confirm["data"]["error"] == "no_pending_claim"

    beat = asyncio.run(ns["_account_heartbeat"](env, _Request(
        {"nodeName": "mirror1", "ts": "1", "sig": "s"})))
    assert "claim" not in beat["data"]
    assert "claim_pending" not in accounts["mirror1"]


def test_too_many_wrong_codes_invalidate_the_claim():
    accounts = {"alice": _user_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_account_claim_node"](
        env, _Request(_auth({"nodeId": "mirror1"}))))
    for _ in range(5):
        asyncio.run(ns["_account_claim_confirm"](
            env, _Request(_auth({"nodeId": "mirror1", "code": "000000"}))))
    assert "claim_pending" not in accounts["mirror1"]


def _admin_rec(name="admin1"):
    return {"name": name, "status": "active", "pubkey": "PK-" + name,
            "is_admin": True}


def test_admin_ownership_transfer_approved_via_node_heartbeat():
    accounts = {"admin1": _admin_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    requested = asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "mirror1", "ts": "1", "sig": "s"})))
    assert requested["status"] == 201
    pending = accounts["mirror1"]["ownership_transfer_pending"]
    assert pending["admin"] == "admin1"

    # The prompt only ever reaches the target node via its own heartbeat.
    beat = asyncio.run(ns["_account_heartbeat"](env, _Request(
        {"nodeName": "mirror1", "ts": "1", "sig": "s"})))
    assert beat["data"]["ownershipTransfer"] == {"admin": "admin1"}

    confirmed = asyncio.run(ns["_account_ownership_transfer_confirm"](
        env, _Request({"nodeName": "mirror1", "ts": "1", "sig": "s",
                      "action": "approve"})))
    assert confirmed["status"] == 200
    assert confirmed["data"]["owner"] == "admin1"
    assert accounts["mirror1"]["owner"] == "admin1"
    assert "ownership_transfer_pending" not in accounts["mirror1"]
    assert accounts["admin1"]["nodes"] == ["mirror1"]


def test_admin_ownership_transfer_denied_leaves_owner_unchanged():
    accounts = {"admin1": _admin_rec(),
               "mirror1": dict(_node_rec(), owner="bob")}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "mirror1", "ts": "1", "sig": "s"})))

    denied = asyncio.run(ns["_account_ownership_transfer_confirm"](
        env, _Request({"nodeName": "mirror1", "ts": "1", "sig": "s",
                      "action": "deny"})))
    assert denied["status"] == 200
    assert denied["data"]["denied"] is True
    assert accounts["mirror1"]["owner"] == "bob"
    assert "ownership_transfer_pending" not in accounts["mirror1"]


def test_admin_ownership_transfer_rejections():
    accounts = {
        "admin1": _admin_rec(),
        "alice": _user_rec(),
        "mirror1": _node_rec(),
    }
    ns = _harness(accounts)
    env = object()

    not_admin = asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "alice", "target": "mirror1", "ts": "1", "sig": "s"})))
    assert not_admin["status"] == 401

    not_a_node = asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "alice", "ts": "1", "sig": "s"})))
    assert not_a_node["status"] == 403
    assert not_a_node["data"]["error"] == "not_a_node"

    self_request = asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "admin1", "ts": "1", "sig": "s"})))
    assert self_request["status"] == 400
    assert self_request["data"]["error"] == "cannot_request_self"

    missing = asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "ghost", "ts": "1", "sig": "s"})))
    assert missing["status"] == 404

    # No pending marker at all -> confirm is a no-op 404.
    no_pending = asyncio.run(ns["_account_ownership_transfer_confirm"](
        env, _Request({"nodeName": "mirror1", "ts": "1", "sig": "s",
                      "action": "approve"})))
    assert no_pending["status"] == 404
    assert no_pending["data"]["error"] == "no_pending_transfer"


def test_expired_ownership_transfer_is_cleaned_up_by_heartbeat():
    accounts = {"admin1": _admin_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_admin_request_ownership"](env, _Request(
        {"node": "admin1", "target": "mirror1", "ts": "1", "sig": "s"})))
    ns["_now"][0] += 25 * 60 * 60 * 1000  # past OWNERSHIP_TRANSFER_TTL_MS

    confirm = asyncio.run(ns["_account_ownership_transfer_confirm"](
        env, _Request({"nodeName": "mirror1", "ts": "1", "sig": "s",
                      "action": "approve"})))
    assert confirm["status"] == 404

    beat = asyncio.run(ns["_account_heartbeat"](env, _Request(
        {"nodeName": "mirror1", "ts": "1", "sig": "s"})))
    assert "ownershipTransfer" not in beat["data"]
    assert "ownership_transfer_pending" not in accounts["mirror1"]


def test_link_self_attaches_node_with_node_key_and_user_password():
    # The desktop "Log in as a user" path: the node signs with its own key and
    # supplies the user's password, so the link completes in one call with no
    # confirmation code.
    accounts = {"alice": _user_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    linked = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "mirror1", "ts": "1", "sig": "s"}))))
    assert linked["status"] == 200
    assert linked["data"]["linked"] is True
    assert linked["data"]["nodes"] == ["mirror1"]
    assert accounts["mirror1"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["mirror1"]

    # Re-running is idempotent (already linked to the same user).
    again = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "mirror1", "ts": "1", "sig": "s"}))))
    assert again["data"]["alreadyLinked"] is True


def test_link_self_rejections():
    accounts = {
        "alice": _user_rec(),
        "bob": _user_rec("bob", "bob@example.com"),
        "owned": dict(_node_rec("owned"), owner="bob"),
        "mirror1": _node_rec(),
    }
    ns = _harness(accounts)
    env = object()

    bad_pass = asyncio.run(ns["_account_link_self"](env, _Request(
        {"identifier": "alice@example.com", "password": "wrong",
         "nodeName": "mirror1", "ts": "1", "sig": "s"})))
    assert bad_pass["status"] == 401
    assert bad_pass["data"]["error"] == "invalid_credentials"

    missing = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "ghost", "ts": "1", "sig": "s"}))))
    assert missing["status"] == 404

    not_node = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "bob", "ts": "1", "sig": "s"}))))
    assert not_node["status"] == 403
    assert not_node["data"]["error"] == "not_a_node"

    self_link = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "alice", "ts": "1", "sig": "s"}))))
    assert self_link["status"] == 400
    assert self_link["data"]["error"] == "cannot_link_self"

    owned = asyncio.run(ns["_account_link_self"](env, _Request(
        _auth({"nodeName": "owned", "ts": "1", "sig": "s"}))))
    assert owned["status"] == 409
    assert owned["data"]["error"] == "node_already_owned"


def _grant(node="mirror1", user="alice"):
    # The browser half of "Link this node to your account" (adhoc #120): the
    # node-signed grant from the URL plus the dashboard session's user name.
    return {"nodeName": node, "user": user, "ts": "1", "sig": "s"}


def test_link_grant_links_node_to_browser_user():
    # The node profile's browser button: a node-signed grant redeemed by the
    # dashboard's logged-in user — no password re-entry, no confirmation code.
    accounts = {"alice": _user_rec(), "mirror1": _node_rec()}
    ns = _harness(accounts)
    env = object()

    linked = asyncio.run(ns["_account_link_grant"](env, _Request(_grant())))
    assert linked["status"] == 200
    assert linked["data"]["linked"] is True
    assert linked["data"]["nodes"] == ["mirror1"]
    assert accounts["mirror1"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["mirror1"]
    assert ns["_link_rows"], "redeemed grant must be parked as used"

    # Redeeming the same grant again is idempotent while still linked…
    again = asyncio.run(ns["_account_link_grant"](env, _Request(_grant())))
    assert again["status"] == 200
    assert again["data"]["alreadyLinked"] is True

    # …but once the node is unlinked, the burned grant cannot re-link it.
    del accounts["mirror1"]["owner"]
    replay = asyncio.run(ns["_account_link_grant"](env, _Request(_grant())))
    assert replay["status"] == 409
    assert replay["data"]["error"] == "grant_used"
    assert "owner" not in accounts["mirror1"]


def test_link_grant_overrides_existing_association():
    # The grant is signed by the node's own key, so it OVERRIDES the current
    # association: an owned node re-homes to the redeeming user (leaving the
    # old owner's fleet), and even a user-kind account's node can be taken
    # possession of.
    accounts = {
        "alice": _user_rec(),
        "bob": _user_rec("bob", "bob@example.com"),
        "owned": dict(_node_rec("owned"), owner="bob"),
    }
    accounts["bob"]["nodes"] = ["owned"]
    ns = _harness(accounts)
    env = object()

    rehomed = asyncio.run(ns["_account_link_grant"](
        env, _Request(_grant(node="owned"))))
    assert rehomed["status"] == 200
    assert rehomed["data"]["linked"] is True
    assert accounts["owned"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["owned"]
    assert accounts["bob"]["nodes"] == [], "old owner's fleet must shrink"

    # A node whose account is itself a user can still be handed over — its own
    # key signed the grant, so the operator authorized it.
    taken = asyncio.run(ns["_account_link_grant"](
        env, _Request({"nodeName": "bob", "user": "alice",
                       "ts": "2", "sig": "s2"})))
    assert taken["status"] == 200
    assert taken["data"]["linked"] is True
    assert accounts["bob"]["owner"] == "alice"
    assert sorted(accounts["alice"]["nodes"]) == ["bob", "owned"]

    # Logged in as the node's own account: a friendly no-op, not an error.
    self_link = asyncio.run(ns["_account_link_grant"](
        env, _Request(_grant(node="alice", user="alice"))))
    assert self_link["status"] == 200
    assert self_link["data"]["alreadyLinked"] is True
    assert self_link["data"]["selfAccount"] is True
    assert "owner" not in accounts["alice"]


def test_link_grant_rejections():
    accounts = {
        "alice": _user_rec(),
        "mirror1": _node_rec(),
        "stray": _node_rec("stray"),
    }
    ns = _harness(accounts)
    env = object()

    missing = asyncio.run(ns["_account_link_grant"](
        env, _Request(_grant(node="ghost"))))
    assert missing["status"] == 404
    assert missing["data"]["error"] == "no_such_node"

    ghost_user = asyncio.run(ns["_account_link_grant"](
        env, _Request(_grant(user="ghost"))))
    assert ghost_user["status"] == 404
    assert ghost_user["data"]["error"] == "no_such_user"

    # A bare node session in the browser can't take possession of other nodes.
    not_user = asyncio.run(ns["_account_link_grant"](
        env, _Request(_grant(user="stray"))))
    assert not_user["status"] == 403
    assert not_user["data"]["error"] == "not_a_user"

    # A stale timestamp or a bad signature invalidates the grant outright
    # (the exec'd functions resolve globals through the harness namespace, so
    # rebinding these stubs takes effect immediately).
    ns["_ts_ok"] = lambda _ts: False
    stale = asyncio.run(ns["_account_link_grant"](env, _Request(_grant())))
    assert stale["status"] == 401
    assert stale["data"]["error"] == "unauthorized"
    ns["_ts_ok"] = lambda _ts: True

    async def _reject(_pubkey, _sig, _canonical):
        return False
    ns["ed25519_verify"] = _reject
    forged = asyncio.run(ns["_account_link_grant"](env, _Request(_grant())))
    assert forged["status"] == 401
    assert forged["data"]["error"] == "bad_signature"

    assert "owner" not in accounts["mirror1"]


def test_link_code_rendezvous_node_registers_first():
    accounts = {"alice": _user_rec(), "mirror2": _node_rec("mirror2")}
    ns = _harness(accounts)
    env = object()

    # The fresh node's finalize parks its half first…
    parked = asyncio.run(ns["_redeem_or_park_link_code"](
        env, "123456", node="mirror2"))
    assert parked == {"linked": False}

    # …then the installing desktop (a user account) offers the same code.
    offer = asyncio.run(ns["_account_link_node"](env, _Request(
        {"nodeName": "alice", "code": "123456", "ts": "1", "sig": "s"})))
    assert offer["status"] == 200
    assert offer["data"]["linked"] is True
    assert offer["data"]["node"] == "mirror2"
    assert accounts["mirror2"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["mirror2"]
    assert not ns["_link_rows"], "code must be burned after linking"


def test_link_code_rendezvous_offer_arrives_first():
    accounts = {"alice": _user_rec(), "mirror2": _node_rec("mirror2")}
    ns = _harness(accounts)
    env = object()

    offer = asyncio.run(ns["_account_link_node"](env, _Request(
        {"nodeName": "alice", "code": "654321", "ts": "1", "sig": "s"})))
    assert offer["status"] == 202
    assert offer["data"]["pending"] is True

    redeemed = asyncio.run(ns["_redeem_or_park_link_code"](
        env, "654321", node="mirror2"))
    assert redeemed["linked"] is True
    assert redeemed["user"] == "alice"
    assert accounts["mirror2"]["owner"] == "alice"
    assert accounts["alice"]["nodes"] == ["mirror2"]
    assert not ns["_link_rows"]


def test_link_offer_resolves_the_user_behind_an_owned_node():
    # An owned node's key can offer a code on behalf of its owning user; a
    # bare, unowned node account cannot confer ownership at all.
    accounts = {
        "alice": _user_rec(),
        "mirror1": dict(_node_rec(), owner="alice"),
        "stray": _node_rec("stray"),
        "mirror3": _node_rec("mirror3"),
    }
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_redeem_or_park_link_code"](env, "111222", node="mirror3"))
    via_owned = asyncio.run(ns["_account_link_node"](env, _Request(
        {"nodeName": "mirror1", "code": "111222", "ts": "1", "sig": "s"})))
    assert via_owned["data"]["user"] == "alice"
    assert accounts["mirror3"]["owner"] == "alice"

    refused = asyncio.run(ns["_account_link_node"](env, _Request(
        {"nodeName": "stray", "code": "333444", "ts": "1", "sig": "s"})))
    assert refused["status"] == 403
    assert refused["data"]["error"] == "no_user_account"


def test_expired_link_code_is_not_redeemed():
    accounts = {"alice": _user_rec(), "mirror2": _node_rec("mirror2")}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["_redeem_or_park_link_code"](env, "999888", node="mirror2"))
    ns["_now"][0] += 31 * 60 * 1000  # past LINK_CODE_TTL_MS

    offer = asyncio.run(ns["_account_link_node"](env, _Request(
        {"nodeName": "alice", "code": "999888", "ts": "1", "sig": "s"})))
    assert offer["status"] == 202, "stale half must not link; offer re-parks"
    assert "owner" not in accounts["mirror2"]


def test_wire_contracts_across_worker_qt_and_installer():
    entry = ENTRY.read_text(encoding="utf-8")
    qt_setup = QT_SETUP.read_text(encoding="utf-8")
    qt_chat = QT_CHAT.read_text(encoding="utf-8")
    install = INSTALL_SH.read_text(encoding="utf-8")

    # The link-offer canonical string matches on both ends.
    assert '"forkmesh-link-v1\\n" + name + "\\n" + code + "\\n" + ts' in entry
    assert '"forkmesh-link-v1\\n" + owner + "\\n" + code + "\\n" + ts' in qt_chat

    # The installer's printed marker is exactly what the desktop app scans for.
    assert 'FORKMESH LINK CODE: $FORKMESH_LINK_CODE' in install
    assert 'FORKMESH LINK CODE:\\\\s*([0-9]{6})' in qt_chat

    # The daemon inherits the code and presents it at finalize.
    assert 'FORKMESH_LINK_CODE="$FORKMESH_LINK_CODE"' in install
    assert 'qEnvironmentVariable("FORKMESH_LINK_CODE")' in qt_setup
    assert '"linkCode"' in qt_setup
    assert 'data.get("linkCode", "")' in entry

    # The heartbeat is the only channel that carries the claim code out.
    assert 'response["claim"]' in entry
    assert 'resp.value(QStringLiteral("claim")).toObject()' in qt_setup

    # "Log in as a user" (link-self): the node signs its own key over the same
    # canonical string the worker verifies, and POSTs it to /link-self.
    assert '"forkmesh-link-self-v1\\n" + node_name + "\\n" + identifier +' in entry
    assert '"/api/accounts/link-self"' in entry
    assert '"forkmesh-link-self-v1\\n" + node + "\\n" + id + "\\n" + ts' in qt_chat
    assert 'accountsApiUrl("link-self")' in qt_chat

    # The public account lookup lists the nodes linked to the account (the same
    # data _account_public_payload already exposes), so a node's profile can
    # render its user's whole fleet — the "show the linked nodes" ask.
    assert '"nodes": _owned_nodes(rec)}' in entry
    assert 'profileNodesFromJson(resp.value(QStringLiteral("nodes")))' in qt_chat
    # The profile knows when its own account is a user (so it stops nagging to
    # "log in as a user" and instead lists the nodes it owns).
    assert 'm_profileIsUserAccount' in qt_chat


def test_link_grant_wire_contract_across_worker_qt_and_dashboard():
    entry = ENTRY.read_text(encoding="utf-8")
    qt_chat = QT_CHAT.read_text(encoding="utf-8")
    # dashboard.js is split into ordered public/dashboard/js/*.js fragments the
    # Worker composes into one /dashboard.js (see src/dashboard_bundle.py).
    dashboard_js = assembled_dashboard_js()
    login_js = (ROOT / "cloudflare_worker" / "public" / "login.js").read_text(
        encoding="utf-8")

    # The grant canonical string matches on both ends, and the endpoint exists.
    assert '"forkmesh-link-grant-v1\\n" + node_name + "\\n" + ts' in entry
    assert '"/api/accounts/link-grant"' in entry
    assert '"forkmesh-link-grant-v1\\n" + node + "\\n" + ts' in qt_chat

    # The desktop app opens /dashboard?link_node=…&link_ts=…&link_sig=… and the
    # dashboard redeems exactly those params against the endpoint.
    for marker in ("link_node", "link_ts", "link_sig"):
        assert marker in qt_chat
        assert marker in dashboard_js
    assert '"/api/accounts/link-grant"' in dashboard_js
    assert "offerLinkGrant" in dashboard_js
    assert "redeemLinkGrant" in dashboard_js

    # The browser side asks for one explicit "Authenticate & link" click; the
    # confirm row exists in both (byte-identical) dashboard HTML copies.
    index_html = (ROOT / "cloudflare_worker" / "public" / "dashboard" /
                  "index.html").read_text(encoding="utf-8")
    dashboard_html = (ROOT / "cloudflare_worker" / "public" /
                      "dashboard.html").read_text(encoding="utf-8")
    # The shell is split into partials the Worker composes; the confirm row lives
    # in the assembled document. The two shell files stay byte-identical.
    assert index_html == dashboard_html
    composed = assembled_dashboard()
    assert "data-link-grant-row" in composed
    assert "data-link-grant-confirm" in composed
    assert "data-link-grant-confirm" in dashboard_js

    # A logged-out browser bounces through login and resumes via ?next= (local
    # paths only, so the bounce can't become an open redirect).
    assert "/login?next=" in dashboard_js
    assert "nextPath()" in login_js
    assert 'value.startsWith("/") && !value.startsWith("//")' in login_js


def test_dashboard_exposes_a_claim_node_panel():
    dashboard_js = assembled_dashboard_js()
    for marker in ("claim-node", "claim-confirm", "renderClaimNodePanel",
                   "data-claim-node-input", "data-claim-code-input"):
        assert marker in dashboard_js

    index_html = (ROOT / "cloudflare_worker" / "public" / "dashboard" /
                 "index.html").read_text(encoding="utf-8")
    dashboard_html = (ROOT / "cloudflare_worker" / "public" / "dashboard.html").read_text(
        encoding="utf-8")
    # The claim panel lives in a shell partial the Worker composes; assert it on
    # the assembled document. The two shell files must stay byte-identical (one is
    # served, the other 308-redirects to it; a frontend test asserts equality).
    assert index_html == dashboard_html
    composed = assembled_dashboard()
    assert "data-claim-node-input" in composed
    assert "data-claim-code-confirm" in composed
