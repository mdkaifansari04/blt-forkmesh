#!/usr/bin/env python3
"""SSH public-key management and node-side Git gateway coverage."""

import ast
import asyncio
import base64
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = ROOT.parent
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
SCHEMA_TEXT = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")


def _module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ssh_keys = _module(ROOT / "src" / "ssh_keys.py", "forkmesh_ssh_keys")
ssh_gateway = _module(PROJECT_ROOT / "tools" / "ssh_gateway.py",
                      "forkmesh_ssh_gateway")


def _ssh_string(value):
    value = bytes(value)
    return len(value).to_bytes(4, "big") + value


def _key_line(key_type="ssh-ed25519", material=None, comment="laptop"):
    if material is None:
        material = b"\x42" * 32
    blob = _ssh_string(key_type.encode()) + _ssh_string(material)
    return "%s %s %s" % (
        key_type, base64.b64encode(blob).decode(), comment)


def _rsa_line(bits):
    modulus = b"\x00\x80" + b"\x00" * ((bits // 8) - 1)
    blob = (
        _ssh_string(b"ssh-rsa")
        + _ssh_string(b"\x01\x00\x01")
        + _ssh_string(modulus)
    )
    return "ssh-rsa " + base64.b64encode(blob).decode()


def _load_entry(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    assert found == set(names), "missing: %s" % sorted(set(names) - found)
    namespace = dict(extra_globals or {})
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=selected, type_ignores=[])), str(ENTRY), "exec"),
         namespace)
    return namespace


def _run(coro):
    return asyncio.run(coro)


def test_public_key_parser_normalizes_fingerprints_and_rejects_injection():
    parsed = ssh_keys.parse_public_key(_key_line())
    assert parsed["keyType"] == "ssh-ed25519"
    assert parsed["publicKey"].count(" ") == 1
    assert parsed["comment"] == "laptop"
    assert parsed["fingerprint"].startswith("SHA256:")
    assert ssh_keys.parse_public_key(parsed["publicKey"])["fingerprint"] == \
        parsed["fingerprint"]

    for value, error in (
        ("command=\"sh\" " + _key_line(), "unsupported_key_type"),
        (_key_line() + "\n" + _key_line(comment="second"),
         "one_public_key_required"),
        ("ssh-dss AAAA", "unsupported_key_type"),
    ):
        with pytest.raises(ssh_keys.SshPublicKeyError, match=error):
            ssh_keys.parse_public_key(value)


def test_public_key_parser_requires_strong_rsa_and_matching_wire_type():
    assert ssh_keys.parse_public_key(_rsa_line(3072))["keyType"] == "ssh-rsa"
    with pytest.raises(ssh_keys.SshPublicKeyError, match="rsa_key_too_small"):
        ssh_keys.parse_public_key(_rsa_line(2048))
    mismatched = _key_line().replace("ssh-ed25519 ", "ssh-rsa ", 1)
    with pytest.raises(ssh_keys.SshPublicKeyError, match="key_type_mismatch"):
        ssh_keys.parse_public_key(mismatched)


def test_ssh_url_is_published_only_for_valid_gateway_configuration():
    assert ssh_keys.ssh_repository_url(
        "ssh.example.org", 22, "alice-node", "widget"
    ) == "ssh://git@ssh.example.org/alice-node/widget.git"
    assert ssh_keys.ssh_repository_url(
        "2001:db8::1", 2222, "alice-node", "widget"
    ) == "ssh://git@[2001:db8::1]:2222/alice-node/widget.git"
    for host in ("", "https://ssh.example.org", "git@ssh.example.org", "bad host"):
        assert ssh_keys.ssh_repository_url(
            host, 22, "alice-node", "widget") == ""


def test_gateway_token_and_repository_allowlist_fail_closed():
    assert ssh_keys.configured_gateway_token("t" * 32) == "t" * 32
    for token in (
        "t" * 31,
        " " + "t" * 32,
        "t" * 32 + "\n",
        "🔑" * 32,
        "t" * 31 + "/",
    ):
        assert ssh_keys.configured_gateway_token(token) == ""

    repositories = ssh_keys.parse_gateway_repository_allowlist(
        "alice-node/widget=read-write,alice-node/docs=ro")
    assert repositories == {
        "alice-node/widget": "read-write",
        "alice-node/docs": "read-only",
    }
    assert ssh_keys.gateway_repository_access(
        repositories, "alice-node", "Widget") == "read-write"
    for invalid in (
        "",
        "disabled",
        "alice-node/widget",
        "alice-node/widget=admin",
        "alice-node/widget=read-only,alice-node/widget=read-write",
        "../widget=read-write",
    ):
        assert ssh_keys.parse_gateway_repository_allowlist(invalid) == {}


