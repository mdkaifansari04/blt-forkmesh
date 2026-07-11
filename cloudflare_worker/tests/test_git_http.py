#!/usr/bin/env python3
"""Git smart-HTTP request-body regression checks (stdlib only)."""

import ast
import gzip
import io
from pathlib import Path

import pytest


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# decode_git_request_body now lives in the extracted stdlib-only git_http.py
# module; the ForkMeshHost/Default method-source checks below still read entry.py.
GIT_HTTP = ENTRY.parent / "git_http.py"


def _load_decoder():
    """Load the real helper without importing the Workers-only JS modules."""
    tree = ast.parse(GIT_HTTP.read_text(encoding="utf-8"), filename=str(GIT_HTTP))
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "decode_git_request_body"
    )
    module = ast.fix_missing_locations(ast.Module(body=[function], type_ignores=[]))
    namespace = {"gzip": gzip, "io": io}
    exec(compile(module, str(GIT_HTTP), "exec"), namespace)
    return namespace["decode_git_request_body"]


decode_git_request_body = _load_decoder()


def _load_git_http_func(name):
    """Load a stdlib-only helper from git_http.py without the Workers JS deps."""
    tree = ast.parse(GIT_HTTP.read_text(encoding="utf-8"), filename=str(GIT_HTTP))
    function = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == name
    )
    module = ast.fix_missing_locations(ast.Module(body=[function], type_ignores=[]))
    namespace = {}
    exec(compile(module, str(GIT_HTTP), "exec"), namespace)
    return namespace[name]


git_advert_cache_key = _load_git_http_func("git_advert_cache_key")


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
    assert "op == 'git-upload-pack'" in src
    assert "_stream_request" in src
    # The buffered leg is unreachable for upload-pack: it's the info-refs tail.
    assert "advertised_refs_canonical" in src


def test_git_advert_cache_key_rotates_with_the_state_hash():
    # The clone-handshake edge-cache key embeds the repo's attested state hash so
    # a push (which republishes a new hash) rotates the key and forces a fresh
    # fetch, while an unchanged state reuses the same key (a colo cache hit).
    a = git_advert_cache_key("Forkmesh", "Forkmesh", "ABC123")
    assert a == "https://forkmesh.internal/git-advert/forkmesh/forkmesh/git-upload-pack/abc123"
    # Case-insensitive and stable for the same logical (owner, repo, state).
    assert git_advert_cache_key("forkmesh", "forkmesh", "abc123") == a
    # A changed state hash yields a different key.
    assert git_advert_cache_key("forkmesh", "forkmesh", "def456") != a
    # The service is part of the key so upload-pack and receive-pack never alias.
    assert git_advert_cache_key(
        "forkmesh", "forkmesh", "abc123", "git-receive-pack") != a


def test_git_advert_cache_key_is_none_without_a_state_hash():
    # No attested state (never-published / legacy repo) => uncacheable, serve live.
    assert git_advert_cache_key("forkmesh", "forkmesh", "") is None
    assert git_advert_cache_key("forkmesh", "forkmesh", None) is None
    assert git_advert_cache_key("", "forkmesh", "abc123") is None
    assert git_advert_cache_key("forkmesh", "", "abc123") is None


def test_clone_handshake_is_edge_cached_only_on_a_stable_source_state():
    # The info/refs upload-pack ref advertisement is served from the colo edge
    # cache keyed on the repo's own attested state hash, but ONLY when this
    # namespace is the working-copy holder (source == local-node) and its host is
    # live — so a mirror's (possibly older) refs are never cached under the
    # source's current-state key, and private repos are never cached.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))

    def _default_method(name):
        for node in ast.walk(tree):
            if isinstance(node, ast.ClassDef):
                for item in node.body:
                    if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                            and item.name == name):
                        return ast.unparse(item)
        raise AssertionError("%s not found in entry.py" % name)

    # The upload-pack info/refs branch routes through the caching wrapper.
    route = _default_method("_route")
    assert "_git_advert_cached(" in route

    wrapper = _default_method("_git_advert_cached")
    assert "git_advert_cache_get(cache_key)" in wrapper
    assert "git_advert_cache_put(cache_key, response)" in wrapper
    # Store only a healthy (200) advertisement served by a live source.
    assert "int(response.status) == 200" in wrapper
    assert "_source_has_live_host(owner, repo)" in wrapper
    # A miss still runs the full serving path (fallback + integrity gate).
    assert "self._git_host(request, owner_raw, repo_raw)" in wrapper

    keyer = _default_method("_clone_advert_cache_key")
    assert "is_private" in keyer                       # private repos: no cache
    assert "'local-node'" in keyer                     # only the source of truth
    assert "git_advert_cache_key(owner, repo, rec.get('stateHash'))" in keyer


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


def test_git_advertisements_use_short_timeout_before_buffering():
    # info/refs is buffered for integrity checking, so it must fail faster than
    # a long streaming pack. Otherwise the outer Worker can be canceled as hung
    # before the DO returns its 504 and the router can fail over to a mirror.
    src = _method_source("ForkMeshHost", "_git")
    assert "is_advertise" in src
    assert "GIT_ADVERTISE_TIMEOUT_MS" in src
    assert "GIT_TIMEOUT_MS" in src
    assert "timeout_ms / 1000" in src


def test_host_disconnect_aborts_inflight_streams():
    src = _method_source("ForkMeshHost", "_host_disconnected")
    assert "git_streams" in src
    assert "abort" in src


