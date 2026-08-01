"""Deleting a repository from the website has to stay deleted (adhoc #91).

The owner's node keeps its local mirror and republishes the catalog record on
every heartbeat, so DELETE /api/repositories used to drop the row only for as
long as it took the node to publish again — from the website it looked like the
delete button did nothing. The delete now writes a tombstone that refuses those
automatic republishes; only an explicitly user-initiated publish from the
desktop ("publishIntent":"user", still owner-signature-gated) lifts it.
"""

import ast
import asyncio
import hashlib
import json
import re
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import parse_qs, urlparse

import pytest

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"

NOW = 1_700_000_000_000
WANTED = (
    "catalog_handler",
    "_repo_delete_tombstone_active",
    "_record_repo_delete_tombstone",
    "_clear_repo_delete_tombstone",
)


def _run(coro):
    return asyncio.get_event_loop_policy().new_event_loop().run_until_complete(coro)


class _Relay:
    """Just enough of the Worker's D1 + crypto surface for catalog_handler."""

    def __init__(self, *, session_owner="alice", repo_exists=True):
        self.session_owner = session_owner
        self.tombstones = {}
        self.repos = {"bi:alice/demo": {"owner": "alice", "name": "demo"}} \
            if repo_exists else {}
        self.deleted_scoped = []


    async def d1_run(self, env, sql, *args):
        if "INSERT INTO repo_deletions" in sql:
            self.tombstones[args[0]] = args[3]
        elif "DELETE FROM repo_deletions WHERE repo_bi=?" in sql:
            self.tombstones.pop(args[0], None)
        elif "DELETE FROM repo_deletions WHERE expires_at<=?" in sql:
            for key, expires in list(self.tombstones.items()):
                if expires <= args[0]:
                    self.tombstones.pop(key)
        elif "DELETE FROM repositories WHERE key_bi=?" in sql:
            self.repos.pop(args[0], None)
        return None

    async def d1_first(self, env, sql, *args):
        if "FROM repo_deletions" in sql:
            expires = self.tombstones.get(args[0])
            return {"repo_bi": args[0]} if expires and expires > args[1] else None
        if "COUNT(*)" in sql:
            return {"c": len(self.repos)}
        if "FROM repositories WHERE key_bi=?" in sql:
            record = self.repos.get(args[0])
            return {"data": record} if record else None
        return None

    async def d1_all(self, env, sql, *args):
        return []


    def namespace(self, body=None):
        relay = self

        async def noop(*a, **k):
            return None

        async def authed_account_name(env, request):
            return relay.session_owner

        async def account_owns_node(env, actor, node):
            return bool(actor)

        async def blind_index(env, value):
            return "bi:" + str(value)

        async def decrypt_row(env, data):
            return data

        async def encrypt_row(env, data):
            return data

        async def account_row(env, name):
            return "bi:" + name, {"pubkey": "pk"}

        async def publication_key(env, owner, maintainer):
            return "pk"

        async def ed25519_verify(pub, sig, canonical):
            return True

        async def bounded_json_request(request):
            return body or {}

        async def contribution_ingest(*a, **k):
            return {"accepted": False, "warning": ""}

        async def delete_repo_scoped_state(env, repo_bi):
            relay.deleted_scoped.append(repo_bi)

        def safe_catalog_record(data):
            return {
                "owner": data.get("owner", ""),
                "name": data.get("name", ""),
                "maintainer": "pk",
                "visibility": "public",
                "updatedAt": str(NOW),
            }

        def json_response(payload, status=200, **kwargs):
            return SimpleNamespace(status=status, payload=payload)

        return {
            "ast": ast, "json": json, "re": re, "hashlib": hashlib,
            "parse_qs": parse_qs, "urlparse": urlparse,
            "Date": type("D", (), {"now": staticmethod(lambda: NOW)}),
            "LOGIN_MAX_SKEW_MS": 60_000,
            "CATALOG_MAX_RECORDS_PER_OWNER": 100,
            "MAX_CATALOG_REPOS": 1000,
            "CATALOG_TTL": 10,
            "STATE_PIN_HISTORY": 100,
            "ensure_schema": noop,
            "method_name": lambda request: request.method,
            "safe_segment": lambda value: str(value or "").strip().lower(),
            "clean_string": lambda value, limit=0: str(value or "")[:limit or None],
            "_authed_account_name": authed_account_name,
            "_account_owns_node": account_owns_node,
            "_audit_sensitive_action": noop,
            "blind_index": blind_index,
            "d1_run": relay.d1_run,
            "d1_first": relay.d1_first,
            "d1_all": relay.d1_all,
            "decrypt_row": decrypt_row,
            "encrypt_row": encrypt_row,
            "_delete_repo_scoped_state": delete_repo_scoped_state,
            "_delete_bounties_namespace": noop,
            "purge_catalog_related_caches": noop,
            "_owner_pubkey": noop,
            "_verify_owner_signature": noop,
            "bounded_json_request": bounded_json_request,
            "safe_catalog_record": safe_catalog_record,
            "_is_blocked_catalog_identity": lambda env, *values: False,
            "_account_row": account_row,
            "_catalog_publication_key": publication_key,
            "ed25519_verify": ed25519_verify,
            "touch_registered_node": noop,
            "catalog_rate_check": noop,
            "_contribution_write_catalog_state": noop,
            "_contribution_ingest_snapshot": contribution_ingest,
            "_https_mirror_refresh_catalog_publisher_health": noop,
            "_promote_hosted_repository_import": noop,
            "_world_broadcast_mirror_push": noop,
            "json_response": json_response,
        }