def test_worker_publishes_url_only_for_explicit_gateway_repo():
    namespace = _load_entry(
        "_ssh_gateway_settings",
        "_ssh_repository_url",
        extra_globals={"ssh_auth": ssh_keys},
    )
    env = SimpleNamespace(
        SSH_GATEWAY_HOST="ssh.example.org",
        SSH_GATEWAY_PORT="22",
        SSH_GATEWAY_TOKEN="t" * 48,
        SSH_GATEWAY_REPOSITORIES="alice-node/widget=read-write",
    )
    assert namespace["_ssh_repository_url"](
        env, "alice-node", "widget"
    ) == "ssh://git@ssh.example.org/alice-node/widget.git"
    assert namespace["_ssh_repository_url"](
        env, "alice-node", "not-hosted"
    ) == ""
    env.SSH_GATEWAY_REPOSITORIES = ""
    assert namespace["_ssh_repository_url"](
        env, "alice-node", "widget"
    ) == ""


@pytest.mark.parametrize(
    "command",
    [
        "/opt/forkmesh/gateway --config /tmp/x",
        "/opt/forkmesh/gateway;sh",
        "/opt/forkmesh/$(id)",
        "/opt/forkmesh/`id`",
        "/opt/forkmesh/../gateway",
        "relative/gateway",
    ],
)
def test_forced_authorized_key_rejects_shell_command_injection(command):
    with pytest.raises(ValueError, match="invalid_gateway_executable"):
        ssh_keys.forced_authorized_key_line(
            _key_line(), "sk_" + "a" * 24, command)


def _gateway_config(tmp_path, **overrides):
    root = tmp_path / "repos"
    root.mkdir(parents=True)
    token = tmp_path / "gateway.token"
    token.write_text("t" * 48, encoding="utf-8")
    token.chmod(0o600)
    config = {
        "schemaVersion": 1,
        "apiOrigin": "https://forkmesh.example",
        "gatewayExecutable": "/opt/forkmesh/ssh_gateway.py",
        "gatewayTokenFile": str(token),
        "repositoryRoot": str(root),
        "repositories": [{
            "owner": "alice-node",
            "name": "widget",
            "path": "alice-node/widget.git",
            "access": "read-write",
        }],
    }
    config.update(overrides)
    path = tmp_path / "gateway.json"
    path.write_text(json.dumps(config), encoding="utf-8")
    path.chmod(0o644)
    return path, root


def test_gateway_config_rejects_symlink_writable_bad_port_and_token(
    tmp_path, monkeypatch
):
    path, _root = _gateway_config(tmp_path)
    assert ssh_gateway.Config(path).api_origin == "https://forkmesh.example"

    path.chmod(0o666)
    with pytest.raises(ssh_gateway.GatewayError,
                       match="invalid gateway configuration"):
        ssh_gateway.Config(path)
    path.chmod(0o644)

    symlink = tmp_path / "linked.json"
    symlink.symlink_to(path)
    with pytest.raises(ssh_gateway.GatewayError,
                       match="invalid gateway configuration"):
        ssh_gateway.Config(symlink)

    path.write_text(json.dumps({
        **json.loads(path.read_text()),
        "apiOrigin": "https://forkmesh.example:99999",
    }), encoding="utf-8")
    with pytest.raises(ssh_gateway.GatewayError,
                       match="invalid Worker API origin"):
        ssh_gateway.Config(path)

    path, _root = _gateway_config(tmp_path / "unicode-token")
    monkeypatch.setenv("FORKMESH_SSH_GATEWAY_TOKEN", "🔑" * 32)
    with pytest.raises(ssh_gateway.GatewayError,
                       match="gateway token is unavailable"):
        ssh_gateway.Config(path)


