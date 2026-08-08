#!/usr/bin/env python3
"""Docs feedback validation and storage contract checks."""

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
SCHEMA = ROOT / "src" / "schema.py"
MIGRATION = ROOT / "migrations" / "0026_feedback.sql"

_WANT_FUNCS = ("_sanitize_feedback_text", "_feedback_fields")
_WANT_CONSTS = (
    "FEEDBACK_SOURCES",
    "FEEDBACK_VOTES",
    "FEEDBACK_MAX_MESSAGE",
    "FEEDBACK_MAX_PATH",
)


def _load():
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    body = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in _WANT_FUNCS:
            body.append(node)
        elif isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in _WANT_CONSTS
            for t in node.targets
        ):
            body.append(node)
    module = ast.fix_missing_locations(ast.Module(body=body, type_ignores=[]))
    namespace = {}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


_NS = _load()
_fields = _NS["_feedback_fields"]
_sanitize = _NS["_sanitize_feedback_text"]
MAX_MESSAGE = _NS["FEEDBACK_MAX_MESSAGE"]
MAX_PATH = _NS["FEEDBACK_MAX_PATH"]


def test_like_payload_is_normalized_without_message():
    out = _fields({
        "source": "docs",
        "vote": "like",
        "path": "/docs/",
        "message": "ignored for likes",
    })

    assert out == ("docs", "like", "/docs/", "")


def test_dislike_payload_accepts_optional_message():
    out = _fields({
        "source": "docs",
        "vote": "dislike",
        "path": "/docs/#install",
        "message": "Install steps need screenshots.",
    })

    assert out == (
        "docs",
        "dislike",
        "/docs/#install",
        "Install steps need screenshots.",
    )


def test_dislike_payload_accepts_empty_message():
    out = _fields({
        "source": "docs",
        "vote": "dislike",
        "path": "/docs/",
        "message": "",
    })

    assert out == ("docs", "dislike", "/docs/", "")


def test_unknown_source_or_vote_is_rejected():
    assert _fields({"source": "blog", "vote": "like", "path": "/docs/"}) is None
    assert _fields({"source": "docs", "vote": "meh", "path": "/docs/"}) is None
    assert _fields(None) is None
    assert _fields([]) is None


def test_world_lobby_feedback_has_a_fixed_private_path():
    assert _fields({
        "source": "world",
        "vote": "feedback",
        "path": "/someone/private?token=secret",
        "message": "The first-run controls need clearer labels.",
    }) == (
        "world",
        "feedback",
        "/world/#lobby-feedback",
        "The first-run controls need clearer labels.",
    )
    assert _fields({
        "source": "world",
        "vote": "feedback",
        "message": "",
    }) is None


def test_path_defaults_to_root_when_missing_or_external():
    assert _fields({"source": "docs", "vote": "like"}) == (
        "docs",
        "like",
        "/docs/",
        "",
    )
    assert _fields({
        "source": "docs",
        "vote": "like",
        "path": "https://example.com/docs",
    }) == ("docs", "like", "/docs/", "")


def test_feedback_path_rejects_queries_private_routes_and_unsafe_fragments():
    assert _fields({
        "source": "docs",
        "vote": "like",
        "path": "/docs/?search=private-term#install",
    }) == ("docs", "like", "/docs/", "")
    assert _fields({
        "source": "docs",
        "vote": "like",
        "path": "/alice/private-repo?token=secret",
    }) == ("docs", "like", "/docs/", "")
    assert _fields({
        "source": "docs",
        "vote": "like",
        "path": "/docs/#install<script>",
    }) == ("docs", "like", "/docs/", "")


def test_message_and_path_are_capped_and_control_scrubbed():
    out = _fields({
        "source": "docs",
        "vote": "dislike",
        "path": "/" + ("a" * (MAX_PATH + 50)),
        "message": "line\x00one\x07\ttab\nnl" + ("x" * (MAX_MESSAGE + 50)),
    })

    assert out is not None
    assert len(out[2]) <= MAX_PATH
    assert len(out[3]) <= MAX_MESSAGE
    assert "\x00" not in out[3]
    assert "\x07" not in out[3]
    assert "\t" in out[3]
    assert "\n" in out[3]


def test_feedback_text_sanitizer_handles_non_strings():
    assert _sanitize(None, 20) == ""
    assert _sanitize(12345, 20) == "12345"


def test_feedback_table_is_defined_in_lazy_schema():
    schema_text = SCHEMA.read_text(encoding="utf-8")

    assert "CREATE TABLE IF NOT EXISTS feedback" in schema_text
    assert "ip_hash TEXT" in schema_text
    assert "CREATE INDEX IF NOT EXISTS idx_feedback_ts ON feedback(ts)" in schema_text
    assert (
        "CREATE INDEX IF NOT EXISTS idx_feedback_source_vote "
        "ON feedback(source, vote, ts)"
    ) in schema_text


def test_feedback_migration_file_exists_with_matching_schema():
    assert MIGRATION.exists()
    text = MIGRATION.read_text(encoding="utf-8")

    assert "CREATE TABLE IF NOT EXISTS feedback" in text
    assert "ip_hash TEXT" in text
    assert "CREATE INDEX IF NOT EXISTS idx_feedback_ts ON feedback(ts)" in text
    assert "idx_feedback_source_vote" in text


def test_feedback_endpoint_is_registered():
    entry_text = ENTRY.read_text(encoding="utf-8")

    assert 'if url.path in ("/api/feedback", "/api/feedback/"):' in entry_text
    assert "return await feedback_handler(self.env, request)" in entry_text


def test_feedback_handler_hashes_ip_and_never_stores_raw_ip():
    entry_text = ENTRY.read_text(encoding="utf-8")

    assert "async def feedback_handler(env, request):" in entry_text
    assert "ip = _transient_client_address(request)" in entry_text
    assert 'headers.get("cf-connecting-ip")' in entry_text
    assert 'headers.get("x-forwarded-for")' in entry_text
    assert "ip_hash = await blind_index(env, ip) if ip else \"\"" in entry_text
    assert "user_agent = _generalized_client_category(request)" in entry_text
    handler = entry_text.split(
        "async def feedback_handler", 1)[1].split(
        "def _validate_security_report", 1)[0]
    assert "_sanitize_feedback_text(" not in handler.split(
        "user_agent =", 1)[1].split("try:", 1)[0]
    assert (
        "INSERT INTO feedback "
        "(ts, source, vote, path, message, ip_hash, user_agent)"
    ) in entry_text
    assert "VALUES (?,?,?,?,?,?,?)" in entry_text
    assert "DELETE FROM feedback WHERE id NOT IN" in entry_text
