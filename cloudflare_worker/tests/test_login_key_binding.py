#!/usr/bin/env python3
"""Login-to-desktop-key binding contract checks."""

import ast
import asyncio
import base64
import json
import sqlite3
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
QT_MAIN = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
DEVICE_KEY = base64.urlsafe_b64encode(b"\x11" * 32).decode().rstrip("=")
SECOND_DEVICE_KEY = base64.urlsafe_b64encode(b"\x12" * 32).decode().rstrip("=")
DEVICE_TS = "1783000000000"
DEVICE_SIG = "device-proof-signature"


def _load_account_login(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.AsyncFunctionDef, ast.FunctionDef))
        and node.name in {
            "_account_login",
            "_register_account_device",
            "_account_device_for_pubkey",
            "_account_devices_list",
            "_with_session_capabilities",
            "_device_bind_canonical",
            "_device_proven_user_bis",
            "_device_retire_unproven_claims",
        }
    ]
    assert selected, "missing _account_login"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = {"json": json, **extra_globals}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_account_login"]


def _load_device_bind_canonical(extra_globals=None):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "_device_bind_canonical"
    ]
    assert selected, "missing device bind canonical helper"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_device_bind_canonical"]


def _load_account_device_register(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_register_account_device"
    ]
    assert selected, "missing account device registration helper"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_register_account_device"]


def _load_contribution_actor_resolver(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.AsyncFunctionDef, ast.FunctionDef))
        and node.name in {
            "_contribution_actor_user_bis",
        }
    ]
    assert selected, "missing contribution actor identity resolver"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = {"json": json, **extra_globals}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_contribution_actor_user_bis"]


class _Request:
    def __init__(self, body):
        self._body = body

    async def json(self):
        return self._body


def _clean_string(value, max_length=240):
    return str(value or "")[:max_length]


def _valid_node_name(value):
    return bool(value)


def _json_response(data, status=200, **_kwargs):
    return {"status": status, "data": data}


