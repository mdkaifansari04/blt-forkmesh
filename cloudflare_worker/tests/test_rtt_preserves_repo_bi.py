#!/usr/bin/env python3
"""_update_rtt must preserve repo_bi in the WebSocket attachment.

After the first successful tunnel response _update_rtt rewrites the
serializeAttachment blob.  serializeAttachment replaces the *whole* attachment,
so if repo_bi is omitted the heartbeat handler (which reads repo_bi from the
attachment after DO hibernation to call _mark_present) silently breaks.  The
mirror's host_presence row then stops being refreshed and after
HOST_PRESENCE_STALE_MS (10 min) the mirror looks offline to
_select_browse_mirror / _select_clone_fallback — serving stops.

These tests lock in the fix via source inspection so the regression can't
re-appear silently."""

import ast
import types
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


def _forkmesh_host_method_src(name):
    """Return the unparsed source of ForkMeshHost.<name>."""
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == "ForkMeshHost":
            for item in node.body:
                if isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    if item.name == name:
                        return ast.unparse(item)
    raise AssertionError("ForkMeshHost.%s not found in entry.py" % name)


def _load_top(*names):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    selected = [
        n for n in tree.body
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
        and n.name in names
    ]
    found = {n.name for n in selected}
    missing = set(names) - found
    assert not missing, "missing top-level functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = {}
    exec(compile(module, str(ENTRY), "exec"), ns)
    return [ns[n] for n in names]


# ── source-level checks ──────────────────────────────────────────────────────

def test_update_rtt_passes_repo_bi_to_serialize_attachment():
    src = _forkmesh_host_method_src("_update_rtt")
    # The attachment write must include repo_bi so it isn't wiped after the
    # first tunnel response.
    assert "repo_bi" in src, (
        "_update_rtt does not pass repo_bi to serializeAttachment — "
        "heartbeat-based presence refresh breaks after DO hibernation"
    )


def test_update_rtt_reads_repo_bi_from_existing_attachment():
    src = _forkmesh_host_method_src("_update_rtt")
    # Must read the current repo_bi back (via _ws_attr) rather than
    # hard-coding a value, so it works for any connected socket.
    assert "_ws_attr(ws, 'repo_bi')" in src or '_ws_attr(ws, "repo_bi")' in src, (
        "_update_rtt must read repo_bi from the existing attachment via "
        "_ws_attr so it is preserved across serializeAttachment calls"
    )


# ── behavioural checks ───────────────────────────────────────────────────────

def _make_fake_to_js():
    return lambda x: x   # identity — no JS runtime in tests


class _FakeWs:
    """Minimal WebSocket stub whose attachment is a plain Python dict."""

    def __init__(self, initial):
        self._att = dict(initial)

    def deserializeAttachment(self):
        # _ws_attr does getattr on the result, so return a namespace.
        return types.SimpleNamespace(**self._att)

    def serializeAttachment(self, obj):
        # to_js is patched to identity, so obj is a plain dict here.
        if isinstance(obj, dict):
            self._att = dict(obj)
        else:
            self._att = dict(vars(obj))

    def snapshot(self):
        return dict(self._att)


def _run_update_rtt(ws, elapsed):
    """
    Execute _update_rtt extracted from ForkMeshHost with a stub `self`
    and a patched to_js so serializeAttachment receives a plain dict.
    """
    # Build a tiny module containing just _update_rtt and its helper _ws_attr.
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))

    # Extract top-level _ws_attr and _ws_attachment.
    helpers = [
        n for n in tree.body
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef))
        and n.name in ("_ws_attr", "_ws_attachment")
    ]

    # Extract ForkMeshHost._update_rtt as a standalone function.
    method_node = None
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == "ForkMeshHost":
            for item in node.body:
                if isinstance(item, ast.FunctionDef) and item.name == "_update_rtt":
                    method_node = item
                    break
    assert method_node is not None

    module = ast.fix_missing_locations(
        ast.Module(body=helpers + [method_node], type_ignores=[]))
    ns = {"to_js": _make_fake_to_js()}
    exec(compile(module, str(ENTRY), "exec"), ns)

    _update_rtt = ns["_update_rtt"]
    obj = types.SimpleNamespace()
    _update_rtt(obj, ws, elapsed)


def test_repo_bi_survives_first_update_rtt():
    ws = _FakeWs({"rtt": None, "repo_bi": "deadbeef"})
    _run_update_rtt(ws, 42)
    assert ws.snapshot()["repo_bi"] == "deadbeef", (
        "repo_bi was lost after _update_rtt — "
        "heartbeat presence refresh will silently stop after DO hibernation"
    )


def test_repo_bi_survives_repeated_update_rtt():
    ws = _FakeWs({"rtt": 100.0, "repo_bi": "cafebabe"})
    _run_update_rtt(ws, 80)
    _run_update_rtt(ws, 60)
    assert ws.snapshot()["repo_bi"] == "cafebabe"


def test_rtt_is_ema_after_update():
    ws = _FakeWs({"rtt": 100.0, "repo_bi": "aabb"})
    _run_update_rtt(ws, 200)
    att = ws.snapshot()
    assert att["rtt"] == 150.0    # 0.5 * 100 + 0.5 * 200
    assert att["repo_bi"] == "aabb"


def test_rtt_initialised_from_first_elapsed_when_no_prior():
    ws = _FakeWs({"rtt": None, "repo_bi": "1234"})
    _run_update_rtt(ws, 77)
    att = ws.snapshot()
    assert att["rtt"] == 77
    assert att["repo_bi"] == "1234"
