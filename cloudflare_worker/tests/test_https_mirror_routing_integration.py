"""Worker wiring for signed, masked direct-HTTPS repository traffic."""

import ast
import asyncio
import hashlib
import json
import re
import sqlite3
import sys
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"
EDGE = ROOT / "cloudflare_worker" / "src" / "edge_routing.py"
SCHEMA = ROOT / "cloudflare_worker" / "src" / "schema.py"
URLS = ROOT / "cloudflare_worker" / "src" / "urls.py"
QT_REPO_HOST = ROOT / "qt_client" / "src" / "RepoHost.cpp"
MIGRATION = (
    ROOT / "cloudflare_worker" / "migrations"
    / "0047_https_mirror_routing.sql"
)
PRIVATE_MIGRATION = (
    ROOT / "cloudflare_worker" / "migrations"
    / "0050_private_mirror_routes.sql"
)
PRIVATE_ACCESS_MIGRATION = (
    ROOT / "cloudflare_worker" / "migrations"
    / "0053_private_replica_access.sql"
)
WRANGLER = ROOT / "cloudflare_worker" / "wrangler.toml"


def _function_source(name):
    source = ENTRY.read_text(encoding="utf-8")
    tree = ast.parse(source)
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name == name:
                return ast.get_source_segment(source, node)
    raise AssertionError(f"{name} not found")


def _method_source(class_name, method_name):
    source = ENTRY.read_text(encoding="utf-8")
    tree = ast.parse(source)
    for node in tree.body:
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for child in node.body:
                if (
                    isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef))
                    and child.name == method_name
                ):
                    return ast.get_source_segment(source, child)
    raise AssertionError(f"{class_name}.{method_name} not found")


def test_public_repository_metadata_cache_is_attestation_keyed_and_bounded():
    namespace = {
        "hashlib": hashlib,
        "json": json,
        "re": re,
        "MAX_BLOB_BATCH": 60,
        "REPOSITORY_METADATA_CACHE_PREFIX": (
            "https://forkmesh.internal/repository-metadata/v3/"
        ),
    }
    exec(_function_source("repository_metadata_cache_key"), namespace)
    key = namespace["repository_metadata_cache_key"]
    first = {
        "repoBi": "public-repo-blind-index",
        "currentPins": {"b" * 64, "a" * 64},
    }
    same = {
        "repoBi": "public-repo-blind-index",
        "currentPins": {"a" * 64, "b" * 64},
    }
    pull_ref = "c" * 40

    tree_key = key(first, "tree", {"path": "pulls", "ref": pull_ref})
    assert tree_key.startswith(namespace["REPOSITORY_METADATA_CACHE_PREFIX"])
    assert tree_key == key(
        same, "tree", {"path": "pulls", "ref": pull_ref}
    )
    assert tree_key != key(
        {**first, "currentPins": {"d" * 64}},
        "tree",
        {"path": "pulls", "ref": pull_ref},
    )
    assert tree_key != key(
        first, "tree", {"path": "pulls", "ref": "d" * 40}
    )
    assert key(first, "branches", {})
    assert key(first, "tree", {"path": "", "ref": "main"})
    assert key(first, "tree", {"path": "", "ref": "Feature/X"}) != key(
        first, "tree", {"path": "", "ref": "feature/x"}
    )
    assert key(first, "sizes", {"ref": "main"})
    assert key(first, "stats", {"ref": "main"})
    assert key(first, "history", {"ref": "main"})
    assert key(first, "blob", {"path": "README.md", "ref": "main"})
    assert key(
        first,
        "blob",
        {
            "path": ".forkmesh/issues/open/44/issue-44.json",
            "ref": "main",
        },
    )
    assert key(
        first,
        "tree",
        {"path": ".forkmesh/discussions/9", "ref": "main"},
    )
    assert key(
        first,
        "blob",
        {"path": "pulls/44/changes.patch", "ref": pull_ref},
    )
    assert key(
        first,
        "blobs",
        {
            "path": ["pulls/44/pull.md", "pulls/43/pull.md"],
            "ref": pull_ref,
        },
    )
    assert key(
        first,
        "blobs",
        {
            "path": [
                ".forkmesh/issues/open/2/issue-2.json",
                ".forkmesh/issues/open/1/issue-1.json",
            ],
            "ref": "main",
        },
    )
    assert key(
        first,
        "blobs",
        {
            "path": [
                ".forkmesh/discussions/3/discussion.md",
                ".forkmesh/discussions/2/discussion.md",
            ],
            "ref": "main",
        },
    )

    # No arbitrary source contents, raw paths, malformed refs, duplicate
    # amplification, or unattested repository state may enter this cache.
    assert not key(
        first,
        "blobs",
        {"path": ["src/main.py"], "ref": pull_ref},
    )
    assert not key(
        first,
        "blobs",
        {
            "path": ["pulls/44/pull.md", "pulls/44/pull.md"],
            "ref": pull_ref,
        },
    )
    assert not key(first, "blob", {"path": "src/main.py", "ref": "main"})
    assert not key(first, "tree", {"path": "private", "ref": pull_ref})
    assert not key(first, "tree", {"path": "pulls", "ref": "bad..ref"})
    assert not key(
        {"repoBi": "public-repo-blind-index", "currentPins": set()},
        "branches",
        {},
    )

    proxy = _function_source("_https_mirror_proxy")
    assert (
        proxy.index("repository_metadata_cache_get(env, metadata_cache_key)")
        < proxy.index("_https_mirror_candidates(")
    )
    assert "not bypass_cache" in proxy
    assert (
        "repository_metadata_cache_put(\n"
        "                env, metadata_cache_key, upstream, status)"
    ) in proxy
    assert 'in context.get("currentPins", set())' in proxy
    assert (
        'response_headers["X-ForkMesh-Served-By"] = endpoint["node"]'
        in proxy
    )
    assert (
        proxy.index("repository_metadata_cache_get(env, metadata_cache_key)")
        < proxy.index('response_headers["X-ForkMesh-Served-By"]')
    )
    get_source = _function_source("repository_metadata_cache_get")
    put_source = _function_source("repository_metadata_cache_put")
    assert '"no-store, max-age=0, must-revalidate"' in get_source
    assert "namespace.get(cache_key)" in get_source
    assert "namespace.put(cache_key, raw)" in put_source
    assert '"public, max-age=%d, immutable"' in put_source
    assert "int(status or 0) != 200" in put_source
    assert "content_length <= 0" in put_source
    assert "content_length > REPOSITORY_METADATA_CACHE_MAX_BYTES" in put_source
    assert "X-ForkMesh-Served-By" not in get_source

    class MustNotClone:
        def clone(self):
            raise AssertionError("a non-200 upstream must never be cached")

    put_namespace = {
        "asyncio": asyncio,
        "json": json,
        "REPOSITORY_METADATA_CACHE_MAX_BYTES": 8 * 1024 * 1024,
        "REPOSITORY_METADATA_CACHE_TTL": 365 * 24 * 60 * 60,
    }
    exec(put_source, put_namespace)
    assert asyncio.run(
        put_namespace["repository_metadata_cache_put"](
            SimpleNamespace(),
            "https://cache.invalid/key",
            MustNotClone(),
            503,
        )
    ) is None


