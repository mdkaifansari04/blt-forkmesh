#!/usr/bin/env python3
"""Git smart-HTTP request-body regression checks (stdlib only)."""

import ast
import gzip
import io
from pathlib import Path

import pytest


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


def _load_decoder():
    """Load the real helper without importing the Workers-only JS modules."""
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "decode_git_request_body"
    )
    module = ast.fix_missing_locations(ast.Module(body=[function], type_ignores=[]))
    namespace = {"gzip": gzip, "io": io}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["decode_git_request_body"]


decode_git_request_body = _load_decoder()


def test_plain_git_request_is_unchanged():
    payload = b"0032want deadbeef\n0000"
    assert decode_git_request_body(payload, "") == payload
    assert decode_git_request_body(payload, "identity") == payload


def test_gzip_git_request_is_decoded_before_upload_pack():
    payload = b"0032want deadbeef\n00000009done\n"
    encoded = gzip.compress(payload)
    assert decode_git_request_body(encoded, "gzip") == payload
    assert decode_git_request_body(encoded, " GZip ") == payload


def test_invalid_or_unsupported_encoding_is_rejected():
    with pytest.raises(ValueError, match="invalid gzip"):
        decode_git_request_body(b"not gzip", "gzip")
    with pytest.raises(ValueError, match="unsupported"):
        decode_git_request_body(b"payload", "br")


def test_decompressed_git_request_size_is_bounded():
    with pytest.raises(ValueError, match="too large"):
        decode_git_request_body(gzip.compress(b"12345"), "gzip", max_bytes=4)


def _method_source(class_name, method_name):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == method_name):
                    return ast.unparse(item)
    raise AssertionError(
        "%s.%s not found in entry.py" % (class_name, method_name))


def test_upload_pack_reply_streams_instead_of_buffering():
    # A full clone's pack (hundreds of MB) must stream chunk-by-chunk to the
    # client via _stream_request. Reassembling it in git_buffers — plus its
    # base64 and bytes() copies — blew the Durable Object's 128 MB memory
    # limit and reset the isolate, killing every in-flight request with it
    # (the site-wide 'hung request' / GIL-poisoning outage). Only the small
    # info/refs advertisement may still buffer: the integrity gate has to
    # hash it whole before releasing it.
    src = _method_source("ForkMeshHost", "_git")
    assert "!= 'git-info-refs'" in src
    assert "_stream_request" in src
    # The buffered leg is unreachable for upload-pack: it's the info-refs tail.
    assert "advertised_refs_canonical" in src


def test_release_and_raw_blobs_stream_too():
    for method in ("_release_blob", "_raw_blob"):
        src = _method_source("ForkMeshHost", method)
        assert "_stream_request" in src
        assert "git_buffers" not in src


def test_stream_request_guards_the_transfer():
    # First sign of life within GIT_TIMEOUT_MS, a watchdog for mid-stream
    # stalls, and chunk bytes copied into JS-owned buffers (a WASM-memory view
    # read off the GIL crashes the runtime, not just the request).
    src = _method_source("ForkMeshHost", "_stream_request")
    assert "TransformStream.new()" in src
    assert "GIT_TIMEOUT_MS" in src
    assert "_stream_watchdog" in src
    ws = _method_source("ForkMeshHost", "webSocketMessage")
    assert "Uint8Array.new(_to_js(data))" in ws
    assert "writer" in ws
    watchdog = _method_source("ForkMeshHost", "_stream_watchdog")
    assert "abort" in watchdog


def test_host_disconnect_aborts_inflight_streams():
    src = _method_source("ForkMeshHost", "_host_disconnected")
    assert "git_streams" in src
    assert "abort" in src
