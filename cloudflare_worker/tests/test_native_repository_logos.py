#!/usr/bin/env python3
"""Native catalog logo privacy and authorization contracts."""

import ast
import asyncio
import hashlib
import re
from pathlib import Path
from urllib.parse import unquote, urlparse


SRC = Path(__file__).resolve().parents[1] / "src"
ENTRY = SRC / "entry.py"
CATALOG = SRC / "catalog.py"
SOURCE = (
    ENTRY.read_text(encoding="utf-8")
    + "\n"
    + CATALOG.read_text(encoding="utf-8")
)

FUNCTIONS = {
    "clean_string",
    "safe_segment",
    "_repo_identity_from_clone_url",
    "_catalog_record_matches_identity",
    "_repository_access_context",
    "_native_repository_logo_id",
    "_native_repository_logo_record",
    "_native_repository_logo_context",
}


def _load(extra):
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    nodes = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCTIONS
    ]
    assert {node.name for node in nodes} == FUNCTIONS
    namespace = {
        "hashlib": hashlib,
        "re": re,
        "unquote": unquote,
        "urlparse": urlparse,
        "ROOM_NAME_RE": re.compile(r"^[A-Za-z0-9._:-]+$"),
        "MAX_NODE_NAME": 100,
        "MAX_REPO_SEGMENT": 80,
        **extra,
    }
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class Request:
    def __init__(self, actor="", method="GET"):
        self.actor = actor
        self.method = method


def _harness(row):
    decryptions = []

    async def blind_index(_env, value):
        return "bi:" + str(value or "").strip().lower()

    async def d1_first(_env, sql, *_args):
        assert (
            "SELECT owner_bi, is_private, data FROM repositories" in sql
        )
        return dict(row) if row else None

    async def ensure_schema(_env):
        return None

    async def decrypt_row(_env, data):
        decryptions.append(data)
        return dict(data) if isinstance(data, dict) else None

    async def authed(_env, request):
        return request.actor

    async def owns(_env, actor, owner):
        return actor == owner == "alice"

    async def shared(_env, owner, repo, actor):
        return owner == "alice" and repo == "secret" and actor == "bob"

    async def basic_auth(_env, _owner, _repo, _request):
        return False

    namespace = _load({
        "blind_index": blind_index,
        "d1_first": d1_first,
        "ensure_schema": ensure_schema,
        "decrypt_row": decrypt_row,
        "_authed_account_name": authed,
        "_account_owns_node": owns,
        "_repo_shared_with": shared,
        "_basic_auth_view_ok": basic_auth,
    })
    return namespace["_native_repository_logo_context"], decryptions


def run(awaitable):
    return asyncio.run(awaitable)


def _record(visibility):
    return {
        "owner": "alice",
        "name": "secret",
        "visibility": visibility,
        "description": "bounded metadata",
        "logoMetadata": {
            "languages": {"C++": 12},
            "fileStructure": ["src/"],
        },
    }


def test_plaintext_private_gate_rejects_before_catalog_decryption():
    context, decryptions = _harness({
        "owner_bi": "bi:alice",
        "is_private": 1,
        "data": _record("private"),
    })

    assert run(context(None, Request(), "alice", "secret")) == (
        None, "", "")
    assert run(context(None, Request("mallory"), "alice", "secret")) == (
        None, "", "")
    assert decryptions == []

    record, repository_id, actor = run(
        context(None, Request("alice"), "alice", "secret"))
    assert record["isPrivate"] is True
    assert repository_id.startswith("native_")
    assert actor == "alice"
    assert len(decryptions) == 1


def test_private_logo_context_allows_explicitly_shared_collaborator():
    context, decryptions = _harness({
        "owner_bi": "bi:alice",
        "is_private": 1,
        "data": _record("private"),
    })

    record, repository_id, actor = run(
        context(None, Request("bob"), "alice", "secret"))
    assert record["isPrivate"] is True
    assert repository_id.startswith("native_")
    assert actor == "bob"
    assert len(decryptions) == 1


def test_missing_plaintext_visibility_fails_closed_before_decryption():
    context, decryptions = _harness({
        "owner_bi": "bi:alice",
        "is_private": None,
        "data": _record("public"),
    })

    assert run(context(None, Request(), "alice", "secret")) == (
        None, "", "")
    assert decryptions == []


def test_private_blind_owner_mismatch_is_not_a_decryption_oracle():
    context, decryptions = _harness({
        "owner_bi": "bi:other",
        "is_private": 1,
        "data": _record("private"),
    })

    assert run(context(
        None, Request("alice"), "alice", "secret")) == (None, "", "")
    assert decryptions == []


def test_encrypted_visibility_remains_authoritative_and_fails_closed():
    context, decryptions = _harness({
        # Historical migrations could leave this stale/public.
        "owner_bi": "bi:alice",
        "is_private": 0,
        "data": _record("private"),
    })

    assert run(context(None, Request(), "alice", "secret")) == (
        None, "", "")
    assert len(decryptions) == 1


def test_public_native_logo_context_uses_sanitized_catalog_factors():
    context, decryptions = _harness({
        "owner_bi": "bi:alice",
        "is_private": 0,
        "data": _record("public"),
    })

    record, repository_id, actor = run(
        context(None, Request(), "alice", "secret"))
    assert record["isPrivate"] is False
    assert record["metadata"]["languages"] == {"C++": 12}
    assert record["metadata"]["fileStructure"] == ["src/"]
    assert repository_id.startswith("native_")
    assert actor == ""
    assert len(decryptions) == 1


def test_native_logo_handler_normalizes_missing_and_unauthorized_to_404():
    handler = SOURCE[
        SOURCE.index("async def native_repository_logo_handler")
        : SOURCE.index("async def _repo_about_public")
    ]
    assert '{"error": "not_found"}' in handler
    assert "status=404" in handler
    assert 'cache_control="no-store"' in handler


def test_private_collaborator_can_suggest_but_cannot_replace_or_review():
    tree = ast.parse(SOURCE, filename=str(ENTRY))
    nodes = [
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "native_repository_logo_handler"
    ]
    assert len(nodes) == 1

    async def ensure_schema(_env):
        return None

    async def context(_env, _request, _owner, _repo):
        return {"isPrivate": True}, "native_opaque", "bob"

    async def owns(_env, actor, owner):
        return actor == owner == "alice"

    async def moderator(_env, actor):
        return actor == "mod"

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    class Service:
        async def native_logo_suggestions(
                self, _env, _request, repository_id, record, can_admin):
            return {
                "repositoryId": repository_id,
                "private": record["isPrivate"],
                "collaboratorMayReview": await can_admin("bob"),
                "ownerMayReview": await can_admin("alice"),
                "moderatorMayReview": await can_admin("mod"),
            }

    namespace = {
        "ensure_schema": ensure_schema,
        "_native_repository_logo_context": context,
        "_account_owns_node": owns,
        "_repository_import_moderator": moderator,
        "_repository_import_service": Service,
        "json_response": json_response,
    }
    module = ast.fix_missing_locations(
        ast.Module(body=nodes, type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    result = run(namespace["native_repository_logo_handler"](
        None, Request("bob", method="POST"), "alice", "secret",
        suggestions=True))

    assert result == {
        "repositoryId": "native_opaque",
        "private": True,
        "collaboratorMayReview": False,
        "ownerMayReview": True,
        "moderatorMayReview": True,
    }