def _handlers(relay, body=None):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    nodes = [
        n for n in tree.body
        if (isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
            and n.name in WANTED)
        or (isinstance(n, ast.Assign)
            and any(isinstance(t, ast.Name)
                    and t.id == "REPO_DELETE_TOMBSTONE_TTL_MS"
                    for t in n.targets))
    ]
    assert {getattr(n, "name", "") for n in nodes} >= set(WANTED)
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
    namespace = relay.namespace(body)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _delete_request():
    return SimpleNamespace(
        method="DELETE",
        url="https://forkmesh.com/api/repositories?owner=alice&name=demo",
        headers={},
    )


def _publish_request():
    return SimpleNamespace(
        method="POST", url="https://forkmesh.com/api/repositories", headers={})


def _publish_body(intent=None):
    body = {"owner": "alice", "name": "demo", "maintainer": "pk",
            "catalogSigVersion": 2, "catalogSig": "sig"}
    if intent is not None:
        body["publishIntent"] = intent
    return body


def test_website_delete_records_a_tombstone():
    relay = _Relay()
    handler = _handlers(relay)["catalog_handler"]
    response = _run(handler(None, _delete_request()))
    assert response.payload == {"ok": True, "deleted": True}
    assert relay.repos == {}
    assert "bi:alice/demo" in relay.tombstones


def test_tombstone_is_recorded_even_when_the_row_is_already_gone():


    relay = _Relay(repo_exists=False)
    handler = _handlers(relay)["catalog_handler"]
    response = _run(handler(None, _delete_request()))
    assert response.payload == {"ok": True, "deleted": False}
    assert "bi:alice/demo" in relay.tombstones


def test_owner_signed_desktop_delete_leaves_no_tombstone():


    relay = _Relay(session_owner="")
    namespace = _handlers(relay)

    async def owner_pubkey(env, owner):
        return "pk"

    async def verify_owner_signature(env, owner, sig, canonical):
        return True

    namespace["_owner_pubkey"] = owner_pubkey
    namespace["_verify_owner_signature"] = verify_owner_signature
    request = _delete_request()
    request.url += "&ts=%d&sig=deadbeef" % NOW
    response = _run(namespace["catalog_handler"](None, request))
    assert response.payload == {"ok": True, "deleted": True}
    assert relay.tombstones == {}


