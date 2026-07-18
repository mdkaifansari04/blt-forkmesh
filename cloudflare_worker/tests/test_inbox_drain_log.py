#!/usr/bin/env python3
"""Inbox drain ack contracts (adhoc #97): exact-id draining keeps a submission
that lands mid-drain alive, and every drain is recorded in inbox_drain_log."""

import ast
import asyncio
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"

FUNCS = {"_drain_ids_from_request", "_drain_issue_inbox", "_log_inbox_drain"}


def _load(extra_globals):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [n for n in tree.body
                if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
                and n.name in FUNCS]
    assert {n.name for n in selected} == FUNCS
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


class _Request:
    def __init__(self, url):
        self.url = url


class _Date:
    @staticmethod
    def now():
        return 1_234


def _env(rows):
    """Fake env whose issue_inbox holds `rows` (list of {id}). Returns (env,
    executed, log)."""
    executed = []
    log = []

    async def d1_first(_env, sql, *args):
        if "COUNT(*)" in sql and "id IN" in sql:
            ids = set(args[1:])
            return {"c": sum(1 for r in rows if r["id"] in ids)}
        if "COUNT(*)" in sql:
            return {"c": len(rows)}
        return {}

    async def d1_run(_env, sql, *args):
        executed.append((sql, args))
        if sql.startswith("INSERT INTO inbox_drain_log"):
            log.append(args)
        elif "id IN" in sql:
            keep = [r for r in rows if r["id"] not in set(args[1:])]
            rows[:] = keep
        elif sql.startswith("DELETE FROM issue_inbox"):
            rows[:] = []

    return {"d1_first": d1_first, "d1_run": d1_run}, executed, log


def test_exact_id_drain_leaves_untouched_rows_and_logs_count():
    rows = [{"id": 1}, {"id": 2}, {"id": 3}]
    env_fakes, executed, log = _env(rows)
    ns = _load({"parse_qs": parse_qs, "urlparse": urlparse, "Date": _Date,
                **env_fakes})
    req = _Request("https://x/api/repo/o/r/issues?owner=o&ts=1&sig=s&ids=1,2")
    removed = asyncio.run(ns["_drain_issue_inbox"](object(), req, "bi:o/r"))
    assert removed == 2
    # Row 3 (filed after the node read ids 1,2) must survive to the next sync.
    assert rows == [{"id": 3}]
    # The drain is recorded: (ts, repo_bi, kind, count).
    assert log == [(1_234, "bi:o/r", "issues", 2)]


def test_no_ids_removes_nothing_and_writes_no_log():
    # Without ?ids= there is no full-drain fallback: an ack that names nothing
    # must leave the queue untouched so items stay until a node acks them by id.
    rows = [{"id": 1}, {"id": 2}]
    env_fakes, executed, log = _env(rows)
    ns = _load({"parse_qs": parse_qs, "urlparse": urlparse, "Date": _Date,
                **env_fakes})
    req = _Request("https://x/api/repo/o/r/issues?owner=o&ts=1&sig=s")
    removed = asyncio.run(ns["_drain_issue_inbox"](object(), req, "bi:o/r"))
    assert removed == 0
    assert rows == [{"id": 1}, {"id": 2}]
    assert not any(sql.startswith("DELETE FROM issue_inbox") for sql, _ in executed)
    assert log == []


def test_acking_already_gone_ids_writes_no_log_row():
    # Ids that no longer match any row (a redelivered ack) drain nothing, so no
    # log entry is written.
    rows = [{"id": 5}]
    env_fakes, executed, log = _env(rows)
    ns = _load({"parse_qs": parse_qs, "urlparse": urlparse, "Date": _Date,
                **env_fakes})
    req = _Request("https://x/api/repo/o/r/issues?owner=o&ts=1&sig=s&ids=1,2")
    removed = asyncio.run(ns["_drain_issue_inbox"](object(), req, "bi:o/r"))
    assert removed == 0
    assert rows == [{"id": 5}]
    assert log == []
