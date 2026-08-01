#!/usr/bin/env python3
"""Key-signed organization-task requests from the desktop (adhoc #18, #52).

A desktop that authenticated silently owns its account's Ed25519 key and holds
no account session token, so the Agents composer's Task toggle and the Tasks
tab's board can only reach /api/tasks by signing a proof onto the URL. These
checks pin how narrow that credential is: three operations, each with its own
proof, the completion proof bound to the one task it closes, and no authority
for an account that is not an active user.

AST-extraction harness in the style of test_chat_room_key.py.
"""

import ast
import asyncio
import hashlib
import re
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

SOURCE_TEXT = ENTRY_TEXT + "\n" + CATALOG.read_text(encoding="utf-8")

FUNCS = {
    "_org_task_signed_session",
    "_owner_signing_pubkeys",
    "_verify_owner_signature",
    "clean_string",
}
CONSTANTS = {
    "ORG_TASK_OPEN_PROOF",
    "ORG_TASK_COMPLETE_PROOF",
    "ORG_TASK_LIST_PROOF",
    "ORG_TASK_COMPLETE_RE",
    "ORG_TASK_COLLECTION_RE",
}

TASK_ID = "a" * 32
OTHER_TASK_ID = "b" * 32


def _load(extra):
    tree = ast.parse(SOURCE_TEXT, filename=str(ENTRY))
    selected = []
    for node in tree.body:
        if (
            isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            and node.name in FUNCS
        ):
            selected.append(node)
        elif isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id in CONSTANTS
            for target in node.targets
        ):
            selected.append(node)
    found = {
        node.name if hasattr(node, "name") else node.targets[0].id
        for node in selected
    }
    assert found == FUNCS | CONSTANTS, \
        "missing: %s" % sorted((FUNCS | CONSTANTS) - found)
    module = ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Headers:
    def __init__(self, mapping=None):
        self._m = {str(k).lower(): v for k, v in (mapping or {}).items()}

    def get(self, name, default=None):
        return self._m.get(str(name).lower(), default)


class _Request:
    def __init__(self, method="POST", url="", headers=None):
        self.method = method
        self.url = url
        self.headers = _Headers(headers)


def _fake_sig(pubkey, canonical):
    digest = hashlib.sha256(canonical).hexdigest()[:32]
    return "sig-" + pubkey + "-" + digest


def _harness(accounts, ts_ok=True, devices=None):
    devices = devices or {}

    async def _account_row(_env, name):
        key = str(name or "").strip().lower()
        record = accounts.get(key)
        if record is None:
            return "bi:" + key, None
        record = dict(record)
        record.setdefault("name", key)
        return "bi:" + key, record

    async def _owner_pubkey(_env, owner):
        _bi, record = await _account_row(_env, owner)
        return record.get("pubkey", "") if record else ""

    async def ed25519_verify(pubkey, sig, canonical):



        return bool(pubkey) and sig == _fake_sig(pubkey, canonical)

    async def blind_index(_env, value):
        return "bi:" + str(value or "").strip().lower()

    async def _account_devices_list(_env, account_bi):
        return [dict(device) for device in devices.get(account_bi, [])]

    namespace = _load({
        "re": re,
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "method_name": lambda request: getattr(request, "method", "GET"),
        "_ts_ok": lambda _ts: ts_ok,
        "_owner_pubkey": _owner_pubkey,
        "_account_row": _account_row,
        "_account_devices_list": _account_devices_list,
        "blind_index": blind_index,
        "_account_kind": lambda record: record.get("kind", "user"),
        "ed25519_verify": ed25519_verify,
        "MAX_NODE_NAME": 63,
    })
    return namespace, object()


def _device(pubkey, capabilities=("owner_sign",), enabled=True):
    return {
        "pubkey": pubkey,
        "capabilities": list(capabilities),
        "enabled": enabled,
    }


def _signed_url(namespace, path, proof_name, resource="", node="alice",
                pubkey="PK-alice", ts="1700000000000"):
    canonical = namespace[proof_name] + "\n" + node + "\n"
    if resource:
        canonical += resource + "\n"
    canonical += ts
    sig = _fake_sig(pubkey, canonical.encode())
    return (
        "https://forkmesh.test" + path
        + "?node=" + node + "&ts=" + ts + "&sig=" + sig
    )


def _user(**overrides):
    record = {"pubkey": "PK-alice", "status": "active", "kind": "user"}
    record.update(overrides)
    return record


