#!/usr/bin/env python3
"""Organization-alias repository social metadata regressions.

The browser document is not part of ``org_alias_rewrite``: its public
organization identity must stay in the address bar and social tags while
privacy/catalog reads use the linked node's signed repository row.  Card
requests *are* internally rewritten, so the renderer recovers the public
identity from the untouched Request URL.
"""

import ast
import asyncio
import html
import re
import time
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import quote, unquote, urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")


def _module(name):
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        name, ROOT / "src" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


catalog = _module("catalog")
urls = _module("urls")


def _run(coro):
    return asyncio.run(coro)


def _load_function(name, namespace):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    function = next(
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == name
    )
    scope = dict(namespace)
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=[function], type_ignores=[])),
        str(ENTRY), "exec"), scope)
    return scope[name]


def _load_default_method(name, namespace):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    default = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "Default")
    method = next(
        node for node in default.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == name)
    subject = ast.ClassDef(
        name="_Subject",
        bases=[],
        keywords=[],
        body=[method],
        decorator_list=[],
    )
    scope = dict(namespace)
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=[subject], type_ignores=[])),
        str(ENTRY), "exec"), scope)
    return scope["_Subject"]


class _Response:
    def __init__(self, body, status=200, headers=None):
        self.body = body
        self.status = status
        self.headers = headers or {}


class _AssetResponse:
    def __init__(self, body):
        self.body = body

    async def text(self):
        return self.body


class _Assets:
    async def fetch(self, _url):
        return _AssetResponse(
            "<!doctype html><html><head>"
            "<title>ForkMesh Dashboard</title></head><body></body></html>")


def test_crawler_gets_alias_canonical_card_and_backing_catalog_metadata():
    """A crawler sees forkmesh/forkmesh, never its mirror2 storage identity."""
    reads = []
    cached = []

    async def org_repo_node(_env, owner, repo):
        reads.append(("alias", owner, repo))
        return "mirror2" if (owner, repo) == ("forkmesh", "forkmesh") else ""

    async def repo_is_private(_env, owner, repo):
        reads.append(("privacy", owner, repo))
        return False

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        reads.append(("catalog", value))
        return "repo-bi"

    async def d1_first(_env, sql, *params):
        assert "FROM repositories" in sql
        assert params == ("repo-bi",)
        return {"data": "encrypted"}

    async def decrypt_row(_env, value):
        assert value == "encrypted"
        return {"description": "Current organization repository"}

    async def edge_cache_match(_key):
        return None

    async def edge_cache_put(key, response):
        cached.append((key, response))

    page_class = _load_default_method("_serve_repo_page", {
        "safe_segment": catalog.safe_segment,
        "unquote": unquote,
        "quote": quote,
        "_org_repo_node": org_repo_node,
        "_repo_is_private": repo_is_private,
        "edge_cache_match": edge_cache_match,
        "edge_cache_put": edge_cache_put,
        "DASHBOARD_REPO_ASSET": "repo.html",
        "Response": _Response,
        "ensure_schema": ensure_schema,
        "blind_index": blind_index,
        "d1_first": d1_first,
        "decrypt_row": decrypt_row,
        "_html_escape": html.escape,
        "og_card": SimpleNamespace(CARD_W=1200, CARD_H=630),
    })
    subject = page_class()
    subject.env = SimpleNamespace(ASSETS=_Assets())
    request = SimpleNamespace(headers={
        "user-agent": "Twitterbot/1.0"})
    response = _run(subject._serve_repo_page(
        request, urlparse("https://forkmesh.com/forkmesh/forkmesh")))

    assert response.status == 200
    assert response.headers["cache-control"] == "public, max-age=300"
    assert (
        '<link rel="canonical" href="https://forkmesh.com/forkmesh/forkmesh">'
        in response.body)
    assert (
        '<meta property="og:url" '
        'content="https://forkmesh.com/forkmesh/forkmesh">'
        in response.body)
    assert (
        '<meta property="og:title" content="forkmesh/forkmesh - ForkMesh">'
        in response.body)
    assert (
        '<meta property="og:image" '
        'content="https://forkmesh.com/api/repo/forkmesh/forkmesh/card.png">'
        in response.body)
    assert (
        '<meta name="twitter:image" '
        'content="https://forkmesh.com/api/repo/forkmesh/forkmesh/card.png">'
        in response.body)
    assert "Current organization repository" in response.body
    assert "mirror2" not in response.body
    assert ("privacy", "mirror2", "forkmesh") in reads
    assert ("catalog", "mirror2/forkmesh") in reads
    assert cached[0][0] == "https://forkmesh.com/forkmesh/forkmesh"