def test_heartbeat_republish_is_refused_after_a_website_delete():
    relay = _Relay()
    _run(_handlers(relay)["catalog_handler"](None, _delete_request()))
    handler = _handlers(relay, _publish_body("auto"))["catalog_handler"]
    response = _run(handler(None, _publish_request()))
    assert response.status == 410
    assert response.payload == {"error": "repository_deleted"}
    assert relay.repos == {}

    assert "bi:alice/demo" in relay.tombstones


def test_publish_without_an_intent_field_is_treated_as_a_heartbeat():


    relay = _Relay()
    _run(_handlers(relay)["catalog_handler"](None, _delete_request()))
    handler = _handlers(relay, _publish_body())["catalog_handler"]
    response = _run(handler(None, _publish_request()))
    assert response.status == 410


def test_user_initiated_publish_lifts_the_tombstone():
    relay = _Relay()
    _run(_handlers(relay)["catalog_handler"](None, _delete_request()))
    handler = _handlers(relay, _publish_body("user"))["catalog_handler"]
    response = _run(handler(None, _publish_request()))
    assert response.status == 201
    assert relay.tombstones == {}


def test_expired_tombstone_stops_blocking_publishes():
    relay = _Relay()
    _run(_handlers(relay)["catalog_handler"](None, _delete_request()))
    relay.tombstones["bi:alice/demo"] = NOW - 1
    handler = _handlers(relay, _publish_body("auto"))["catalog_handler"]
    response = _run(handler(None, _publish_request()))
    assert response.status == 201


def test_tombstone_ttl_is_bounded_and_generous():
    source = ENTRY.read_text(encoding="utf-8")
    match = re.search(
        r"REPO_DELETE_TOMBSTONE_TTL_MS\s*=\s*([0-9 */+]+)", source)
    assert match, "the tombstone TTL constant should stay declarative"
    ttl = eval(match.group(1))
    day = 24 * 60 * 60 * 1000
    assert 30 * day <= ttl <= 365 * day


@pytest.mark.parametrize("statement", [
    "CREATE TABLE IF NOT EXISTS repo_deletions",
    "idx_repo_deletions_expires",
])
def test_schema_declares_the_tombstone_table(statement):
    schema = (Path(__file__).resolve().parents[1] / "src" / "schema.py") \
        .read_text(encoding="utf-8")
    assert statement in schema


REPOS_CPP = (Path(__file__).resolve().parents[2] / "qt_client" / "src" /
             "MainWindowRepos.cpp").read_text(encoding="utf-8")


def test_desktop_publish_declares_whether_a_person_asked_for_it():
    body = REPOS_CPP[REPOS_CPP.index(
        "void MainWindow::publishRepositoryNow("):]
    body = body[:body.index("QNetworkRequest request(catalogApiUrl());")]
    assert 'metadata.insert(QStringLiteral("publishIntent")' in body


    assert "m_catalogPublishServeState.value(publishKey, false)" in body
    assert "showDialogOnError || (repo.publishToNetwork && !wasServing)" in body


    assert 'fingerprintSource.remove(QStringLiteral("publishIntent"));' \
        in REPOS_CPP


def test_desktop_stops_sharing_a_repository_deleted_on_the_website():
    branch = REPOS_CPP[REPOS_CPP.index(
        'responseCode == QLatin1String("repository_deleted")'):]
    branch = branch[:branch.index("if (status == 409 &&")]
    assert "status == 410" in REPOS_CPP[
        REPOS_CPP.index('QLatin1String("repository_deleted")') - 200:
        REPOS_CPP.index('QLatin1String("repository_deleted")')]
    assert "repo.publishToNetwork = false;" in branch
    assert "saveRepositories();" in branch

    assert "scheduleCatalogPublish(" not in branch