def test_repository_metadata_cache_round_trips_through_global_kv():
    config = WRANGLER.read_text(encoding="utf-8")
    assert 'binding = "REPOSITORY_METADATA"' in config
    assert 'id = "ee8854d698c14e93a9950ec0f3e6538f"' in config

    class FakeHeaders(dict):
        pass

    class FakeUpstream:
        headers = FakeHeaders({
            "content-type": "application/json; charset=utf-8",
        })

        def clone(self):
            return self

        async def text(self):
            return '{"ok":true,"entries":[]}'

    class FakeCache:
        def __init__(self):
            self.values = {}

        async def match(self, key):
            return self.values.get(key)

        async def put(self, key, value):
            self.values[key] = value

    class FakeKV:
        def __init__(self):
            self.values = {}

        async def get(self, key):
            return self.values.get(key)

        async def put(self, key, value):
            self.values[key] = value

    class FakeJsResponse:
        @staticmethod
        def new(body, options):
            return SimpleNamespace(
                body=body,
                headers=FakeHeaders(options["headers"]),
            )

    class FakeWorkerResponse:
        def __init__(self, body, status=200, headers=None):
            self.body = body
            self.status = status
            self.headers = headers or {}

    edge = FakeCache()
    kv = FakeKV()
    namespace = {
        "asyncio": asyncio,
        "json": json,
        "js_caches": SimpleNamespace(default=edge),
        "JsResponse": FakeJsResponse,
        "Response": FakeWorkerResponse,
        "to_js": lambda value: value,
        "REPOSITORY_METADATA_CACHE_MAX_BYTES": 8 * 1024 * 1024,
        "REPOSITORY_METADATA_CACHE_TTL": 365 * 24 * 60 * 60,
    }
    exec(_function_source("repository_metadata_cache_put"), namespace)
    exec(_function_source("repository_metadata_cache_get"), namespace)
    cache_key = "https://forkmesh.internal/repository-metadata/v3/" + "a" * 64
    env = SimpleNamespace(REPOSITORY_METADATA=kv)

    asyncio.run(namespace["repository_metadata_cache_put"](
        env, cache_key, FakeUpstream(), 200))
    assert kv.values[cache_key] == '{"ok":true,"entries":[]}'
    assert cache_key in edge.values

    # Prove a different colo can refill its local edge from persistent KV.
    edge.values.clear()
    response = asyncio.run(namespace["repository_metadata_cache_get"](
        env, cache_key))
    assert response.body == '{"ok":true,"entries":[]}'
    assert response.headers["cache-control"].startswith("no-store")
    assert cache_key in edge.values