def _login_harness(rec, *, device_proof_valid=True, initial_devices=None,
                   proven_users=None):
    saved = []
    device_rows = [dict(row) for row in (initial_devices or [])]
    proven_users = list(proven_users or [])
    login_clears = []
    login_fails = []

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def d1_first(_env, sql, *args):
        if "FROM users WHERE email_bi" in sql:
            return {"data": "encrypted"} if args == ("bi:alice@example.com",) else None
        if "FROM account_devices" in sql:
            for row in device_rows:
                if args == (row["account_bi"], row["pubkey"]):
                    return dict(row)
        return None

    async def d1_all(_env, sql, *args):
        if "proven_device_users" in sql:
            resolved = set(proven_users)
            resolved.update(
                row["account_bi"] for row in device_rows
                if row.get("pubkey") == args[0]
                and "contribution_key_proof" in
                row.get("capabilities", "").split(",")
            )
            return [{"user_bi": value} for value in sorted(resolved)]
        if "FROM account_devices" in sql:
            return [dict(row) for row in device_rows if args == (row["account_bi"],)]
        return []

    async def d1_run(_env, sql, *args):
        if "INSERT INTO account_devices" in sql:
            row = {
                "device_bi": args[0],
                "account_bi": args[1],
                "pubkey": args[2],
                "kind": args[3],
                "label": args[4],
                "capabilities": args[5],
                "enabled": 1,
                "last_seen": args[7],
                "revoked_at": 0,
            }
            existing = next((item for item in device_rows
                             if item["device_bi"] == row["device_bi"]), None)
            if existing:
                existing.update(row)
            else:
                device_rows.append(row)
        elif "DELETE FROM account_devices" in sql and args:
            pubkey = args[0]
            account_bi = args[1] if len(args) > 1 else None
            device_rows[:] = [
                row for row in device_rows
                if not (row.get("pubkey") == pubkey
                        and row.get("account_bi") != account_bi
                        and "contribution_key_proof" not in
                        row.get("capabilities", "").split(","))
            ]

    async def decrypt_row(_env, _data):
        return dict(rec)

    async def _account_row(_env, name):
        return "bi:" + name, dict(rec)

    async def verify_password(password, _salt, _hash):
        return password == "correct horse battery staple"

    async def totp_verify(_secret, _totp):
        return True

    async def ed25519_verify(pubkey, signature, canonical):
        expected = (
            "forkmesh-device-bind-v1\n"
            "alice-node\n" + pubkey + "\n" + DEVICE_TS
        ).encode()
        return (device_proof_valid and signature == DEVICE_SIG
                and canonical == expected)

    async def _login_record_fail(_env, id_bi):
        login_fails.append(id_bi)

    async def _login_locked_until(_env, _id_bi):
        return 0

    async def _login_clear(_env, id_bi):
        login_clears.append(id_bi)

    async def _save_account(_env, name_bi, updated_rec, **_kwargs):
        saved.append((name_bi, dict(updated_rec)))

    async def _is_admin(_env, _name):
        return False

    async def _account_public_payload(_env, account):
        solana = account.get("solana", "")
        return {
            "ok": True,
            "nodeName": account.get("name", ""),
            "email": account.get("email", ""),
            "status": account.get("status", "active"),
            "pubkey": account.get("pubkey", ""),
            "emailVerified": bool(account.get("email_verified")),
            "isAdmin": False,
            "solana": solana,
            "hasPayoutAddress": bool(solana),
        }

    def _account_session_token(_env, name):
        return "session-token-for-" + str(name or "")

    class _Date:
        @staticmethod
        def now():
            return 1783000000000

    handler = _load_account_login(
        {
            "blind_index": blind_index,
            "clean_string": _clean_string,
            "d1_first": d1_first,
            "d1_all": d1_all,
            "d1_run": d1_run,
            "decrypt_row": decrypt_row,
            "_account_row": _account_row,
            "verify_password": verify_password,
            "valid_node_name": _valid_node_name,
            "totp_verify": totp_verify,
            "ed25519_verify": ed25519_verify,
            "_ts_ok": lambda value: value == DEVICE_TS,
            "valid_node_pubkey": lambda value: (
                isinstance(value, str) and len(value) == 43
            ),
            "_login_record_fail": _login_record_fail,
            "_login_locked_until": _login_locked_until,
            "_login_clear": _login_clear,
            "_save_account": _save_account,
            "_is_admin": _is_admin,
            "_account_public_payload": _account_public_payload,
            "_account_session_token": _account_session_token,
            "_admin_session_cookie": lambda _env, _name: "admin-session",
            "_clear_admin_session_cookie": lambda: "clear-admin-session",
            "json_response": _json_response,
            "Date": _Date,
            "DESKTOP_NODE_CAPABILITIES": "browse,comment,submit_issue,submit_pr,host_repo,mirror_repo,publish_repo,owner_sign",
            "CLIENT_CAPABILITIES": "browse,comment,submit_issue,submit_pr",
            "CONTRIBUTION_KEY_PROOF_CAPABILITY": "contribution_key_proof",
            "MAX_NODE_NAME": 64,
        }
    )
    return handler, saved, device_rows, login_clears, login_fails


def _active_account(**overrides):
    rec = {
        "name": "alice-node",
        "email": "alice@example.com",
        "status": "active",
        "pass_salt": "salt",
        "pass_hash": "hash",
        "pubkey": "",
        "email_verified": True,
    }
    rec.update(overrides)
    return rec


def test_login_binds_desktop_pubkey_to_unbound_password_account():
    handler, saved, devices, login_clears, login_fails = _login_harness(_active_account())

    response = asyncio.run(
        handler(
            object(),
            _Request(
                {
                    "email": "Alice@Example.com",
                    "password": "correct horse battery staple",
                    "pubkey": DEVICE_KEY,
                    "deviceTs": DEVICE_TS,
                    "deviceSig": DEVICE_SIG,
                }
            ),
        )
    )

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["pubkey"] == DEVICE_KEY
    assert saved == [("bi:alice-node", {**_active_account(), "pubkey": DEVICE_KEY})]
    assert devices and devices[0]["pubkey"] == DEVICE_KEY
    assert response["data"]["desktopCapable"] is True
    assert login_clears == ["bi:alice@example.com"]
    assert login_fails == []