class _BinaryResponse:
    @classmethod
    def new(cls, body, init):
        return SimpleNamespace(
            body=body,
            status=init["status"],
            headers=init["headers"])


class _Uint8Array:
    @staticmethod
    def new(value):
        return value


def test_alias_card_renders_public_identity_after_internal_route_rewrite():
    """Card data comes from mirror2, but its visible title remains ForkMesh."""
    rendered = []
    cache_puts = []

    async def repo_is_private(_env, owner, repo):
        assert (owner, repo) == ("mirror2", "forkmesh")
        return False

    async def d1_first(_env, sql, *params):
        if "FROM repositories" in sql:
            assert params == ("repo-bi",)
            return {"data": "encrypted"}
        if "FROM repo_stars" in sql:
            return {"n": 7}
        raise AssertionError(sql)

    async def org_repo_node(_env, owner, repo):
        assert (owner, repo) == ("forkmesh", "forkmesh")
        return "mirror2"

    class Card:
        @staticmethod
        def render_repo_card(info, _now):
            rendered.append(info)
            return b"png"

    handler = _load_function("repo_card_handler", {
        "method_name": lambda request: request.method,
        "json_response": lambda *args, **kwargs: (args, kwargs),
        "ensure_schema": lambda _env: _async_none(),
        "_repo_is_private": repo_is_private,
        "urlparse": urlparse,
        "REPO_CARD_RE": urls.REPO_CARD_RE,
        "safe_segment": catalog.safe_segment,
        "_org_repo_node": org_repo_node,
        "_ap_origin": lambda _env, _request: "https://forkmesh.com",
        "edge_cache_match_media": lambda *_args: _async_value(None),
        "blind_index": lambda _env, value: _async_value(
            "repo-bi" if value == "mirror2/forkmesh" else "wrong-bi"),
        "d1_first": d1_first,
        "decrypt_row": lambda _env, value: _async_value({
            "description": "Catalog from mirror2",
            "branch": "main",
        } if value == "encrypted" else {}),
        "_ap_enabled": lambda _env: _async_value(False),
        "_decrypted_public_catalog": lambda *_args: _async_value([]),
        "repo_mirror_same_group": lambda *_args: False,
        "time": time,
        "og_card": Card,
        "JsResponse": _BinaryResponse,
        "Uint8Array": _Uint8Array,
        "_to_js": lambda value: value,
        "to_js": lambda value: value,
        "edge_cache_put": lambda key, response: _record_async(
            cache_puts, (key, response)),
    })
    request = SimpleNamespace(
        method="GET",
        url="https://forkmesh.com/api/repo/forkmesh/forkmesh/card.png",
        headers={"user-agent": "facebookexternalhit/1.1"},
    )
    response = _run(handler(None, request, "mirror2", "forkmesh"))

    assert response.status == 200
    assert rendered[0]["owner"] == "forkmesh"
    assert rendered[0]["repo"] == "forkmesh"
    assert rendered[0]["description"] == "Catalog from mirror2"
    assert cache_puts[0][0] == (
        "https://forkmesh.com/api/repo/forkmesh/forkmesh/card.png")


async def _async_none():
    return None


async def _async_value(value):
    return value


async def _record_async(collection, value):
    collection.append(value)


def test_alias_html_stays_out_of_internal_path_rewrite():
    """The bare page identity is intentionally resolved inside page serving."""
    rewrite = ENTRY_TEXT[
        ENTRY_TEXT.index("async def org_alias_rewrite"):
        ENTRY_TEXT.index("\n\n\nclass _OrganizationSuccessionRuntime")
    ]
    assert "REPO_API_PREFIX_RE.match" in rewrite
    assert not re.search(
        r"(?:REPO_PAGE|DASHBOARD_REPO).*\\.match", rewrite)

