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
