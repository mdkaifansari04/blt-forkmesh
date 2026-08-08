#!/usr/bin/env python3
"""Direct-HTTPS Git transport regression checks."""

import ast
import gzip
import io
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
GIT_HTTP = ROOT / "src" / "git_http.py"


def _load_helper(name, namespace=None):
    tree = ast.parse(GIT_HTTP.read_text(encoding="utf-8"))
    function = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == name)
    scope = dict(namespace or {})
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=[function], type_ignores=[])),
        str(GIT_HTTP), "exec"), scope)
    return scope[name]


decode_git_request_body = _load_helper(
    "decode_git_request_body", {"gzip": gzip, "io": io})
git_advert_cache_key = _load_helper("git_advert_cache_key")


def _method_source(class_name, method_name):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for child in node.body:
                if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)) \
                        and child.name == method_name:
                    return ast.unparse(child)
    raise AssertionError(f"{class_name}.{method_name} not found")


def test_plain_and_gzip_git_request_decoding_is_bounded():
    payload = b"0032want deadbeef\n00000009done\n"
    assert decode_git_request_body(payload, "") == payload
    assert decode_git_request_body(payload, "identity") == payload
    assert decode_git_request_body(gzip.compress(payload), " GZip ") == payload
    with pytest.raises(ValueError, match="invalid gzip"):
        decode_git_request_body(b"not gzip", "gzip")
    with pytest.raises(ValueError, match="unsupported"):
        decode_git_request_body(payload, "br")
    with pytest.raises(ValueError, match="too large"):
        decode_git_request_body(gzip.compress(b"12345"), "gzip", max_bytes=4)


def test_git_advert_cache_key_rotates_with_attested_state():
    first = git_advert_cache_key("Forkmesh", "Forkmesh", "ABC123")
    assert first == (
        "https://forkmesh.internal/git-advert/forkmesh/forkmesh/"
        "git-upload-pack/abc123")
    assert git_advert_cache_key("forkmesh", "forkmesh", "abc123") == first
    assert git_advert_cache_key("forkmesh", "forkmesh", "def456") != first
    assert git_advert_cache_key("forkmesh", "forkmesh", "") is None
    assert git_advert_cache_key("", "forkmesh", "abc123") is None


def test_public_clone_uses_signed_direct_https_and_sticky_selection():
    source = ENTRY.read_text(encoding="utf-8")
    tree = ast.parse(source)
    route = _method_source("Default", "_route")
    assert "_https_mirror_proxy(" in route
    assert "'git-info-refs'" in route
    assert "'git-upload-pack'" in route

    proxy = ast.unparse(next(
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_https_mirror_proxy"))
    assert "https_routing.request_message" in proxy
    assert "ed25519_sign" in proxy
    assert "JsResponse.new(upstream.body" in proxy
    assert "FORKMESH_HOST" not in proxy

    advance = ast.unparse(next(
        node for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name == "_https_mirror_route_advance"))
    assert "clone_sticky" in advance
    assert "operation == 'git-info-refs'" in advance


def test_receive_pack_fails_closed_without_consuming_request_body():
    push = _method_source("Default", "_git_push")
    assert "direct_https_receive_pack_required" in push
    assert "repositoryBytesAccepted" in push
    assert "socketFallback" in push
    assert "request.bytes" not in push
    assert "request.text" not in push


def test_batched_blobs_use_direct_https_gateway():
    route = _method_source("Default", "_route")
    gateway = (ROOT.parent / "tools" / "mirror_gateway.py").read_text(
        encoding="utf-8")
    assert "'blobs'" in route
    assert "_https_mirror_proxy" in route
    assert "list(paths)[:60]" in gateway
