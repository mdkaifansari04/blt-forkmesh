#!/usr/bin/env python3
"""Performance budgets for hot worker endpoints (issue #357).

Startup, the Commits tab and the lazy list tabs were all found and fixed "by
feel" on the desktop client. These two guard the equivalent hot paths on the
worker: `catalog_handler` (GET /api/repositories) must stay linear in the repo
count, and the batched `/blobs` action (ForkMeshHost.fetch) must actually fan
its reads out concurrently over the tunnel rather than serializing them --
that concurrency is the whole point of batching (see MAX_BLOB_BATCH above its
definition in entry.py). Budgets are calibrated generously against what these
pure-Python stand-ins measure here; a real deploy's D1/tunnel latency dwarfs
anything a regression in this code could add, so the point is to catch a
step-function regression (an accidental O(n^2) loop, or a fan-out that
silently became sequential), not to chase milliseconds.

Skippable per-environment (sandboxed CI can be slow/noisy) without losing
functional coverage elsewhere: set FORKMESH_SKIP_PERF_TESTS=1.
"""

import ast
import asyncio
import os
import re
import time
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import parse_qs, unquote, urlparse

import pytest

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"
CATALOG = ENTRY.parent / "catalog.py"
# Mirror grouping / clone-selection helpers were extracted from entry.py into
# mirrors.py, and the catalog-record sanitization helpers into catalog.py;
# parse all sources so the AST loaders below still find them.
_WORKER_SRC = (
    ENTRY.read_text(encoding="utf-8") + "\n"
    + MIRRORS.read_text(encoding="utf-8") + "\n"
    + CATALOG.read_text(encoding="utf-8"))

pytestmark = pytest.mark.skipif(
    os.environ.get("FORKMESH_SKIP_PERF_TESTS") == "1",
    reason="performance budget tests disabled via FORKMESH_SKIP_PERF_TESTS",
)


