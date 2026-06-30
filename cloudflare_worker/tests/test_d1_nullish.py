#!/usr/bin/env python3
"""Regression tests for D1 row conversion helpers.

Workers Python may return JavaScript null/undefined sentinels rather than Python
None from D1 first()/all(). Load only the small pure helpers from entry.py so
this test runs without the Workers JS runtime.
"""
import ast
import asyncio
from pathlib import Path

ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
_WANT_FUNCS = ("js_nullish", "d1_row_to_dict", "d1_all", "d1_first")


def _load():
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    body = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in _WANT_FUNCS:
            body.append(node)
        if isinstance(node, ast.AsyncFunctionDef) and node.name in _WANT_FUNCS:
            body.append(node)
    mod = ast.Module(body=body, type_ignores=[])
    ast.fix_missing_locations(mod)
    ns = {}
    exec(compile(mod, str(ENTRY), "exec"), ns)
    return ns


class JsNull:
    pass


class JsUndefined:
    pass


class _Stmt:
    def __init__(self, row=None, results=None):
        self.row = row
        self.results = results
        self.bound = ()

    def bind(self, *args):
        self.bound = args
        return self

    async def first(self):
        return self.row

    async def all(self):
        return type("D1Result", (), {"results": self.results})()


class _DB:
    def __init__(self, row=None, results=None):
        self.stmt = _Stmt(row=row, results=results)

    def prepare(self, sql):
        self.sql = sql
        return self.stmt


class _Env:
    def __init__(self, row=None, results=None):
        self.DB = _DB(row=row, results=results)


def test_js_nullish_detects_workers_js_nullish_sentinels():
    ns = _load()

    assert ns["js_nullish"](JsNull()) is True
    assert ns["js_nullish"](JsUndefined()) is True
    assert ns["js_nullish"](None) is True
    assert ns["js_nullish"]({}) is False


def test_d1_first_treats_js_nullish_as_no_row():
    ns = _load()

    for sentinel in (JsNull(), JsUndefined()):
        row = asyncio.run(ns["d1_first"](_Env(row=sentinel), "SELECT data FROM accounts WHERE name_bi=?", "abc"))
        assert row is None


def test_d1_first_keeps_real_rows():
    ns = _load()

    row = asyncio.run(ns["d1_first"](_Env(row={"data": "ok"}), "SELECT data FROM accounts WHERE name_bi=?", "abc"))

    assert row == {"data": "ok"}


def test_d1_all_treats_js_nullish_results_as_empty():
    ns = _load()

    for sentinel in (JsNull(), JsUndefined()):
        rows = asyncio.run(ns["d1_all"](_Env(results=sentinel), "SELECT data FROM accounts"))
        assert rows == []


def test_d1_all_skips_js_nullish_rows_and_keeps_real_rows():
    ns = _load()

    rows = asyncio.run(ns["d1_all"](_Env(results=[JsNull(), JsUndefined(), {"data": "ok"}]), "SELECT data FROM accounts"))

    assert rows == [{"data": "ok"}]


if __name__ == "__main__":
    for test in (
        test_js_nullish_detects_workers_js_nullish_sentinels,
        test_d1_first_treats_js_nullish_as_no_row,
        test_d1_first_keeps_real_rows,
        test_d1_all_treats_js_nullish_results_as_empty,
        test_d1_all_skips_js_nullish_rows_and_keeps_real_rows,
    ):
        test()
        print("PASS", test.__name__)