def test_login_registers_additional_desktop_pubkey_without_replacing_primary():
    handler, saved, devices, _login_clears, login_fails = _login_harness(
        _active_account(pubkey=DEVICE_KEY)
    )

    response = asyncio.run(
        handler(
            object(),
            _Request(
                {
                    "email": "alice@example.com",
                    "password": "correct horse battery staple",
                    "pubkey": SECOND_DEVICE_KEY,
                    "deviceTs": DEVICE_TS,
                    "deviceSig": DEVICE_SIG,
                }
            ),
        )
    )

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["pubkey"] == DEVICE_KEY
    assert response["data"]["desktopCapable"] is False
    assert response["data"]["deviceKeyMatched"] is True
    assert response["data"]["canHost"] is False
    assert response["data"]["canPublish"] is False
    assert response["data"]["canOwnerSign"] is False
    assert saved == []
    assert devices and devices[0]["pubkey"] == SECOND_DEVICE_KEY
    assert login_fails == []


def test_login_without_desktop_pubkey_still_allows_plain_web_session():
    handler, saved, devices, _login_clears, login_fails = _login_harness(_active_account())

    response = asyncio.run(
        handler(
            object(),
            _Request(
                {
                    "email": "alice@example.com",
                    "password": "correct horse battery staple",
                }
            ),
        )
    )

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["pubkey"] == ""
    assert response["data"]["sessionKind"] == "account"
    assert response["data"]["desktopCapable"] is False
    assert saved == []
    assert login_fails == []


def test_device_bind_canonical_is_exact_and_normalizes_account_name():
    canonical = _load_device_bind_canonical({
        "valid_node_name": _valid_node_name,
        "valid_node_pubkey": lambda value: len(value) == 43,
    })

    assert canonical(" Alice-Node ", DEVICE_KEY, DEVICE_TS) == (
        "forkmesh-device-bind-v1\n"
        "alice-node\n" + DEVICE_KEY + "\n" + DEVICE_TS
    ).encode()


@pytest.mark.parametrize(
    "body_change",
    [
        {},
        {"deviceTs": DEVICE_TS, "deviceSig": "bad-signature"},
        {"deviceTs": "1", "deviceSig": DEVICE_SIG},
    ],
)
def test_login_rejects_missing_bad_or_stale_device_possession_proof(body_change):
    handler, _saved, devices, _clears, _fails = _login_harness(
        _active_account(), device_proof_valid=False
    )
    body = {
        "email": "alice@example.com",
        "password": "correct horse battery staple",
        "pubkey": DEVICE_KEY,
    }
    body.update(body_change)

    response = asyncio.run(handler(object(), _Request(body)))

    assert response == {
        "status": 401,
        "data": {"error": "device_proof_required"},
    }
    assert devices == []


def test_login_marks_valid_device_registration_as_possession_proven():
    handler, _saved, devices, _clears, _fails = _login_harness(_active_account())

    response = asyncio.run(handler(object(), _Request({
        "email": "alice@example.com",
        "password": "correct horse battery staple",
        "pubkey": DEVICE_KEY,
        "deviceTs": DEVICE_TS,
        "deviceSig": DEVICE_SIG,
    })))

    assert response["status"] == 200
    assert devices
    assert "contribution_key_proof" in devices[0]["capabilities"].split(",")


def test_login_rejects_possession_proven_cross_account_device_conflict():
    handler, _saved, devices, _clears, _fails = _login_harness(
        _active_account(), proven_users=["bi:other-user"]
    )

    response = asyncio.run(handler(object(), _Request({
        "email": "alice@example.com",
        "password": "correct horse battery staple",
        "pubkey": DEVICE_KEY,
        "deviceTs": DEVICE_TS,
        "deviceSig": DEVICE_SIG,
    })))

    assert response == {
        "status": 409,
        "data": {"error": "device_key_conflict"},
    }
    assert devices == []