def _load(*names, extra_globals=None):
    tree = ast.parse(_WORKER_SRC, filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


def _load_method(class_name, method, extra_globals=None):
    """Pull a single async method's body out of a class, as a free function
    taking `self` explicitly. Lets the real "blobs" fan-out code in
    ForkMeshHost.fetch run against a stub self/env instead of the full
    Durable Object/WebSocket machinery."""
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    found = None
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == method):
                    found = item
                    break
    assert found is not None, "missing %s.%s" % (class_name, method)
    module = ast.fix_missing_locations(ast.Module(body=[found], type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace[method]


NOW = 1_700_000_000_000


# --- GET /api/repositories (catalog_handler) --------------------------------

def _make_repos(n):
    return [
        {
            "owner": "owner%d" % i,
            "name": "repo%d" % i,
            "visibility": "public",
            "rootCommit": "root%d" % i,
            "updatedAt": str(NOW - i),
        }
        for i in range(n)
    ]


def _run_catalog_get(n_repos):
    repos = _make_repos(n_repos)

    async def d1_all(env, sql, *args):
        if "FROM repositories" in sql:
            return [{"key_bi": "bi%d" % i, "data": r} for i, r in enumerate(repos)]
        return []  # host_presence query -> nobody currently live

    async def noop(*a, **k):
        return None

    async def identity(env, data):
        return data

    async def const_none(*a, **k):
        return None

    def json_response(payload, status=200, cache_seconds=None, cache_control=None):
        return payload

    ns = _load(
        "catalog_handler", "method_name", "safe_segment", "clean_string",
        "served_mirror_groups", "repo_clone_online", "repo_mirror_group_key",
        extra_globals={
            "Date": type("D", (), {"now": staticmethod(lambda: NOW)}),
            "HOST_PRESENCE_STALE_MS": 10 * 60 * 1000,
            "MAX_REPO_SEGMENT": 80,
            "MAX_CATALOG_REPOS": 200,
            "CATALOG_TTL": 10,
            "CATALOG_CACHE_KEY": "catalog",
            "ROOM_NAME_RE": re.compile(r"^[A-Za-z0-9._:-]+$"),
            "unquote": unquote,
            "ensure_schema": noop,
            "purge_blocked_catalog": noop,
            "verify_catalog_view_token": const_none,
            "edge_cache_match": const_none,
            "edge_cache_put": noop,
            "decrypt_row": identity,
            "_is_blocked_catalog_identity": (lambda env, o, n: False),
            "d1_all": d1_all,
            "json_response": json_response,
            "urlparse": urlparse,
            "parse_qs": parse_qs,
        },
    )

    request = SimpleNamespace(
        method="GET", url="https://forkmesh.internal/api/repositories")
    start = time.perf_counter()
    result = asyncio.run(ns["catalog_handler"](None, request))
    elapsed = time.perf_counter() - start
    return result, elapsed


def test_catalog_get_returns_expected_repos():
    result, _ = _run_catalog_get(50)
    assert result["ok"] is True
    assert len(result["repositories"]) == 50


def test_catalog_get_stays_within_budget_at_scale():
    # 3000 repos is well beyond MAX_CATALOG_REPOS (200) and far more than any
    # real owner base today; this is here to catch an accidental O(n^2) (e.g.
    # a nested per-repo scan) rather than to model production load.
    result, elapsed = _run_catalog_get(3000)
    assert result["ok"] is True
    budget_s = 0.5
    assert elapsed < budget_s, (
        "GET /api/repositories took %.3fs for 3000 repos, over the %.1fs "
        "budget -- looks like a regression from O(n) to something worse"
        % (elapsed, budget_s))


# --- batched /blobs (ForkMeshHost.fetch) ------------------------------------

def _run_blobs_fetch(n_paths, per_blob_delay_s):
    calls = []

    async def tunnel_result(self, op, path, ref):
        calls.append(path)
        await asyncio.sleep(per_blob_delay_s)
        return {"ok": True, "path": path}

    def json_response(payload, status=200, cache_seconds=None, cache_control=None):
        return payload

    fetch = _load_method(
        "ForkMeshHost", "fetch",
        extra_globals={
            "urlparse": urlparse,
            "parse_qs": parse_qs,
            "asyncio": asyncio,
            "MAX_BLOB_BATCH": 60,
            "REPO_HOST_RE": re.compile(
                r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blobs|blob|raw|history"
                r"|commit|branches)$"),
            "RELEASE_BLOB_RE": re.compile(
                r"^/api/repo/([^/]+)/([^/]+)/releases/blob/sha256/"
                r"([0-9a-f]{64})$"),
            "safe_segment": (lambda v, *a: v),
            "json_response": json_response,
        },
    )

    host = SimpleNamespace(
        _ensure=lambda: None,
        _rate_ok=lambda: True,
        _mark_present=(lambda path=None: _noop()),
    )
    # `self._tunnel_result(op, path, ref)` is a plain attribute lookup on
    # `host` (not a real bound method), so the stub takes exactly those args.
    host._tunnel_result = lambda op, path, ref: tunnel_result(host, op, path, ref)

    paths = ["dir/file%d.md" % i for i in range(n_paths)]
    query = "&".join("path=" + p for p in paths)
    request = SimpleNamespace(
        method="GET",
        url="https://forkmesh.internal/api/repo/me/myrepo/blobs?" + query,
        headers=SimpleNamespace(get=lambda k: None),
    )

    start = time.perf_counter()
    result = asyncio.run(fetch(host, request))
    elapsed = time.perf_counter() - start
    return result, elapsed, calls


async def _noop():
    return None


def test_batched_blobs_returns_all_requested_paths():
    result, _, calls = _run_blobs_fetch(10, per_blob_delay_s=0.001)
    assert result["ok"] is True
    assert len(result["blobs"]) == 10
    assert len(calls) == 10


def test_batched_blobs_fans_out_concurrently_not_sequentially():
    # The whole point of the batched endpoint (see MAX_BLOB_BATCH's comment)
    # is that N per-file reads over the tunnel happen concurrently instead of
    # the website making N separate HTTP round trips. If a future change
    # silently turned the asyncio.gather fan-out into a sequential `await` in
    # a loop, N * per_blob_delay would show up directly in the wall time.
    n_paths = 40
    per_blob_delay_s = 0.05
    _, elapsed, calls = _run_blobs_fetch(n_paths, per_blob_delay_s)
    assert len(calls) == n_paths
    sequential_s = n_paths * per_blob_delay_s
    # Generous budget: well under half of the fully-sequential time, so this
    # only fails on a genuine loss of concurrency, not scheduler jitter.
    budget_s = sequential_s / 2
    assert elapsed < budget_s, (
        "batched /blobs took %.3fs for %d paths (%.3fs each); expected close "
        "to one delay (%.3fs) via concurrent fan-out, not %.3fs sequential -- "
        "looks like the asyncio.gather batching regressed to a sequential loop"
        % (elapsed, n_paths, per_blob_delay_s, per_blob_delay_s, sequential_s))