def test_open_and_complete_proofs_authorize_their_own_operation():
    namespace, env = _harness({"alice": _user()})
    resolve = namespace["_org_task_signed_session"]

    account_bi, record = asyncio.run(resolve(env, _Request(
        url=_signed_url(namespace, "/api/tasks", "ORG_TASK_OPEN_PROOF"))))
    assert account_bi == "bi:alice"
    assert record["name"] == "alice"

    account_bi, record = asyncio.run(resolve(env, _Request(
        url=_signed_url(
            namespace, "/api/tasks/%s/complete" % TASK_ID,
            "ORG_TASK_COMPLETE_PROOF", resource=TASK_ID))))
    assert account_bi == "bi:alice"
    assert record["name"] == "alice"


def test_completion_proof_is_bound_to_the_task_it_names():
    namespace, env = _harness({"alice": _user()})

    replayed = _signed_url(
        namespace, "/api/tasks/%s/complete" % OTHER_TASK_ID,
        "ORG_TASK_COMPLETE_PROOF", resource=TASK_ID)
    account_bi, record = asyncio.run(
        namespace["_org_task_signed_session"](env, _Request(url=replayed)))
    assert (account_bi, record) == ("", None)


    crossed = _signed_url(
        namespace, "/api/tasks/%s/complete" % TASK_ID,
        "ORG_TASK_OPEN_PROOF", resource=TASK_ID)
    account_bi, record = asyncio.run(
        namespace["_org_task_signed_session"](env, _Request(url=crossed)))
    assert (account_bi, record) == ("", None)


def test_list_proof_reads_the_board_and_nothing_else():
    """The Tasks tab's GET is signature-authorized; its edits are not (#52)."""

    namespace, env = _harness({"alice": _user()})
    resolve = namespace["_org_task_signed_session"]

    account_bi, record = asyncio.run(resolve(env, _Request(
        method="GET",
        url=_signed_url(namespace, "/api/tasks", "ORG_TASK_LIST_PROOF"))))
    assert account_bi == "bi:alice"
    assert record["name"] == "alice"


    for path in ("/api/tasks/%s" % TASK_ID, "/api/tasks/%s/qa" % TASK_ID):
        assert asyncio.run(resolve(env, _Request(
            method="GET",
            url=_signed_url(
                namespace, path, "ORG_TASK_LIST_PROOF",
                resource=TASK_ID)))) == ("", None), path
        assert asyncio.run(resolve(env, _Request(
            method="GET",
            url=_signed_url(
                namespace, path, "ORG_TASK_LIST_PROOF")))) == ("", None), path


    assert asyncio.run(resolve(env, _Request(
        url=_signed_url(
            namespace, "/api/tasks", "ORG_TASK_LIST_PROOF")))) == ("", None)
    assert asyncio.run(resolve(env, _Request(
        method="GET",
        url=_signed_url(
            namespace, "/api/tasks", "ORG_TASK_OPEN_PROOF")))) == ("", None)


def test_registered_device_key_reads_the_board_after_a_restart():
    """adhoc #63: the board went "invalid session" on every restarted app.

    A desktop that logged in with a password gets its key stored in
    account_devices; the account's primary pubkey keeps naming the install that
    created the account. The session token is memory-only, so the next launch
    signs the list proof with the device key — which must authorize the read,
    exactly as it authorizes GET /api/sync.
    """
    namespace, env = _harness(
        {"alice": _user()},
        devices={"bi:alice": [
            _device("PK-alice-laptop"),
            _device("PK-alice-retired", enabled=False),
            _device("PK-alice-readonly", capabilities=()),
        ]},
    )
    resolve = namespace["_org_task_signed_session"]

    account_bi, record = asyncio.run(resolve(env, _Request(
        method="GET",
        url=_signed_url(namespace, "/api/tasks", "ORG_TASK_LIST_PROOF",
                        pubkey="PK-alice-laptop"))))
    assert account_bi == "bi:alice"
    assert record["name"] == "alice"


    assert asyncio.run(resolve(env, _Request(
        url=_signed_url(namespace, "/api/tasks", "ORG_TASK_OPEN_PROOF",
                        pubkey="PK-alice-laptop"))))[0] == "bi:alice"


    for pubkey in ("PK-alice-retired", "PK-alice-readonly"):
        assert asyncio.run(resolve(env, _Request(
            method="GET",
            url=_signed_url(
                namespace, "/api/tasks", "ORG_TASK_LIST_PROOF",
                pubkey=pubkey)))) == ("", None), pubkey