def test_login_valid_proof_replaces_legacy_unproven_cross_account_claim():
    legacy = {
        "device_bi": "legacy-device",
        "account_bi": "bi:other-user",
        "pubkey": DEVICE_KEY,
        "kind": "desktop_node",
        "label": "legacy",
        "capabilities": "browse,owner_sign",
        "enabled": 1,
        "last_seen": 1,
        "revoked_at": 0,
    }
    handler, _saved, devices, _clears, _fails = _login_harness(
        _active_account(), initial_devices=[legacy]
    )

    response = asyncio.run(handler(object(), _Request({
        "email": "alice@example.com",
        "password": "correct horse battery staple",
        "pubkey": DEVICE_KEY,
        "deviceTs": DEVICE_TS,
        "deviceSig": DEVICE_SIG,
    })))

    assert response["status"] == 200
    assert len(devices) == 1
    assert devices[0]["account_bi"] == "bi:alice-node"
    assert "contribution_key_proof" in devices[0]["capabilities"].split(",")


def test_concurrent_first_device_bind_allows_exactly_one_account_winner():
    accounts = {
        "alice@example.com": _active_account(
            name="alice", email="alice@example.com"
        ),
        "bob@example.com": _active_account(
            name="bob", email="bob@example.com"
        ),
    }
    devices = []
    precheck_count = 0
    both_prechecked = asyncio.Event()

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def d1_first(_env, sql, *args):
        if "FROM users WHERE email_bi" in sql:
            email = args[0][3:]
            rec = accounts.get(email)
            return {"data": dict(rec)} if rec else None
        if "FROM nodes WHERE node_bi" in sql:
            return None
        if "FROM account_devices WHERE account_bi" in sql:
            for row in devices:
                if (row["account_bi"], row["pubkey"]) == args:
                    return dict(row)
            return None
        return None

    async def d1_all(_env, sql, *args):
        nonlocal precheck_count
        if "proven_device_users" in sql:
            snapshot = sorted({
                row["account_bi"] for row in devices
                if row["pubkey"] == args[0]
                and "contribution_key_proof" in row["capabilities"].split(",")
            })
            if precheck_count < 2:
                precheck_count += 1
                if precheck_count == 2:
                    both_prechecked.set()
                await both_prechecked.wait()
            return [{"user_bi": value} for value in snapshot]
        return []

    async def d1_run(_env, sql, *args):
        if "DELETE FROM account_devices" in sql:
            pubkey, account_bi = args
            devices[:] = [
                row for row in devices
                if not (row["pubkey"] == pubkey
                        and row["account_bi"] != account_bi
                        and "contribution_key_proof" not in
                        row["capabilities"].split(","))
            ]
            return
        if "INSERT INTO account_devices" in sql:
            if "atomic_device_proof_claim" in sql:
                target_user_bi = args[-1]
                conflict = any(
                    row["pubkey"] == args[2]
                    and "contribution_key_proof" in
                    row["capabilities"].split(",")
                    and row["account_bi"] != target_user_bi
                    for row in devices
                )
                if conflict:
                    return
            row = {
                "device_bi": args[0],
                "account_bi": args[1],
                "pubkey": args[2],
                "kind": args[3],
                "label": args[4],
                "capabilities": args[5],
                "enabled": 1,
                "last_seen": args[7],
                "revoked_at": 0,
            }
            devices.append(row)

    async def decrypt_row(_env, value):
        return dict(value)

    async def verify_password(password, _salt, _hash):
        return password == "correct horse battery staple"

    async def ed25519_verify(pubkey, signature, canonical):
        account = "alice" if signature == "sig:alice" else "bob"
        return canonical == (
            "forkmesh-device-bind-v1\n" + account + "\n" + pubkey
            + "\n" + DEVICE_TS
        ).encode()

    async def account_payload(_env, rec):
        return {"ok": True, "nodeName": rec["name"], "email": rec["email"]}

    async def noop(*_args, **_kwargs):
        return None

    handler = _load_account_login({
        "blind_index": blind_index,
        "clean_string": _clean_string,
        "d1_first": d1_first,
        "d1_all": d1_all,
        "d1_run": d1_run,
        "decrypt_row": decrypt_row,
        "verify_password": verify_password,
        "valid_node_name": _valid_node_name,
        "valid_node_pubkey": lambda value: len(value) == 43,
        "totp_verify": noop,
        "ed25519_verify": ed25519_verify,
        "_ts_ok": lambda value: value == DEVICE_TS,
        "_login_record_fail": noop,
        "_login_locked_until": lambda *_args: asyncio.sleep(0, result=0),
        "_login_clear": noop,
        "_save_account": noop,
        "touch_registered_node": noop,
        "_account_public_payload": account_payload,
        "_account_session_token": lambda _env, name: "session:" + name,
        "_admin_session_cookie": lambda _env, _name: "admin",
        "_clear_admin_session_cookie": lambda: "clear",
        "json_response": _json_response,
        "Date": type("Date", (), {"now": staticmethod(lambda: int(DEVICE_TS))}),
        "DESKTOP_NODE_CAPABILITIES": "browse,owner_sign",
        "CLIENT_CAPABILITIES": "browse",
        "CONTRIBUTION_KEY_PROOF_CAPABILITY": "contribution_key_proof",
        "MAX_NODE_NAME": 64,
    })

    async def bind(email, account):
        return await handler(object(), _Request({
            "email": email,
            "password": "correct horse battery staple",
            "pubkey": DEVICE_KEY,
            "deviceTs": DEVICE_TS,
            "deviceSig": "sig:" + account,
        }))

    async def scenario():
        return await asyncio.gather(
            bind("alice@example.com", "alice"),
            bind("bob@example.com", "bob"),
        )

    results = asyncio.run(scenario())

    assert sorted(result["status"] for result in results) == [200, 409]
    assert len(devices) == 1


