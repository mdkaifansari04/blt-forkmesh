#!/usr/bin/env python3
"""Opt-in crash/stall telemetry validation checks (stdlib only).

Loads the pure helpers (_sanitize_node_hash, _telemetry_rows and the caps they
depend on) straight out of src/entry.py without importing the Workers-only JS
runtime, the same way test_install_diag.py isolates pure code. Verifies the
unauthenticated /api/telemetry endpoint can't be used to store junk kinds,
identifying data beyond the anonymized node hash, or oversized payloads that
would threaten the small isolate (issue #354).
"""

import ast
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"

_WANT_FUNCS = ("_sanitize_diag_field", "_sanitize_node_hash", "_telemetry_rows")
_WANT_CONSTS = ("TELEMETRY_KINDS", "TELEMETRY_MAX_EVENTS",
                "TELEMETRY_MAX_SUMMARY")


def _load():
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    body = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in _WANT_FUNCS:
            body.append(node)
        elif isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in _WANT_CONSTS for t in node.targets
        ):
            body.append(node)
    module = ast.fix_missing_locations(ast.Module(body=body, type_ignores=[]))
    namespace = {}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


_NS = _load()
_rows = _NS["_telemetry_rows"]
_node = _NS["_sanitize_node_hash"]
KINDS = _NS["TELEMETRY_KINDS"]
MAX_EVENTS = _NS["TELEMETRY_MAX_EVENTS"]
MAX_SUMMARY = _NS["TELEMETRY_MAX_SUMMARY"]


def _payload(**overrides):
    data = {
        "node": "a" * 64,
        "version": "0.7.0",
        "os": "Debian GNU/Linux 13 x86_64",
        "events": [{"kind": "crash", "summary": "SIGSEGV in MainWindow.cpp:42"}],
    }
    data.update(overrides)
    return data


def test_valid_payload_is_normalized():
    out = _rows(_payload())
    assert out == [("a" * 64, "crash", "0.7.0", "Debian GNU/Linux 13 x86_64",
                    "SIGSEGV in MainWindow.cpp:42")]


def test_both_kinds_accepted():
    out = _rows(_payload(events=[
        {"kind": "crash", "summary": "boom"},
        {"kind": "stall", "summary": "froze"},
    ]))
    assert [r[1] for r in out] == ["crash", "stall"]


def test_unknown_kind_is_dropped():
    out = _rows(_payload(events=[
        {"kind": "rm-rf", "summary": "x"},
        {"kind": "stall", "summary": "froze"},
    ]))
    assert [r[1] for r in out] == ["stall"]


def test_non_dict_and_empty_events_yield_no_rows():
    assert _rows(None) == []
    assert _rows("nope") == []
    assert _rows(_payload(events=[])) == []
    assert _rows(_payload(events="not-a-list")) == []
    assert _rows({"node": "a"}) == []


def test_missing_or_blank_summary_is_dropped():
    assert _rows(_payload(events=[{"kind": "crash"}])) == []
    assert _rows(_payload(events=[{"kind": "crash", "summary": ""}])) == []
    assert _rows(_payload(events=[{"kind": "crash", "summary": 123}])) == []


def test_event_count_is_capped():
    many = [{"kind": "stall", "summary": "s%d" % i} for i in range(50)]
    out = _rows(_payload(events=many))
    assert len(out) <= MAX_EVENTS


def test_summary_is_length_clamped_and_control_scrubbed():
    out = _rows(_payload(events=[{"kind": "crash",
                                  "summary": "A" * (MAX_SUMMARY + 500)}]))
    assert len(out[0][4]) <= MAX_SUMMARY
    # NUL / control bytes (except tab/newline) must not survive into storage.
    out = _rows(_payload(events=[{"kind": "crash",
                                  "summary": "line\x00one\x07\ttab\nnl"}]))
    summary = out[0][4]
    assert "\x00" not in summary and "\x07" not in summary
    assert "\t" in summary and "\n" in summary


def test_node_hash_is_hex_only_and_clamped():
    assert _node("ABCDEF0123") == "abcdef0123"
    assert _node("<script>/etc/passwd") == "cecad"  # only hex chars kept
    assert _node("f" * 200) == "f" * 64
    assert _node(None) == ""
    assert _node(12345) == ""


def test_node_may_be_anonymous():
    # An empty node hash (client without an identity yet) is still accepted.
    out = _rows(_payload(node=""))
    assert out and out[0][0] == ""