def test_signature_authorizes_only_the_three_named_operations():
    namespace, env = _harness({"alice": _user()})
    resolve = namespace["_org_task_signed_session"]


    for method in ("PATCH", "DELETE", "PUT"):
        for proof in ("ORG_TASK_OPEN_PROOF", "ORG_TASK_LIST_PROOF"):
            account_bi, record = asyncio.run(resolve(env, _Request(
                method=method,
                url=_signed_url(namespace, "/api/tasks", proof))))
            assert (account_bi, record) == ("", None), (method, proof)


    for action in ("start", "stop", "checkin", "qa"):
        path = "/api/tasks/%s/%s" % (TASK_ID, action)
        for proof in ("ORG_TASK_OPEN_PROOF", "ORG_TASK_COMPLETE_PROOF"):
            account_bi, record = asyncio.run(resolve(env, _Request(
                url=_signed_url(namespace, path, proof, resource=TASK_ID))))
            assert (account_bi, record) == ("", None), (action, proof)


def test_bad_signature_stale_stamp_and_ineligible_accounts_are_refused():
    namespace, env = _harness({
        "alice": _user(),
        "suspended": _user(status="suspended"),
        "orgaccount": _user(kind="org"),
    })
    resolve = namespace["_org_task_signed_session"]
    open_path = "/api/tasks"

    forged = (
        "https://forkmesh.test/api/tasks"
        "?node=alice&ts=1700000000000&sig=not-a-real-signature"
    )
    assert asyncio.run(resolve(env, _Request(url=forged))) == ("", None)


    wrong_key = _signed_url(
        namespace, open_path, "ORG_TASK_OPEN_PROOF", pubkey="PK-mallory")
    assert asyncio.run(resolve(env, _Request(url=wrong_key))) == ("", None)


    assert asyncio.run(resolve(env, _Request(
        url="https://forkmesh.test/api/tasks"))) == ("", None)
    assert asyncio.run(resolve(env, _Request(url=_signed_url(
        namespace, open_path, "ORG_TASK_OPEN_PROOF",
        node="nobody", pubkey="PK-nobody")))) == ("", None)
    assert asyncio.run(resolve(env, _Request(url=_signed_url(
        namespace, open_path, "ORG_TASK_OPEN_PROOF",
        node="suspended")))) == ("", None)
    assert asyncio.run(resolve(env, _Request(url=_signed_url(
        namespace, open_path, "ORG_TASK_OPEN_PROOF",
        node="orgaccount")))) == ("", None)


    stale, stale_env = _harness({"alice": _user()}, ts_ok=False)
    assert asyncio.run(stale["_org_task_signed_session"](
        stale_env,
        _Request(url=_signed_url(
            stale, open_path, "ORG_TASK_OPEN_PROOF")))) == ("", None)


def test_signed_request_never_falls_back_to_cookie_authority():
    """A URL signature must switch cookie auth off, not merely add to it.

    same_origin() relaxes for a signed write, so if session() could still fall
    through to the cookie a cross-site POST could append a junk triple and
    borrow the victim's browser session.
    """
    start = ENTRY_TEXT.index("class _OfficeMarketingTasksRuntime:")
    runtime = ENTRY_TEXT[start:start + 6000]
    session = runtime[runtime.index("    async def session(self, data=None):"):]
    signed = session.index("if self._signed_query():")
    fallback = session.index("_account_session_record")
    assert signed < fallback, "cookie fallback must sit behind the signed check"
    assert "return await _org_task_signed_session(" in session

    assert "self._signed_query()" in runtime[:runtime.index("def query(")]


def test_desktop_and_worker_agree_on_the_canonical_proof_strings():
    qt = (
        ENTRY.parents[2] / "qt_client" / "src" / "MainWindowInternal.h"
    ).read_text(encoding="utf-8")
    for proof in ("forkmesh-org-task-open-v1", "forkmesh-org-task-complete-v1",
                  "forkmesh-org-task-list-v1"):
        assert '"%s"' % proof in ENTRY_TEXT
        assert '"%s"' % proof in qt


def test_desktop_tasks_tab_signs_when_it_holds_no_session_token():
    """The tab must not go back to bailing out on an empty token (#52).

    Every ordinary launch authenticates silently, so a token-only Tasks tab
    shows an empty board to an operator who is signed in.
    """
    tab = (
        ENTRY.parents[2] / "qt_client" / "src" / "MainWindowTasks.cpp"
    ).read_text(encoding="utf-8")
    request = tab[tab.index("void MainWindow::requestOrganizationTasks("):]
    request = request[:request.index("\nvoid MainWindow::")]
    assert "authenticateOrgTaskRequest(" in request
    assert "kOrgTaskListProof" in tab