def test_device_proof_claim_sql_conditionally_rejects_a_second_owner():
    database = sqlite3.connect(":memory:")
    database.executescript(
        """
        CREATE TABLE nodes (
            node_bi TEXT PRIMARY KEY,
            user_bi TEXT,
            pubkey TEXT
        );
        CREATE TABLE account_devices (
            device_bi TEXT PRIMARY KEY,
            account_bi TEXT NOT NULL,
            pubkey TEXT NOT NULL,
            kind TEXT NOT NULL,
            label TEXT,
            capabilities TEXT NOT NULL,
            enabled INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            last_seen INTEGER NOT NULL,
            revoked_at INTEGER NOT NULL
        );
        """
    )

    async def blind_index(_env, value):
        return "bi:" + value

    async def d1_run(_env, sql, *args):
        database.execute(sql, args)
        database.commit()

    date = type("Date", (), {"now": staticmethod(lambda: int(DEVICE_TS))})
    register = _load_account_device_register({
        "blind_index": blind_index,
        "d1_run": d1_run,
        "Date": date,
        "DESKTOP_NODE_CAPABILITIES": "browse,owner_sign",
        "CLIENT_CAPABILITIES": "browse",
        "CONTRIBUTION_KEY_PROOF_CAPABILITY": "contribution_key_proof",
    })

    asyncio.run(register(
        object(), "bi:alice", DEVICE_KEY,
        proof_verified=True, target_user_bi="bi:alice",
    ))
    asyncio.run(register(
        object(), "bi:bob", DEVICE_KEY,
        proof_verified=True, target_user_bi="bi:bob",
    ))

    claims = database.execute(
        "SELECT account_bi, pubkey FROM account_devices"
    ).fetchall()
    assert claims == [("bi:alice", DEVICE_KEY)]


def test_qt_login_sends_desktop_pubkey_to_worker():
    qt = QT_MAIN.read_text(encoding="utf-8")
    login = qt[qt.index("bool MainWindow::verifyTotpLogin") : qt.index("bool MainWindow::runLoginFlow")]

    assert "ForkMeshIdentity::deviceBindCanonical(" in login
    assert "accountName, publicKey, deviceTs" in login
    assert 'loginRequest.insert(QStringLiteral("pubkey"), publicKey)' in login
    assert 'loginRequest.insert(QStringLiteral("deviceTs"), deviceTs)' in login
    assert 'loginRequest.insert(QStringLiteral("deviceSig"), deviceSig)' in login
    assert 'resp.value(QStringLiteral("deviceKeyMatched")).toBool(false)' in login
    assert 'resp.value(QStringLiteral("desktopCapable")).toBool(false)' in login