def test_push_route_is_owner_key_gated_and_never_hits_a_mirror():
    # git push (issue #358): the router gates receive-pack on an owner-key-signed
    # HTTP Basic token and dispatches straight to the OWNER's host DO — never the
    # clone mirror fallback (a mirror is read-only; a push must reach the working
    # copy holder). Both the info/refs advertisement and the POST route here.
    entry = ENTRY.read_text(encoding="utf-8")
    assert "GIT_RECEIVE_RE" in entry
    push = _method_source("Default", "_git_push")
    assert "_basic_auth_push_ok" in push
    assert "_basic_auth_challenge" in push
    assert "idFromName(f'host:{owner}/{repo}')" in push
    # No mirror fallback in the push path.
    assert "_sticky_clone_fallback" not in push
    assert "_forward_to_node" not in push


def test_push_token_binds_owner_repo_with_a_distinct_prefix():
    # verify_push_token mirrors the host/view tokens (fresh ts + ed25519 over an
    # explicit canonical), but a distinct prefix so a host or view token can't be
    # replayed as a push token. Only the owner account's key may push for now.
    entry = ENTRY.read_text(encoding="utf-8")
    assert '"forkmesh-push-v1\\n"' in entry or "'forkmesh-push-v1\\n'" in entry
    tree = ast.parse(entry, filename=str(ENTRY))
    fn = next(n for n in tree.body
              if isinstance(n, ast.AsyncFunctionDef) and n.name == "verify_push_token")
    body = ast.unparse(fn)
    assert "ed25519_verify" in body
    assert "_ts_ok" in body
    assert "_owner_pubkey" in body


def test_receive_pack_request_body_streams_and_is_never_buffered():
    # The pushed pack (client->host, potentially hundreds of MB) must stream
    # straight through the tunnel to the host's receive-pack stdin. Buffering a
    # pack in the isolate is what OOM'd the DO once — so the push POST path must
    # NOT read request.bytes()/decode_git_request_body, and must pump the body
    # via a reader as git-req-chunk messages.
    recv = _method_source("ForkMeshHost", "_stream_receive")
    assert "_pump_request_body" in recv
    assert "TransformStream.new()" in recv
    assert "_stream_watchdog" in recv
    pump = _method_source("ForkMeshHost", "_pump_request_body")
    assert "getReader()" in pump
    assert "git-req-chunk" in pump
    assert "git-req-end" in pump
    assert "GIT_REQ_CHUNK" in pump
    # The DO's receive-pack POST leg forwards to _git without buffering the body.
    fetch = _method_source("ForkMeshHost", "_fetch_inner")
    assert "'/git-receive-pack'" in fetch
    assert "self._git(request, 'git-receive-pack')" in fetch


def test_host_do_fetch_captures_exceptions_instead_of_error_1101():
    # An exception escaping ForkMeshHost.fetch never reaches Default.fetch's
    # Sentry capture (the router awaits a DO *stub*), so Cloudflare logged only
    # a bare "outcome: exception" with no traceback anywhere — adhoc #17: a
    # git-upload-pack POST dying opaquely with Error 1101. fetch must stay a
    # guard around _fetch_inner: capture the real stack to Sentry, then answer
    # a retryable 503 the router's mirror fallback already understands.
    src = _method_source("ForkMeshHost", "fetch")
    assert "await self._fetch_inner(request)" in src
    assert "capture_worker_exception" in src
    assert "status=503" in src


def test_mark_present_never_fails_the_request_it_rides_on():
    # _mark_present is documented best-effort ("never fails the call") and runs
    # ahead of every git/browse request AND inside the WebSocket heartbeat
    # handler (where the fetch guard can't help). The blind-index derivation it
    # awaits (crypto + D1) must not be able to take the request down with it.
    src = _method_source("ForkMeshHost", "_mark_present")
    assert "try:" in src
    assert "_repo_blind_index" in src.split("try:", 1)[1]


def test_git_advertises_receive_pack_without_the_clone_integrity_gate():
    # The receive-pack ref advertisement uses its own service string and content
    # type, and skips the clone integrity pin (a push targets the owner's source,
    # not a mirror — the pin is re-attested AFTER the push lands).
    src = _method_source("ForkMeshHost", "_git")
    assert "git-receive-info-refs" in src
    assert "x-%s-advertisement" in src
    assert "if op == 'git-info-refs':" in src  # pin gate scoped to upload-pack


def test_blobs_batch_endpoint_reads_many_files_in_one_request():
    # /api/repo/o/r/blobs collects up to MAX_BLOB_BATCH files in ONE HTTP
    # request (repeated ?path= params), fanned out concurrently over the live
    # tunnel inside the DO — replacing the website's per-record /blob fan-out.
    # When nothing can be served it surfaces 503/504 so the router's mirror
    # fallback re-routes instead of caching a 200 full of nulls.
    entry = ENTRY.read_text(encoding="utf-8")
    urls = (ENTRY.parent / "urls.py").read_text(encoding="utf-8")
    assert "blobs|blob" in urls  # routed (REPO_HOST_RE) + gated like other ops
    assert 'if action == "blobs":' in entry
    assert "MAX_BLOB_BATCH" in entry
    assert "asyncio.gather" in entry
    src = _method_source("ForkMeshHost", "_fetch_inner")
    assert "_tunnel_result('blob', p, ref, ua)" in src
    assert "503 if 503 in stats else 504 if 504 in stats else 502" in src