def test_registration_is_signed_account_bound_and_manifest_verified():
    handler = _function_source("https_mirror_endpoint_handler")
    assert "_owner_signing_pubkeys" in handler
    assert "ed25519_verify" in handler
    assert "_https_mirror_manifest_ok" in handler
    assert "_https_mirror_cloudflare_dns_ok" in handler
    assert "cloudflare_proxied_dns_required" in handler
    assert "mirror_https_endpoints" in handler
    assert "stale_registration" in handler
    assert "_https_mirror_refresh_registered_health" in handler
    assert '"health": "active" if health_active else "pending"' in handler
    assert "repositoryBytesInD1" in handler
    assert "private" not in handler.lower()

    manifest = _function_source("_https_mirror_manifest_ok")
    assert "/forkmesh-mirror.json" in manifest
    assert "validate_tunnel_manifest" in manifest
    assert "ed25519_verify" in manifest

    durable_write = handler.index(
        '"""INSERT INTO mirror_https_endpoints')
    activation = handler.index(
        "await _https_mirror_refresh_registered_health")
    assert durable_write < activation


def test_dns_over_https_requests_cloudflare_json_media_type(monkeypatch):
    calls = []

    class Response:
        status = 200
        headers = {}

        async def text(self):
            return '{"Status":0,"Answer":[]}'

    async def fetch(url, options):
        calls.append((url, options))
        return Response()

    async def fetch_with_timeout(url, options, timeout):
        assert timeout == 5
        return await fetch(url, options)

    namespace = {
        "js_fetch_with_timeout": fetch_with_timeout,
    }
    exec(_function_source("_https_mirror_fetch_text"), namespace)
    result = asyncio.run(namespace["_https_mirror_fetch_text"](
        "https://cloudflare-dns.com/dns-query?name=mirror.example&type=A",
        4096,
        5,
        "application/dns-json",
    ))
    assert result == (200, '{"Status":0,"Answer":[]}')
    assert calls[0][1]["headers"]["accept"] == "application/dns-json"

    dns_check = _function_source("_https_mirror_cloudflare_dns_ok")
    assert '"application/dns-json"' in dns_check


def test_cron_verifies_fresh_forkmesh_proof_and_clears_failed_state():
    scheduled = _method_source("Default", "_run_scheduled_jobs")
    health = _function_source("_https_mirror_health_one")
    cron = _function_source("https_mirror_health_cron")
    failed = _function_source("_https_mirror_mark_failed")
    assert "https_mirror_health_cron" in scheduled
    assert "repository_health_challenge" in health
    assert "_https_mirror_cloudflare_dns_ok" in health
    assert "messageSha256" in health
    assert "ed25519_verify" in health
    assert "_https_mirror_refs_match" in health
    assert "HTTPS_MIRROR_REQUIRED_FORKMESH_OPERATIONS" in health
    assert "return forkmesh_active" in health
    assert "_https_mirror_accepted_forkmesh_refs" in cron
    assert "asyncio.gather" not in cron
    assert "for row in rows" in cron
    assert "forkmesh_active=0" in failed
    assert "healthy=0" in failed


def test_flagship_state_pin_resolves_org_alias_but_health_challenges_public_alias():
    namespace = {"re": re}
    exec(_function_source("_https_mirror_expected_forkmesh_refs"), namespace)
    looked_up = []
    state_hash = "a" * 64

    async def org_repo_node(env, owner, repo):
        assert (owner, repo) == ("forkmesh", "forkmesh")
        return "mirror2"

    async def blind_index(env, value):
        looked_up.append(value)
        return "repo-bi"

    async def d1_first(env, sql, *params):
        assert "FROM repositories" in sql
        assert params == ("repo-bi",)
        return {"data": "encrypted", "is_private": 0}

    async def decrypt_row(env, value):
        assert value == "encrypted"
        return {
            "owner": "mirror2",
            "name": "forkmesh",
            "visibility": "public",
            "stateHash": state_hash,
        }

    namespace.update({
        "_org_repo_node": org_repo_node,
        "blind_index": blind_index,
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
        "clean_string": lambda value, limit: str(value or "")[:limit],
    })
    result = asyncio.run(
        namespace["_https_mirror_expected_forkmesh_refs"](object()))
    assert result == state_hash
    assert looked_up == ["mirror2/forkmesh"]

    health = _function_source("_https_mirror_health_one")
    assert '"&owner=forkmesh&repo=forkmesh"' in health