def test_gateway_parses_only_exact_git_commands():
    assert ssh_gateway._parse_original_command(
        "git-receive-pack 'alice-node/widget.git'"
    ) == ("git-receive-pack", "alice-node", "widget")
    assert ssh_gateway._parse_original_command(
        "git-upload-pack '/alice-node/widget.git'"
    ) == ("git-upload-pack", "alice-node", "widget")
    for command in (
        "bash",
        "git-receive-pack '../../etc'",
        "git-receive-pack 'alice-node/widget.git' extra",
        "git-receive-pack 'alice-node/widget.git;id'",
    ):
        with pytest.raises(ssh_gateway.GatewayError):
            ssh_gateway._parse_original_command(command)


def test_gateway_execs_git_without_shell_after_worker_authorization(monkeypatch):
    repo_path = Path("/srv/forkmesh/git/alice-node/widget.git")
    config = SimpleNamespace(
        repositories={("alice-node", "widget"): (repo_path, True)},
        repository_root=Path("/srv/forkmesh/git"))
    monkeypatch.setenv(
        "SSH_ORIGINAL_COMMAND",
        "git-receive-pack 'alice-node/widget.git'")
    monkeypatch.setattr(ssh_gateway, "_api", lambda _config, payload: {
        "authorized": True,
        "owner": payload["owner"],
        "repository": payload["repository"],
    })
    monkeypatch.setattr(ssh_gateway, "_bare_repository", lambda path: True)
    monkeypatch.setattr(
        Path, "resolve",
        lambda self, strict=False: self)
    captured = {}

    def fake_exec(file, args, env):
        captured.update(file=file, args=args, env=env)
        raise RuntimeError("exec captured")

    monkeypatch.setattr(ssh_gateway.os, "execvpe", fake_exec)
    with pytest.raises(RuntimeError, match="exec captured"):
        ssh_gateway._serve(config, "sk_" + "a" * 24)
    assert captured["file"] == "git"
    assert captured["args"] == ["git", "receive-pack", str(repo_path)]
    assert "SSH_ORIGINAL_COMMAND" not in captured["env"]


def test_gateway_refuses_receive_pack_for_local_read_only_repo(monkeypatch):
    config = SimpleNamespace(repositories={
        ("alice-node", "widget"): (Path("/safe/widget.git"), False)},
        repository_root=Path("/safe"))
    monkeypatch.setenv(
        "SSH_ORIGINAL_COMMAND",
        "git-receive-pack 'alice-node/widget.git'")
    monkeypatch.setattr(ssh_gateway, "_api", lambda _config, payload: {
        "authorized": True,
        "owner": payload["owner"],
        "repository": payload["repository"],
    })
    with pytest.raises(ssh_gateway.GatewayError, match="read-only"):
        ssh_gateway._serve(config, "sk_" + "a" * 24)


def test_gateway_prints_worker_allowlist_from_same_local_configuration(
    tmp_path, capsys
):
    path, _root = _gateway_config(tmp_path)
    config = ssh_gateway.Config(path)
    assert ssh_gateway._worker_allowlist(config) == 0
    assert capsys.readouterr().out.strip() == \
        "alice-node/widget=read-write"


def test_schema_and_migration_define_revocable_encrypted_key_records():
    migration = ROOT / "migrations" / "0067_account_ssh_keys.sql"
    assert migration.exists()
    sql = migration.read_text(encoding="utf-8")
    for source in (SCHEMA_TEXT, sql):
        assert "CREATE TABLE IF NOT EXISTS account_ssh_keys" in source
        assert "key_bi TEXT NOT NULL UNIQUE" in source
        assert "data TEXT NOT NULL" in source
        assert "revoked_at INTEGER NOT NULL DEFAULT 0" in source
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(sql)
        connection.execute(
            "INSERT INTO account_ssh_keys "
            "(key_id,account_bi,key_bi,key_type,fingerprint,data,created_at) "
            "VALUES (?,?,?,?,?,?,?)",
            ("sk_one", "account", "key", "ssh-ed25519", "SHA256:x",
             "encrypted", 1),
        )
        with pytest.raises(sqlite3.IntegrityError):
            connection.execute(
                "INSERT INTO account_ssh_keys "
                "(key_id,account_bi,key_bi,key_type,fingerprint,data,created_at) "
                "VALUES (?,?,?,?,?,?,?)",
                ("sk_two", "other", "key", "ssh-ed25519", "SHA256:x",
                 "encrypted", 2),
            )
    finally:
        connection.close()


