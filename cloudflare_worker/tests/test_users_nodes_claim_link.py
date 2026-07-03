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


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
QT_SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
QT_CHAT = ROOT / "qt_client" / "src" / "MainWindowChat.cpp"
INSTALL_SH = ROOT / "cloudflare_worker" / "public" / "install.sh"

FUNCS = {
    "_account_kind", "_owned_nodes", "_generate_confirm_code", "_claim_pending",
    "_resolve_user_by_password", "_link_node_to_user", "_account_claim_node",
    "_account_claim_confirm", "_redeem_or_park_link_code", "_account_link_node",
    "_account_link_self", "_account_heartbeat",
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

    async def _is_admin(_env, _name):
        return False

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
        "LINK_CODE_TTL_MS": 30 * 60 * 1000,
        "LINK_CODE_RE": re.compile(r"^[0-9]{6}$"),
        "SOLANA_RE": re.compile(r"^solana-[a-z0-9]{4,60}$"),
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


def test_dashboard_exposes_a_claim_node_panel():
    dashboard_js = (ROOT / "cloudflare_worker" / "public" / "dashboard.js").read_text(
        encoding="utf-8")
    for marker in ("claim-node", "claim-confirm", "renderClaimNodePanel",
                   "data-claim-node-input", "data-claim-code-input"):
        assert marker in dashboard_js

    index_html = (ROOT / "cloudflare_worker" / "public" / "dashboard" /
                 "index.html").read_text(encoding="utf-8")
    dashboard_html = (ROOT / "cloudflare_worker" / "public" / "dashboard.html").read_text(
        encoding="utf-8")
    for html in (index_html, dashboard_html):
        assert "data-claim-node-input" in html
        assert "data-claim-code-confirm" in html
    # The two dashboard copies must stay byte-identical (one is served, the
    # other 308-redirects to it; a frontend test elsewhere asserts equality).
    assert index_html == dashboard_html