def test_flagship_state_pin_falls_back_to_account_namespace_without_org_alias():
    namespace = {"re": re}
    exec(_function_source("_https_mirror_expected_forkmesh_refs"), namespace)
    looked_up = []
    state_hash = "b" * 64

    async def org_repo_node(env, owner, repo):
        return ""

    async def blind_index(env, value):
        looked_up.append(value)
        return "repo-bi"

    async def d1_first(env, sql, *params):
        return {"data": "encrypted", "is_private": 0}

    async def decrypt_row(env, value):
        return {"visibility": "public", "stateHash": state_hash}

    namespace.update({
        "_org_repo_node": org_repo_node,
        "blind_index": blind_index,
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
        "clean_string": lambda value, limit: str(value or "")[:limit],
    })
    result = asyncio.run(
        namespace["_https_mirror_expected_forkmesh_refs"](object()))
    assert result == state_hash
    assert looked_up == ["forkmesh/forkmesh"]


def test_flagship_health_accepts_recent_source_pin_during_mirror_convergence():
    namespace = {
        "re": re,
        "hmac": __import__("hmac"),
    }
    exec(_function_source("_https_mirror_refs_match"), namespace)
    matches = namespace["_https_mirror_refs_match"]
    current = "c" * 64
    previous = "b" * 64

    assert matches(current, frozenset({current, previous})) is True
    assert matches(previous, frozenset({current, previous})) is True
    assert matches("a" * 64, frozenset({current, previous})) is False
    assert matches("not-a-digest", frozenset({current, previous})) is False


def test_flagship_accepted_pins_include_bounded_source_history():
    namespace = {
        "re": re,
        "STATE_PIN_HISTORY": 100,
    }
    exec(
        _function_source("_https_mirror_accepted_forkmesh_refs"),
        namespace,
    )
    current = "d" * 64
    previous = "c" * 64
    queries = []

    async def org_repo_node(env, owner, repo):
        assert (owner, repo) == ("forkmesh", "forkmesh")
        return "source-node"

    async def blind_index(env, value):
        assert value == "source-node/forkmesh"
        return "repo-bi"

    async def d1_first(env, sql, *params):
        queries.append((sql, params))
        return {"data": "encrypted", "is_private": 0}

    async def d1_all(env, sql, *params):
        queries.append((sql, params))
        return [
            {"state_hash": previous},
            {"state_hash": "invalid"},
        ]

    async def decrypt_row(env, value):
        assert value == "encrypted"
        return {
            "visibility": "public",
            "stateHash": current,
        }

    namespace.update({
        "_org_repo_node": org_repo_node,
        "blind_index": blind_index,
        "d1_first": d1_first,
        "d1_all": d1_all,
        "decrypt_row": decrypt_row,
        "clean_string": lambda value, limit: str(value or "")[:limit],
    })
    pins = asyncio.run(
        namespace["_https_mirror_accepted_forkmesh_refs"](object()))

    assert pins == frozenset({current, previous})
    assert queries[1][1] == ("repo-bi", 100)


def test_every_registered_mirror_can_refresh_its_exact_signed_health():
    events = []
    endpoint = {
        "node_bi": "mirror-3-bi",
        "node_name": "mirror3",
        "base_url": "https://mirror3.example.test",
        "public_key": "node-public-key",
    }

    async def d1_first(env, sql, *params):
        events.append(("lookup", params))
        assert "FROM mirror_https_endpoints WHERE node_name=?" in sql
        assert params == ("mirror3",)
        return endpoint

    async def accepted_refs(env):
        events.append(("expected",))
        return frozenset({"c" * 64})

    async def health_one(env, row, expected):
        events.append(("health", row, expected))
        return True

    namespace = {
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", value)),
        "d1_first": d1_first,
        "_https_mirror_accepted_forkmesh_refs": accepted_refs,
        "_https_mirror_health_one": health_one,
    }
    exec(
        _function_source("_https_mirror_refresh_registered_health"),
        namespace,
    )
    refreshed = asyncio.run(
        namespace["_https_mirror_refresh_registered_health"](
            object(), "Mirror3"))
    assert refreshed is True
    assert events == [
        ("lookup", ("mirror3",)),
        ("expected",),
        ("health", endpoint, frozenset({"c" * 64})),
    ]