def _response(payload, status=200, **kwargs):
    return {"payload": payload, "status": status, **kwargs}


class _Request:
    def __init__(self, payload, method="POST"):
        self.payload = payload
        self.method = method


def _authorization_handler(
    *, owns=False, org_write=False, gateway_access="read-write",
    authoritative_private=True, visibility="private"
):
    audits = []

    async def ensure_schema(_env):
        return None

    async def body(request):
        return request.payload, ""

    async def gateway_record(_env, key_id="", public_key=""):
        return {"key_id": key_id}, {"account": "alice"}

    async def d1_first(_env, sql, *params):
        assert "FROM repositories" in sql
        return {
            "is_private": 1 if authoritative_private else 0,
            "data": "encrypted",
        }

    async def audit(_env, actor, action, target_type="", target="",
                    outcome="success", details=None):
        audits.append((actor, action, outcome, details or {}))

    async def d1_run(*_args):
        return None

    namespace = _load_entry(
        "ssh_gateway_authorize_handler",
        extra_globals={
            "ensure_schema": ensure_schema,
            "method_name": lambda request: "POST",
            "_ssh_gateway_token_ok": lambda env, request: True,
            "_ssh_json_body": body,
            "clean_string": lambda value, limit: str(value or "")[:limit],
            "MAX_NODE_NAME": 64,
            "safe_segment": lambda value: (
                str(value) if str(value).replace("-", "").replace(
                    "_", "").replace(".", "").isalnum() else ""),
            "_ssh_key_gateway_record": gateway_record,
            "_org_repo_node": lambda *args: _async_value(""),
            "_ssh_gateway_repository_access": (
                lambda *args: gateway_access),
            "blind_index": lambda *args: _async_value("repo-bi"),
            "d1_first": d1_first,
            "decrypt_row": lambda *args: _async_value({
                "owner": "alice-node", "name": "widget",
                "visibility": visibility,
            }),
            "_catalog_record_matches_identity": (
                lambda record, owner, repo:
                record["owner"] == owner and record["name"] == repo),
            "_account_owns_node": lambda *args: _async_value(owns),
            "_org_write_allowed": lambda *args: _async_value(org_write),
            "_repo_shared_with": lambda *args: _async_value(False),
            "_ssh_org_read_allowed": lambda *args: _async_value(False),
            "_audit_sensitive_action": audit,
            "d1_run": d1_run,
            "Date": SimpleNamespace(now=lambda: 1000),
            "json_response": _response,
            "re": __import__("re"),
        },
    )
    return namespace["ssh_gateway_authorize_handler"], audits


async def _async_value(value):
    return value


def _key_management_handler(
    method, *, existing=None, account_kind="user", listed_rows=None
):
    audits = []
    writes = []
    key_line = _key_line()

    async def session(*_args):
        return "bi:alice", {
            "name": "alice", "kind": account_kind, "status": "active"}

    async def d1_first(_env, sql, *params):
        if "WHERE key_bi=?" in sql:
            return existing
        if "COUNT(*)" in sql:
            return {"n": 0}
        if "WHERE key_id=? AND account_bi=?" in sql:
            return {"key_id": params[0]} if existing is not False else None
        raise AssertionError(sql)

    async def d1_run(_env, sql, *params):
        writes.append((sql, params))

    async def audit(_env, actor, action, target_type="", target="",
                    outcome="success", details=None):
        audits.append((actor, action, target_type, target, outcome, details or {}))

    namespace = _load_entry(
        "_ssh_key_public_projection",
        "ssh_keys_handler",
        extra_globals={
            "ensure_schema": lambda env: _async_value(None),
            "method_name": lambda request: request.method,
            "_ssh_json_body": lambda request: _async_value((request.payload, "")),
            "_account_session_record": session,
            "clean_string": lambda value, limit: str(value or "")[:limit],
            "MAX_NODE_NAME": 64,
            "_account_kind": lambda record: record.get("kind"),
            "json_response": _response,
            "d1_all": lambda *args: _async_value(listed_rows or []),
            "decrypt_row": lambda *args: _async_value({
                "label": "Work laptop",
                "publicKey": key_line,
            }),
            "ssh_auth": ssh_keys,
            "blind_index": lambda *args: _async_value("bi:key"),
            "d1_first": d1_first,
            "_b64url_encode": lambda value: "a" * 24,
            "_random_bytes": lambda length: b"x" * length,
            "Date": SimpleNamespace(now=lambda: 1000),
            "encrypt_row": lambda env, data: _async_value("encrypted"),
            "d1_run": d1_run,
            "_audit_sensitive_action": audit,
            "re": __import__("re"),
            "_ssh_gateway_settings": lambda env: {
                "configured": False, "host": "", "port": 0},
        },
    )
    request = _Request(
        {"publicKey": key_line, "label": "Laptop"}, method=method)
    return namespace["ssh_keys_handler"], request, audits, writes