def test_qt_silent_auth_does_not_treat_cached_password_as_signed_hosting_auth():
    qt = QT_MAIN.read_text(encoding="utf-8")
    silent_auth = qt[
        qt.index("bool MainWindow::authenticateSilently")
        : qt.index("// POST /api/accounts/login")
    ]

    # Cached markers are only trusted when the relay gave no authoritative
    # 200 answer (unreachable, rate-limited, erroring). An answered lookup
    # must prove the desktop pubkey above to authenticate.
    assert "if (cachedHere && status != 200 &&" in silent_auth
    assert "restoreDesktopCapability(accountName)" in silent_auth
    assert "cachedHere && (status == 0 || activeAccount)" not in silent_auth


def test_qt_login_explains_desktop_key_mismatch():
    qt = QT_MAIN.read_text(encoding="utf-8")

    assert 'err == "pubkey_mismatch"' in qt
    assert "already bound to another" in qt


def test_signed_private_publish_and_direct_routing_stay_account_key_bound():
    entry = ENTRY.read_text(encoding="utf-8")
    # Catalog publication and direct-route registration live in split
    # MainWindow TUs. The retired repository socket has no credential.
    qt = "\n".join(
        p.read_text(encoding="utf-8")
        for p in sorted(QT_MAIN.parent.glob("MainWindow*.cpp"))
    )

    assert 'primary_owner_pub = (publish_owner_rec.get("pubkey", "")' in entry
    assert 'return json_response({"error": "account_required"}, status=403)' in entry
    assert "owner_pub = await _catalog_publication_key(" in entry
    assert "allowed = await _owner_signing_pubkeys(env, owner)" in entry
    assert "return maintainer if maintainer in allowed else" in entry
    assert 'canonical = ("forkmesh-catalog-v2\\n" + record_hash).encode()' in entry
    assert 'metadata.insert(QStringLiteral("catalogSigVersion"), 2);' in qt
    assert "forkmesh::control::catalogV2SigningPayload(" in qt
    assert 'url.setPath(QStringLiteral("/api/mirrors/private"));' in qt
    assert "forkmesh::control::privateReplicaRouteSigningPayload(" in qt
    assert 'HTTPS_MIRROR_ENDPOINT_PATH = "/api/mirrors/https"' in entry
    assert "registration[\"publicKey\"] not in allowed_keys" in entry
    assert "FROM mirror_https_endpoints e " in entry
    assert '"direct_https_receive_pack_required"' in entry
    assert '{"maintainer", m_profileIdentity.publicKey()}' in qt
    assert "per-repository persistent socket is retired" in qt
    assert "forkmesh-host-v1" not in qt


def test_contribution_actor_resolution_accepts_only_enabled_nonrevoked_devices():
    async def d1_all(_env, sql, *args):
        if "json_each" in sql:
            requested = json.loads(args[0])
            return ([{"pubkey": "enabled-key", "user_bi": "user:alice"}]
                    if "enabled-key" in requested else [])
        if "FROM nodes WHERE pubkey IN" in sql:
            return []
        if "FROM account_devices" in sql:
            return [
                {
                    "account_bi": "user:alice",
                    "pubkey": "enabled-key",
                    "enabled": 1,
                    "revoked_at": 0,
                }
            ] if "enabled-key" in args else []
        if "FROM nodes WHERE node_bi IN" in sql:
            return []
        return []

    resolver = _load_contribution_actor_resolver({"d1_all": d1_all})

    resolved = asyncio.run(resolver(
        object(), ["enabled-key", "disabled-key", "revoked-key"]
    ))

    assert resolved == {"enabled-key": "user:alice"}