def test_registered_health_refresh_stays_pending_on_missing_pin_or_failure():
    rows = []
    health_calls = []
    expected = ""

    async def d1_first(env, sql, *params):
        rows.append(params)
        return {
            "node_bi": "mirror-3-bi",
            "node_name": "mirror3",
            "base_url": "https://mirror3.example.test",
            "public_key": "node-public-key",
        }

    async def accepted_refs(env):
        return frozenset({expected}) if expected else frozenset()

    async def health_one(env, row, state_hash):
        health_calls.append((row, state_hash))
        raise RuntimeError("bounded health fetch failed")

    namespace = {
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", value)),
        "d1_first": d1_first,
        "_https_mirror_accepted_forkmesh_refs": accepted_refs,
        "_https_mirror_health_one": health_one,
    }
    exec(
        _function_source("_https_mirror_refresh_registered_health"),
        namespace,
    )
    refresh = namespace["_https_mirror_refresh_registered_health"]

    assert asyncio.run(refresh(object(), "not valid")) is False
    assert rows == []
    assert asyncio.run(refresh(object(), "mirror3")) is False
    assert rows == [("mirror3",)]
    assert health_calls == []

    expected = "d" * 64
    assert asyncio.run(refresh(object(), "mirror3")) is False
    assert rows == [("mirror3",), ("mirror3",)]
    assert len(health_calls) == 1


def test_public_flagship_publish_refreshes_only_its_exact_node_health():
    events = []

    async def org_repo_node(env, owner, repo):
        events.append(("canonical", owner, repo))
        return "mirror2"

    async def refresh_registered(env, node):
        events.append(("health", node))
        return True

    namespace = {
        "MAX_REPO_SEGMENT": 128,
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", value)),
        "_org_repo_node": org_repo_node,
        "_https_mirror_refresh_registered_health": refresh_registered,
    }
    exec(
        _function_source("_https_mirror_refresh_catalog_publisher_health"),
        namespace,
    )
    refreshed = asyncio.run(
        namespace["_https_mirror_refresh_catalog_publisher_health"](
            object(),
            {
                "owner": "Mirror2",
                "name": "forkmesh",
                "visibility": "public",
            },
        )
    )
    assert refreshed is True
    assert events == [
        ("canonical", "forkmesh", "forkmesh"),
        ("health", "mirror2"),
    ]


def test_catalog_health_activation_is_flagship_public_only_and_best_effort():
    calls = []
    endpoint_available = True

    async def org_repo_node(env, owner, repo):
        return "mirror2"

    async def refresh_registered(env, node):
        calls.append((node,))
        if not endpoint_available:
            return False
        raise RuntimeError("bounded health fetch failed")

    namespace = {
        "MAX_REPO_SEGMENT": 128,
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", value)),
        "_org_repo_node": org_repo_node,
        "_https_mirror_refresh_registered_health": refresh_registered,
    }
    exec(
        _function_source("_https_mirror_refresh_catalog_publisher_health"),
        namespace,
    )
    refresh = namespace["_https_mirror_refresh_catalog_publisher_health"]
    for record in (
        {"owner": "mirror2", "name": "forkmesh", "visibility": "private"},
        {"owner": "mirror2", "name": "another-repo", "visibility": "public"},
        {"owner": "not valid", "name": "forkmesh", "visibility": "public"},
        # Same repository name, but this account is not the organization's
        # current canonical backing node and must not trigger a health fetch.
        {"owner": "mirror3", "name": "forkmesh", "visibility": "public"},
    ):
        assert asyncio.run(refresh(object(), record)) is False
    assert calls == []

    endpoint_available = False
    assert asyncio.run(refresh(object(), {
        "owner": "mirror2",
        "name": "forkmesh",
        "visibility": "public",
    })) is False
    assert calls == [("mirror2",)]

    # Once the exact endpoint is selected, a fetch/verifier exception is
    # swallowed so the already accepted catalog publication is not rolled back.
    endpoint_available = True
    assert asyncio.run(refresh(object(), {
        "owner": "mirror2",
        "name": "forkmesh",
        "visibility": "public",
    })) is False
    assert calls == [("mirror2",), ("mirror2",)]


