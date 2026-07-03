#!/usr/bin/env python3
"""Login-to-desktop-key binding contract checks."""

import ast
import asyncio
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
QT_MAIN = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"


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
        }
    ]
    assert selected, "missing _account_login"
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["_account_login"]


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


def _login_harness(rec):
    saved = []
    device_rows = []
    login_clears = []
    login_fails = []

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def d1_first(_env, sql, *args):
        if "FROM accounts WHERE email_bi" in sql:
            return {"data": "encrypted"} if args == ("bi:alice@example.com",) else None
        if "FROM account_devices" in sql:
            for row in device_rows:
                if args == (row["account_bi"], row["pubkey"]):
                    return dict(row)
        return None

    async def d1_all(_env, sql, *args):
        if "FROM account_devices" in sql:
            return [dict(row) for row in device_rows if args == (row["account_bi"],)]
        return []

    async def d1_run(_env, sql, *args):
        if "INSERT INTO account_devices" in sql:
            device_rows.append({
                "device_bi": args[0],
                "account_bi": args[1],
                "pubkey": args[2],
                "kind": args[3],
                "label": args[4],
                "capabilities": args[5],
                "enabled": 1,
                "last_seen": args[7],
                "revoked_at": 0,
            })

    async def decrypt_row(_env, _data):
        return dict(rec)

    async def _account_row(_env, name):
        return "bi:" + name, dict(rec)

    async def verify_password(password, _salt, _hash):
        return password == "correct horse battery staple"

    async def totp_verify(_secret, _totp):
        return True

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
            "_login_record_fail": _login_record_fail,
            "_login_locked_until": _login_locked_until,
            "_login_clear": _login_clear,
            "_save_account": _save_account,
            "_is_admin": _is_admin,
            "_account_public_payload": _account_public_payload,
            "json_response": _json_response,
            "Date": _Date,
            "DESKTOP_NODE_CAPABILITIES": "browse,comment,submit_issue,submit_pr,host_repo,mirror_repo,publish_repo,owner_sign",
            "CLIENT_CAPABILITIES": "browse,comment,submit_issue,submit_pr",
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
                    "pubkey": "desktop-pubkey",
                }
            ),
        )
    )

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["pubkey"] == "desktop-pubkey"
    assert saved == [("bi:alice-node", {**_active_account(), "pubkey": "desktop-pubkey"})]
    assert devices and devices[0]["pubkey"] == "desktop-pubkey"
    assert response["data"]["desktopCapable"] is True
    assert login_clears == ["bi:alice@example.com"]
    assert login_fails == []


def test_login_registers_additional_desktop_pubkey_without_replacing_primary():
    handler, saved, devices, _login_clears, login_fails = _login_harness(
        _active_account(pubkey="existing-pubkey")
    )

    response = asyncio.run(
        handler(
            object(),
            _Request(
                {
                    "email": "alice@example.com",
                    "password": "correct horse battery staple",
                    "pubkey": "different-pubkey",
                }
            ),
        )
    )

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["pubkey"] == "existing-pubkey"
    assert response["data"]["desktopCapable"] is True
    assert response["data"]["deviceKeyMatched"] is True
    assert saved == []
    assert devices and devices[0]["pubkey"] == "different-pubkey"
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


def test_qt_login_sends_desktop_pubkey_to_worker():
    qt = QT_MAIN.read_text(encoding="utf-8")
    login = qt[qt.index("bool MainWindow::verifyTotpLogin") : qt.index("bool MainWindow::runLoginFlow")]

    assert '{"pubkey", m_profileIdentity.publicKey()}' in login


def test_qt_silent_auth_does_not_treat_cached_password_as_signed_hosting_auth():
    qt = QT_MAIN.read_text(encoding="utf-8")
    silent_auth = qt[
        qt.index("bool MainWindow::authenticateSilently")
        : qt.index("// POST /api/accounts/login")
    ]

    # Cached markers are only trusted when the relay gave no authoritative
    # 200 answer (unreachable, rate-limited, erroring). An answered lookup
    # must prove the desktop pubkey above to authenticate.
    assert "if (cachedHere && status != 200)" in silent_auth
    assert "cachedHere && (status == 0 || activeAccount)" not in silent_auth


def test_qt_login_explains_desktop_key_mismatch():
    qt = QT_MAIN.read_text(encoding="utf-8")

    assert 'err == "pubkey_mismatch"' in qt
    assert "already bound to another" in qt


def test_signed_publish_and_hosting_stay_account_key_bound():
    entry = ENTRY.read_text(encoding="utf-8")
    # Catalog publish + host-token signing moved into split MainWindow TUs.
    qt = "\n".join(
        p.read_text(encoding="utf-8")
        for p in sorted(QT_MAIN.parent.glob("MainWindow*.cpp"))
    )

    assert "owner_pub = await _owner_pubkey(env, owner)" in entry
    assert 'return json_response({"error": "account_required"}, status=403)' in entry
    assert 'record["maintainer"] != owner_pub' in entry
    assert '"forkmesh-host-v1\\n" + owner + "\\n" + repo + "\\n" + str(ts)' in entry
    assert '{"maintainer", m_profileIdentity.publicKey()}' in qt
    assert '"forkmesh-catalog-v1\\n" + owner + "\\n" + name + "\\n" + updatedAt' in qt
    assert '"forkmesh-host-v1\\n" + tokenOwner + "\\n" + tokenRepo + "\\n" + ts' in qt
