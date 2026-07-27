#!/usr/bin/env python3
"""Transient HTTPS verifier failures preserve the last signed health lease."""

import ast
import asyncio
import json
import sqlite3
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import quote, urlparse

import pytest


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
NOW = 1_700_000_000_000


def _function_source(name):
    source = ENTRY.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(ENTRY))
    node = next(
        item for item in tree.body
        if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
        and item.name == name
    )
    return ast.get_source_segment(source, node)


def test_transient_marker_preserves_last_verified_lease():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.executescript(
        """
        CREATE TABLE mirror_https_endpoints (
            node_bi TEXT PRIMARY KEY,
            checked_at INTEGER NOT NULL,
            healthy INTEGER NOT NULL,
            integrity TEXT NOT NULL,
            forkmesh_verified_at INTEGER NOT NULL,
            forkmesh_active INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
        INSERT INTO mirror_https_endpoints VALUES (
            'mirror-bi', 700, 1, 'ok', 700, 1, 700
        );
        """
    )

    async def d1_run(_env, sql, *args):
        db.execute(sql, args)
        db.commit()

    namespace = {"d1_run": d1_run}
    exec(_function_source("_https_mirror_mark_transient"), namespace)
    asyncio.run(namespace["_https_mirror_mark_transient"](
        object(), {"node_bi": "mirror-bi"}, 900
    ))

    row = dict(db.execute(
        "SELECT * FROM mirror_https_endpoints WHERE node_bi='mirror-bi'"
    ).fetchone())
    assert row == {
        "node_bi": "mirror-bi",
        "checked_at": 700,
        "healthy": 1,
        "integrity": "ok",
        "forkmesh_verified_at": 700,
        "forkmesh_active": 1,
        "updated_at": 900,
    }


def _dns_result(*, documents, fetch_result=(200, '{"Status":0}'),
                proxied=False, base_url="https://mirror.example"):
    calls = []

    async def edge_documents():
        return documents

    async def fetch_text(*args):
        calls.append(args)
        return fetch_result

    routing = SimpleNamespace(
        normalize_base_url=lambda value: value,
        cloudflare_proxied_dns_answers=(
            lambda payloads, cidrs: bool(proxied)
        ),
    )
    namespace = {
        "Date": type("D", (), {"now": staticmethod(lambda: NOW)}),
        "https_routing": routing,
        "urlparse": urlparse,
        "quote": quote,
        "json": json,
        "_HTTPS_MIRROR_EDGE_DNS_MEMO": {},
        "HTTPS_MIRROR_EDGE_DNS_TTL_MS": 120_000,
        "HTTPS_MIRROR_MANIFEST_MAX_BYTES": 16_384,
        "HTTPS_MIRROR_CONTROL_FETCH_TIMEOUT_SECONDS": 5,
        "_cloudflare_edge_range_documents": edge_documents,
        "_https_mirror_fetch_text": fetch_text,
    }
    exec(_function_source("_https_mirror_cloudflare_dns_ok"), namespace)
    result = asyncio.run(
        namespace["_https_mirror_cloudflare_dns_ok"](base_url)
    )
    return result, calls


def test_cloudflare_dns_check_distinguishes_unavailable_from_rejected():
    unavailable, calls = _dns_result(documents=())
    assert unavailable is None
    assert calls == []

    unavailable, calls = _dns_result(
        documents=("173.245.48.0/20", "2400:cb00::/32"),
        fetch_result=(0, ""),
    )
    assert unavailable is None
    assert len(calls) == 1

    rejected, calls = _dns_result(
        documents=("173.245.48.0/20", "2400:cb00::/32"),
        proxied=False,
    )
    assert rejected is False
    assert len(calls) == 2

    invalid, calls = _dns_result(
        documents=("173.245.48.0/20", "2400:cb00::/32"),
        base_url="",
    )
    assert invalid is False
    assert calls == []