def test_catalog_health_activation_falls_back_only_to_canonical_account():
    looked_up = []

    async def org_repo_node(env, owner, repo):
        return ""

    async def refresh_registered(env, node):
        looked_up.append((node,))
        return False

    namespace = {
        "MAX_REPO_SEGMENT": 128,
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: bool(
            re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", value)),
        "_org_repo_node": org_repo_node,
        "_https_mirror_refresh_registered_health": refresh_registered,
    }
    exec(
        _function_source("_https_mirror_refresh_catalog_publisher_health"),
        namespace,
    )
    refresh = namespace["_https_mirror_refresh_catalog_publisher_health"]
    assert asyncio.run(refresh(object(), {
        "owner": "mirror2",
        "name": "forkmesh",
        "visibility": "public",
    })) is False
    assert looked_up == []
    assert asyncio.run(refresh(object(), {
        "owner": "forkmesh",
        "name": "forkmesh",
        "visibility": "public",
    })) is False
    assert looked_up == [("forkmesh",)]


def test_catalog_persists_before_exact_node_health_activation_and_cache_purge():
    handler = _function_source("catalog_handler")
    durable_write = handler.index(
        "await _contribution_write_catalog_state")
    activation = handler.index(
        "await _https_mirror_refresh_catalog_publisher_health")
    cache_purge = handler.index("await purge_catalog_related_caches()")
    assert durable_write < activation < cache_purge

    activation_source = _function_source(
        "_https_mirror_refresh_catalog_publisher_health")
    assert '_org_repo_node(env, "forkmesh", "forkmesh")' in activation_source
    assert "_https_mirror_refresh_registered_health" in activation_source


def test_public_browse_clone_and_release_are_intercepted_before_host_tunnel():
    route = _method_source("Default", "_route")
    assert route.index("HTTPS_MIRROR_ENDPOINT_PATH") < route.index(
        "org_alias_rewrite")
    assert '"git-info-refs"' in route
    assert '"git-upload-pack"' in route
    assert '"release-blob"' in route
    assert "return await _https_mirror_proxy" in route
    assert '"tree", "blobs", "blob", "raw", "history", "commit"' in route
    assert '"compare", "branches", "search", "stats", "sizes"' in route

    git_block = route.split("git_info = GIT_INFO_RE.match", 1)[1].split(
        'if url.path == "/"', 1)[0]
    assert "_https_mirror_proxy" in git_block
    assert "FORKMESH_HOST" not in git_block

    release_block = route.split(
        "release_blob_match = RELEASE_BLOB_RE.match", 1)[1].split(
            "host_match = REPO_HOST_RE.match", 1)[0]
    assert "_https_mirror_proxy" in release_block
    assert "FORKMESH_HOST" not in release_block

    browse_fast_path = route.split(
        "host_match = REPO_HOST_RE.match", 1)[1].split(
            "# Registering as a host", 1)[0]
    assert "_https_mirror_proxy" in browse_fast_path
    assert "_https_mirror_private_proxy" not in browse_fast_path
    assert "_private_replica_not_found" in browse_fast_path
    assert "FORKMESH_HOST" not in browse_fast_path

    proxy = _function_source("_https_mirror_proxy")
    assert "forkmesh-masked-proxy-v1" not in proxy  # canonical lives in policy
    assert "https_routing.request_message" in proxy
    assert "ed25519_sign" in proxy
    assert "JsResponse.new" in proxy
    assert "upstream.body" in proxy
    assert "FORKMESH_HOST" not in proxy
    assert "WebSocket" not in proxy
    assert "authorization" not in proxy.lower()
    assert "cookie" not in proxy.lower()
    assert '"redirect": "manual"' in proxy
    assert "status in HTTPS_MIRROR_RETRY_STATUSES" in proxy
    assert "if status in (401, 403):" in proxy
    assert "if status in (401, 403, 500, 502, 503, 504):" not in proxy


def test_authorized_private_reads_stream_only_opaque_ciphertext_over_https():
    route = _method_source("Default", "_route")
    registration = _function_source("https_mirror_private_route_handler")
    access = _function_source("_https_mirror_private_access_handler")
    lookup = _function_source("_https_mirror_private_access_record")
    authorization = _function_source(
        "_https_mirror_private_request_authorized")
    candidates = _function_source("_https_mirror_private_candidates")
    proxy = _function_source("_https_mirror_private_proxy")
    urls = URLS.read_text(encoding="utf-8")
    assert "PRIVATE_REPLICA_ACCESS_RE" in route
    assert route.index("PRIVATE_REPLICA_ACCESS_RE") < route.index(
        "org_alias_rewrite")
    assert "REPO_PRIVATE_REPLICA_RE" not in route
    assert r"^/api/private-replicas/([0-9a-f]{64})$" in urls
    assert "/api/repo/([^/]+)/([^/]+)/private-replica" not in urls
    assert "url.query" in access
    assert 'method_name(request) not in ("GET", "HEAD")' in access
    assert "_https_mirror_private_access_record" in access
    assert "_https_mirror_private_request_authorized" in access
    assert "_private_replica_not_found" in access
    assert "_https_mirror_private_proxy" in access
    assert "parse_qs" not in authorization
    assert "urlparse" not in authorization
    assert "_basic_auth_view_ok" in authorization
    assert "opaque_replica_id=? AND active=1" in lookup
    assert "SELECT DISTINCT repo_bi,key_epoch" in lookup
    assert "len(repo_bis) != 1" in lookup
    assert "decrypt_row" in lookup
    assert "blind_index(env, owner + \"/\" + repo)" in lookup
    assert "private_mirror_routes" in registration
    assert '"accessPath"' in registration
    assert '"/api/private-replicas/" + binding["opaqueId"]' in registration
    assert "WHERE opaque_replica_id=? AND repo_bi<>?" in registration
    assert "https_routing.private_route_message" in _function_source(
        "_https_mirror_private_route_payload")
    assert "_owner_pubkey" in registration
    assert "ed25519_verify" in registration
    assert "privateRoutesListed" in registration
    assert "JOIN mirror_https_endpoints" in candidates
    assert "r.repo_bi=?" in candidates
    assert "r.key_epoch=?" in candidates
    assert "https_routing.masked_private_replica_url" in proxy
    assert "application/vnd.forkmesh.private-replica+json" in proxy
    assert "hmac.compare_digest(etag, expected_etag)" in proxy
    assert "upstream.body" in proxy
    assert "authorization" not in proxy.lower()
    assert "cookie" not in proxy.lower()
    assert '"redirect": "manual"' in proxy
    assert "WebSocket" not in proxy


def test_private_catalog_discloses_opaque_access_only_after_viewer_acl():
    catalog = _function_source("catalog_handler")
    access_id = _function_source(
        "_https_mirror_private_catalog_access_id")
    assert catalog.index("if not authed_viewer:") < catalog.index(
        'rec["privateAccessId"] = access_id')
    assert "repo_shares" in catalog
    assert "_https_mirror_private_catalog_access_id" in catalog
    assert '"privateAccessPath"' in catalog
    assert "no-store, max-age=0, must-revalidate" in catalog
    assert "WHERE repo_bi=? AND active=1 AND key_epoch=?" in access_id
    assert "ORDER BY updated_at DESC LIMIT 1" in access_id


def test_private_access_request_denials_are_uniform_and_queries_never_lookup():
    namespace = {}
    exec(_function_source("_https_mirror_private_access_handler"), namespace)
    handler = namespace["_https_mirror_private_access_handler"]
    denial = {"status": 404, "body": {"error": "not_found"}}
    proxied = {"status": 200}
    calls = []
    record = {
        "repoBi": "blind",
        "owner": "alice",
        "repo": "secret",
        "keyEpoch": 2,
    }

    namespace["method_name"] = lambda request: request.method
    namespace["_private_replica_not_found"] = lambda: denial

    async def lookup(env, opaque_id):
        calls.append(("lookup", opaque_id))
        return env.get("record")

    async def authorize(env, request, private_record):
        calls.append(("authorize", private_record["repoBi"]))
        return env.get("authorized", False)

    async def proxy(env, request, private_record):
        calls.append(("proxy", private_record["repoBi"]))
        return proxied

    namespace["_https_mirror_private_access_record"] = lookup
    namespace["_https_mirror_private_request_authorized"] = authorize
    namespace["_https_mirror_private_proxy"] = proxy
    request = SimpleNamespace(method="GET")
    clean_url = SimpleNamespace(query="")
    query_url = SimpleNamespace(query="viewer=alice")

    assert asyncio.run(handler(
        {}, request, clean_url, "a" * 64)) == denial
    unknown_calls = list(calls)
    calls.clear()
    assert asyncio.run(handler(
        {"record": record}, request, clean_url, "a" * 64)) == denial
    unauthorized_calls = list(calls)
    calls.clear()
    assert asyncio.run(handler(
        {"record": record, "authorized": True},
        request, clean_url, "a" * 64)) == proxied
    authorized_calls = list(calls)
    calls.clear()
    assert asyncio.run(handler(
        {"record": record, "authorized": True},
        request, query_url, "a" * 64)) == denial
    query_calls = list(calls)

    assert unknown_calls == [("lookup", "a" * 64)]
    assert unauthorized_calls == [
        ("lookup", "a" * 64), ("authorize", "blind")]
    assert authorized_calls == [
        ("lookup", "a" * 64), ("authorize", "blind"), ("proxy", "blind")]
    assert query_calls == []


def test_receive_pack_fails_closed_without_host_socket_or_request_body():
    push = _method_source("Default", "_git_push")
    assert "direct_https_receive_pack_required" in push
    assert "repositoryBytesAccepted" in push
    assert "socketFallback" in push
    assert "FORKMESH_HOST" not in push
    assert "durable_object_request" not in push
    assert "request.bytes" not in push
    assert "request.text" not in push

    qt = QT_REPO_HOST.read_text(encoding="utf-8")
    assert '"persistentSocket"), false' in qt
    assert "QTcpSocket" not in qt
    assert "connectSocket" not in qt
    assert "bounded-https-poll" in qt


def test_installer_candidates_are_fresh_https_proofs_not_host_sockets():
    install = _function_source("install_source")
    assert "mirror_https_endpoints" in install
    assert "forkmesh_active=1" in install
    assert "forkmesh_verified_at>=?" in install
    assert "https_routing.ENDPOINT_STALE_MS" in install
    assert "FORKMESH_HOST" not in install
    assert "host_object" not in install
    assert "WebSocket" not in install


def test_selection_is_public_group_scoped_fresh_integrity_and_abuse_gated():
    context = _function_source("_https_mirror_public_context")
    hosted_route = _function_source("_hosted_repository_import_route")
    candidates = _function_source("_https_mirror_candidates")
    proof = _function_source("_https_mirror_repository_proof")
    edge = EDGE.read_text(encoding="utf-8")
    assert "_decrypted_public_catalog" in context
    assert "_hosted_repository_import_route" in context
    assert "status='actively_mirrored'" in hosted_route
    assert 'mirror_owner in ("mirror2", "mirror3")' in hosted_route
    assert "visibility" in context
    assert "repo_mirror_same_group" in context
    assert "clone_state_pins" in context
    assert '"currentNodes": current_nodes' in context
    assert '"currentPins": set(current_pins)' in context
    assert "FROM org_repos" in context
    assert 'target.get("stateHash", "")' in context
    assert '"remote-clone"' in context
    assert "context[\"nodes\"]" in candidates
    assert "preferred_region" in candidates
    assert "select_endpoints" in candidates
    assert 'context.get("currentNodes", set())' in candidates
    assert "ENDPOINT_STALE_MS" in edge
    assert "abuseBlocked" in edge
    assert "integrity" in edge
    assert "MAX_FAILOVER_ATTEMPTS" in edge
    assert "repository_health_challenge" in proof
    assert "refs_digest in context[\"pins\"]" in proof
    assert 'endpoint["refsSha256"] = refs_digest' in proof
    assert 'memo.get("refsSha256", "")' in proof
    assert "operations_digest == claimed_operations_digest" in proof
    assert "ed25519_verify" in proof


def test_public_proxy_preserves_verified_org_alias_for_gateway_bytes():
    proxy = _function_source("_https_mirror_proxy")
    proof = _function_source("_https_mirror_repository_proof")
    assert 'context["routeOwner"] = route_owner' in proxy
    assert "await _org_repo_node(" in proxy
    assert 'context.get("routeOwner") or context["owner"]' in proxy
    assert 'route_owner = context.get("routeOwner") or context["owner"]' in proof
    assert '"&owner=" + quote(route_owner)' in proof


def test_clone_round_robin_advances_once_per_two_request_git_clone():
    advance = _function_source("_https_mirror_route_advance")
    assert 'if operation != "git-upload-pack"' in advance
    assert "cursor=edge_route_cursor.cursor+1" in advance
    assert 'if operation == "git-info-refs"' in advance
    assert "clone_sticky" in advance


def test_d1_schema_is_metadata_only_and_contains_no_repository_bytes():
    migration = MIGRATION.read_text(encoding="utf-8")
    private_migration = PRIVATE_MIGRATION.read_text(encoding="utf-8")
    private_access_migration = PRIVATE_ACCESS_MIGRATION.read_text(
        encoding="utf-8")
    schema = SCHEMA.read_text(encoding="utf-8")
    for text in (migration, schema):
        assert "mirror_https_endpoints" in text
        assert "edge_route_cursor" in text
        assert "forkmesh_verified_at" in text
        assert "forkmesh_refs_sha256" in text
        assert "forkmesh_operations_sha256" in text
    assert "repository bytes" in migration.lower()
    assert "private key" in migration.lower()
    assert " BLOB" not in migration
    for text in (private_migration, schema):
        assert "private_mirror_routes" in text
        assert "opaque_replica_id" in text
        assert "replica_sha256" in text
        assert "key_epoch" in text
    assert "repository identities" in private_migration.lower()
    assert "private keys" in private_migration.lower()
    assert " BLOB" not in private_migration
    assert "owner TEXT" not in private_migration
    assert "repository TEXT" not in private_migration
    for text in (private_access_migration, schema):
        assert "idx_private_mirror_route_access" in text
        assert (
            "opaque_replica_id, active, key_epoch, repo_bi" in text)


def test_https_and_private_route_migrations_apply_together():
    database = sqlite3.connect(":memory:")
    try:
        database.executescript(MIGRATION.read_text(encoding="utf-8"))
        database.executescript(PRIVATE_MIGRATION.read_text(encoding="utf-8"))
        database.executescript(
            PRIVATE_ACCESS_MIGRATION.read_text(encoding="utf-8"))
        columns = {
            row[1]
            for row in database.execute(
                "PRAGMA table_info(private_mirror_routes)")
        }
        assert columns == {
            "binding_bi", "repo_bi", "node_bi", "opaque_replica_id",
            "replica_sha256", "key_epoch", "owner_sig", "issued_at",
            "active", "updated_at",
        }
        indexes = {
            row[1]
            for row in database.execute(
                "PRAGMA index_list(private_mirror_routes)")
        }
        assert "idx_private_mirror_route_access" in indexes
    finally:
        database.close()