def test_account_key_registration_and_revocation_are_session_gated_and_audited():
    handler, request, audits, writes = _key_management_handler("POST")
    response = _run(handler(None, request))
    assert response["status"] == 201
    assert response["payload"]["privateKeysStored"] is False
    assert response["payload"]["key"]["fingerprint"].startswith("SHA256:")
    assert any("INSERT INTO account_ssh_keys" in sql for sql, _ in writes)
    assert audits[-1][1:5] == (
        "ssh_key.register", "ssh_public_key", "sk_" + "a" * 24, "success")

    handler, request, audits, writes = _key_management_handler(
        "DELETE", existing=True)
    response = _run(handler(None, request, "sk_" + "a" * 24))
    assert response["status"] == 200
    assert response["payload"]["revoked"] is True
    assert any("SET revoked_at=?" in sql for sql, _ in writes)
    assert audits[-1][1:5] == (
        "ssh_key.revoke", "ssh_public_key", "sk_" + "a" * 24, "success")


def test_account_key_list_is_owner_only_and_never_returns_public_key_material():
    row = {
        "key_id": "sk_" + "a" * 24,
        "key_type": "ssh-ed25519",
        "fingerprint": "SHA256:example",
        "data": "encrypted",
        "created_at": 100,
        "last_used_at": 200,
    }
    handler, request, _audits, _writes = _key_management_handler(
        "GET", listed_rows=[row])
    response = _run(handler(None, request))
    assert response["status"] == 200
    assert response["payload"]["privateKeysStored"] is False
    assert response["payload"]["keys"] == [{
        "id": row["key_id"],
        "label": "Work laptop",
        "keyType": "ssh-ed25519",
        "fingerprint": "SHA256:example",
        "createdAt": 100,
        "lastUsedAt": 200,
    }]
    assert "publicKey" not in json.dumps(response["payload"])

    handler, request, _audits, _writes = _key_management_handler(
        "GET", account_kind="node")
    response = _run(handler(None, request))
    assert response["status"] == 401
    assert response["payload"] == {"error": "invalid_session"}


def test_duplicate_key_registration_does_not_disclose_its_owner_or_state():
    handler, request, _audits, writes = _key_management_handler(
        "POST", existing={"account_bi": "bi:someone", "revoked_at": 100})
    response = _run(handler(None, request))
    assert response["status"] == 409
    assert response["payload"] == {"error": "ssh_key_already_registered"}
    assert writes == []


def test_worker_gateway_maps_key_to_existing_owner_and_team_write_permissions():
    payload = {
        "action": "authorize",
        "keyId": "sk_" + "a" * 24,
        "owner": "alice-node",
        "repository": "widget",
        "operation": "git-receive-pack",
    }
    handler, audits = _authorization_handler(owns=True)
    response = _run(handler(None, _Request(payload)))
    assert response["status"] == 200
    assert response["payload"]["authorized"] is True
    assert response["payload"]["principal"] == "alice"
    assert response["payload"]["repositoryRelativePath"] == \
        "alice-node/widget.git"
    assert audits[-1][2] == "success"

    handler, audits = _authorization_handler(org_write=True)
    response = _run(handler(None, _Request(payload)))
    assert response["status"] == 200
    assert response["payload"]["authorized"] is True

    handler, audits = _authorization_handler()
    response = _run(handler(None, _Request(payload)))
    assert response["status"] == 403
    assert response["payload"] == {"authorized": False}
    assert audits[-1][2] == "denied"


