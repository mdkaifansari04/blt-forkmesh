#!/usr/bin/env python3
"""The unread-notifications digest email shows item number, submitter, and
time — not just a bare title/body (previously the only fields rendered)."""

import ast
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
CATALOG_TEXT = CATALOG.read_text(encoding="utf-8")

FUNCS = {
    "clean_string",
    "_html_escape",
    "_light_email_fragment",
    "_forkmesh_email_action_html",
    "_forkmesh_email_card_html",
    "_format_email_ts",
    "_notification_digest_email",
}


def _load():
    tree = ast.parse(CATALOG_TEXT + "\n" + ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCS
    ]
    found = {node.name for node in selected}
    assert found == FUNCS, "missing functions: %s" % sorted(FUNCS - found)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    ns = {"time": time}
    exec(compile(module, str(ENTRY), "exec"), ns)
    return ns


def test_format_email_ts_is_a_sortable_utc_string():
    ns = _load()
    fmt = ns["_format_email_ts"]
    # 2026-07-09T14:32:00Z in epoch ms.
    assert fmt(1783607520000) == "2026-07-09 14:32 UTC"
    # Missing/zero/garbage timestamps degrade to empty, not a crash or "1970".
    assert fmt(0) == ""
    assert fmt(None) == ""
    assert fmt("not-a-number") == ""


def test_digest_item_shows_number_actor_and_time():
    ns = _load()
    subject, text, html = ns["_notification_digest_email"]("alice", [{
        "kind": "pending_inbox",
        "title": "Issue submitted for alice/forkmesh",
        "body": "Search box is broken on mobile",
        "repo": "alice/forkmesh",
        "actor": "bob",
        "ts": 1783607520000,
        "meta": {"number": 42, "source": "issue"},
    }])
    assert subject == "ForkMesh: 1 new notification"
    # Text body: number folded into the header line, "by <actor>" + time on
    # their own indented line (matching the existing "    body" indent style).
    assert "- [alice/forkmesh] #42 Issue submitted for alice/forkmesh" in text
    assert "    by bob \xb7 2026-07-09 14:32 UTC" in text
    assert "    Search box is broken on mobile" in text
    # HTML body: same three facts, escaped, in a small muted meta line.
    assert "#42" in html
    assert "by <strong>bob</strong>" in html
    assert "2026-07-09 14:32 UTC" in html


def test_digest_item_without_number_or_actor_degrades_gracefully():
    # A brand-new PR/comment has no durable number yet, and an older stored
    # notification predating actor tracking has none either — the meta line
    # must simply omit whatever piece is missing, not print "None" or "#0".
    ns = _load()
    _subject, text, html = ns["_notification_digest_email"]("alice", [{
        "kind": "pull_submitted",
        "title": "Pull request submitted for alice/forkmesh",
        "body": "",
        "repo": "alice/forkmesh",
        "actor": "",
        "ts": 0,
        "meta": {},
    }])
    # The header line has no "#0" prefix and no dangling "by " with nothing
    # after it.
    assert "- [alice/forkmesh] Pull request submitted for alice/forkmesh" in text
    assert "#0" not in text
    assert "None" not in text
    # No "by "/time meta line at all follows the header — straight to the
    # closing "Open ForkMesh..." text (there's no body either in this case).
    lines = text.split("\n")
    header_idx = lines.index(
        "- [alice/forkmesh] Pull request submitted for alice/forkmesh")
    assert lines[header_idx + 1] == ""
    # HTML: the meta <p> (distinguished by its #8a8a93 muted color) is omitted
    # entirely rather than rendering an empty/placeholder line.
    assert "#8a8a93;font-size:12px\">" not in html.split(
        "Pull request submitted for alice/forkmesh</p>")[1].split("</div>")[0]


def test_digest_html_escapes_the_actor_name():
    ns = _load()
    _subject, _text, html = ns["_notification_digest_email"]("alice", [{
        "kind": "mention",
        "title": "You were mentioned",
        "body": "",
        "repo": "alice/forkmesh",
        "actor": "<script>alert(1)</script>",
        "ts": 1783607520000,
        "meta": {"number": 7},
    }])
    assert "<script>alert(1)</script>" not in html
    assert "&lt;script&gt;" in html


def test_digest_html_uses_one_high_contrast_light_mode():
    ns = _load()
    _subject, _text, html = ns["_notification_digest_email"]("alice", [{
        "kind": "mention",
        "title": "You were mentioned",
        "body": "",
        "repo": "alice/forkmesh",
        "actor": "bob",
        "ts": 1783607520000,
        "meta": {"number": 7},
    }])
    assert 'name="color-scheme" content="light"' in html
    assert 'name="supported-color-schemes" content="light"' in html
    assert "prefers-color-scheme:dark" not in html
    assert "#ffffff" in html
    assert 'class="fm-card"' in html
    assert "background:#090909" not in html
    assert "background:#0f0f11" not in html
    assert "data-forkmesh-site-action" in html
    assert 'href="https://forkmesh.com/"' in html


def test_notify_mentions_accepts_an_optional_number_for_the_digest():
    # notify_mentions had no way to attach the item's number to the stored
    # notification at all — mentions always rendered numberless in the digest
    # even when the mentioning comment was on a real, numbered issue/PR.
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def notify_mentions"):
        ENTRY_TEXT.index("def _thread_key")
    ]
    assert "number=0" in body
    assert 'meta={"number": number} if number else {}' in body
    # Call sites that know the number now pass it through.
    for marker in (
        'event.get("body", ""), repo_web_href(owner, repo), "issue",\n                number=number))',
        '"issue",\n                number=number))',
        'repo_web_href(owner, repo), "pull", number=number)',
        'repo_web_href(owner, repo), "discussion", number=number)',
    ):
        assert marker in ENTRY_TEXT