@pytest.mark.parametrize(
    ("dns_result", "fetch_result", "expected_marker"),
    [
        (None, (200, "{}"), "transient"),
        (True, (0, ""), "transient"),
        (True, (503, ""), "transient"),
        (True, (401, ""), "failed"),
        (True, (403, ""), "failed"),
        (True, (404, ""), "failed"),
    ],
)
def test_health_probe_distinguishes_transient_from_definitive_failures(
        dns_result, fetch_result, expected_marker):
    events = []

    async def owner_signing_pubkeys(_env, _node):
        return ("node-public-key",)

    async def dns_ok(_base_url):
        events.append("dns")
        return dns_result

    async def fetch_text(*_args):
        events.append("fetch")
        return fetch_result

    async def mark_transient(_env, _row, _now):
        events.append("transient")

    async def mark_failed(_env, _row, _now):
        events.append("failed")

    namespace = {
        "Date": type("D", (), {"now": staticmethod(lambda: NOW)}),
        "MAX_NODE_NAME": 80,
        "clean_string": lambda value, limit: str(value or "")[:limit],
        "valid_node_name": lambda value: value == "mirror",
        "valid_node_pubkey": lambda value: value == "node-public-key",
        "https_routing": SimpleNamespace(
            normalize_base_url=lambda value: str(value or "")
        ),
        "_owner_signing_pubkeys": owner_signing_pubkeys,
        "_https_mirror_cloudflare_dns_ok": dns_ok,
        "_https_mirror_fetch_text": fetch_text,
        "_https_mirror_mark_transient": mark_transient,
        "_https_mirror_mark_failed": mark_failed,
        "_b64url_encode": lambda _value: "nonce",
        "_random_bytes": lambda _size: b"nonce",
        "quote": quote,
        "json": json,
        "HTTPS_MIRROR_MANIFEST_MAX_BYTES": 16_384,
        "HTTPS_MIRROR_CONTROL_FETCH_TIMEOUT_SECONDS": 5,
        "HTTPS_MIRROR_HEALTH_TRANSIENT_STATUSES": {
            408, 425, 429, 500, 502, 503, 504,
        },
    }
    exec(_function_source("_https_mirror_health_one"), namespace)

    active = asyncio.run(namespace["_https_mirror_health_one"](
        object(),
        {
            "node_bi": "mirror-bi",
            "node_name": "mirror",
            "base_url": "https://mirror.example",
            "public_key": "node-public-key",
        },
        frozenset({"a" * 64}),
    ))

    assert active is False
    assert events[-1] == expected_marker
    assert (
        "failed" not in events
        if expected_marker == "transient"
        else "transient" not in events
    )
    if dns_result is None:
        assert "fetch" not in events
    else:
        assert "fetch" in events


def _load_health_cron(*, rows, accepted, batch=10):
    selected = []
    queries = []

    async def ensure_schema(_env):
        return None

    async def d1_all(_env, sql, *args):
        queries.append((sql, args))
        return rows(sql, args) if callable(rows) else list(rows)

    async def accepted_refs(_env):
        return accepted

    async def health_one(_env, row, refs):
        selected.append((row["node_name"], refs))
        return True

    namespace = {
        "HTTPS_MIRROR_HEALTH_BATCH": batch,
        "ensure_schema": ensure_schema,
        "d1_all": d1_all,
        "_https_mirror_accepted_forkmesh_refs": accepted_refs,
        "_https_mirror_health_one": health_one,
    }
    exec(_function_source("https_mirror_health_cron"), namespace)
    asyncio.run(namespace["https_mirror_health_cron"](object()))
    return selected, queries


def test_health_cron_skips_endpoint_checks_without_accepted_refs():
    selected, queries = _load_health_cron(
        rows=[{
            "node_bi": "mirror-bi",
            "node_name": "mirror",
            "base_url": "https://mirror.example",
            "public_key": "node-public-key",
        }],
        accepted=frozenset(),
    )

    assert len(queries) == 1
    assert selected == []


def test_health_cron_schedules_by_latest_attempt_not_old_success():
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    db.executescript(
        """
        CREATE TABLE mirror_https_endpoints (
            node_bi TEXT PRIMARY KEY,
            node_name TEXT NOT NULL,
            base_url TEXT NOT NULL,
            public_key TEXT NOT NULL,
            checked_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
        INSERT INTO mirror_https_endpoints VALUES
            ('flaky-bi', 'flaky', 'https://flaky.example', 'key', 0, 900),
            ('old-bi', 'old', 'https://old.example', 'key', 100, 100),
            ('middle-bi', 'middle', 'https://middle.example', 'key', 200, 50);
        """
    )

    def query_rows(sql, args):
        return [
            dict(row)
            for row in db.execute(sql, args).fetchall()
        ]

    refs = frozenset({"a" * 64})
    selected, queries = _load_health_cron(
        rows=query_rows,
        accepted=refs,
        batch=2,
    )

    assert selected == [("old", refs), ("middle", refs)]
    sql, args = queries[0]
    assert "updated_at" in sql
    assert "checked_at" in sql
    assert args == (2,)