def test_worker_gateway_public_read_requires_both_public_indicators():
    payload = {
        "action": "authorize",
        "keyId": "sk_" + "a" * 24,
        "owner": "alice-node",
        "repository": "widget",
        "operation": "git-upload-pack",
    }
    handler, _ = _authorization_handler(
        authoritative_private=False, visibility="public")
    assert _run(handler(None, _Request(payload)))["status"] == 200

    # Either disagreement fails closed and requires an owner/share/org ACL.
    for private_flag, visibility in (
        (True, "public"),
        (False, "private"),
    ):
        handler, _ = _authorization_handler(
            authoritative_private=private_flag, visibility=visibility)
        response = _run(handler(None, _Request(payload)))
        assert response["status"] == 403
        assert response["payload"] == {"authorized": False}


def test_worker_gateway_requires_repo_allowlist_and_write_mode():
    payload = {
        "action": "authorize",
        "keyId": "sk_" + "a" * 24,
        "owner": "alice-node",
        "repository": "widget",
        "operation": "git-receive-pack",
    }
    for access in ("", "read-only"):
        handler, audits = _authorization_handler(
            owns=True, gateway_access=access)
        response = _run(handler(None, _Request(payload)))
        assert response["status"] == 403
        assert response["payload"] == {"authorized": False}
        assert audits[-1][2] == "denied"


def test_renamed_account_is_resolved_from_current_account_index():
    line = _key_line(comment="")
    row = {
        "key_id": "sk_" + "a" * 24,
        "account_bi": "bi:new-name",
        "data": "encrypted",
    }

    async def d1_first(_env, sql, *params):
        assert "WHERE key_id=?" in sql
        return row

    namespace = _load_entry(
        "_ssh_key_gateway_record",
        extra_globals={
            "d1_first": d1_first,
            "decrypt_row": lambda *args: _async_value({
                "account": "old-name",
                "publicKey": line,
            }),
            "ssh_auth": ssh_keys,
            "_account_identity_rec_by_bi": lambda env, account_bi: _async_value({
                "name": "new-name", "status": "active", "kind": "user",
            }),
            "clean_string": lambda value, limit: str(value or "")[:limit],
            "MAX_NODE_NAME": 64,
            "_account_kind": lambda record: record.get("kind"),
            "blind_index": lambda *args: _async_value("unused"),
            "hmac": __import__("hmac"),
        },
    )
    found, data = _run(namespace["_ssh_key_gateway_record"](
        None, key_id=row["key_id"]))
    assert found == row
    assert data["account"] == "new-name"
    # The production helper resolves the principal from row.account_bi, not
    # from the historical registration-time value, and overwrites the returned
    # authorization context with the current name.
    function_source = ast.get_source_segment(
        ENTRY_TEXT,
        next(node for node in ast.parse(ENTRY_TEXT).body
             if isinstance(node, ast.AsyncFunctionDef)
             and node.name == "_ssh_key_gateway_record"),
    )
    assert "_account_identity_rec_by_bi" in function_source


def test_routes_frontend_and_deploy_contract_are_wired():
    assert '"/api/ssh/authorize"' in ENTRY_TEXT
    assert '"/api/accounts/ssh-keys"' in ENTRY_TEXT
    assert 'rec["sshUrl"]' in ENTRY_TEXT
    assert "node-side-forced-command" in ENTRY_TEXT
    settings = (
        ROOT / "public" / "dashboard" / "partials" / "views" / "settings.html"
    ).read_text(encoding="utf-8")
    account_js = (
        ROOT / "public" / "dashboard" / "js" / "04-account.js"
    ).read_text(encoding="utf-8")
    repo_js = (
        ROOT / "public" / "dashboard" / "js" / "08-repo-detail-network.js"
    ).read_text(encoding="utf-8")
    assert 'data-settings-section="ssh-keys"' in settings
    assert "data-ssh-public-key" in settings
    assert "/api/accounts/ssh-keys" in account_js
    assert "data-repo-ssh-url" in repo_js
    deploy = (PROJECT_ROOT / ".forkmesh" / "deploy.yml").read_text(
        encoding="utf-8")
    for name in (
        "SSH_GATEWAY_HOST",
        "SSH_GATEWAY_PORT",
        "SSH_GATEWAY_TOKEN",
        "SSH_GATEWAY_REPOSITORIES",
    ):
        assert "${{ vars.%s }}" % name in deploy
