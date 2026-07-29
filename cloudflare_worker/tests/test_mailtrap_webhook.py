"""Mailtrap lifecycle webhook and public status projection contracts."""

import ast
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
ENV_EXAMPLE = (ROOT / ".env.production.example").read_text(encoding="utf-8")


def _helper(name):
    tree = ast.parse(ENTRY)
    node = next(
        item for item in tree.body
        if isinstance(item, ast.FunctionDef) and item.name == name
    )
    namespace = {"json": json}
    exec(compile(ast.Module(body=[node], type_ignores=[]), "<helper>", "exec"),
         namespace)
    return namespace[name]


def test_webhook_parser_accepts_json_envelopes_arrays_and_jsonl():
    parse = _helper("_mailtrap_webhook_payload")
    event = {"event_id": "evt-1", "event": "delivery"}
    assert parse(json.dumps(event)) == [event]
    assert parse(json.dumps({"events": [event]})) == [event]
    assert parse(json.dumps([event, event])) == [event, event]
    assert parse(json.dumps(event) + "\n" + json.dumps(event)) == [event, event]
    assert parse("{broken") is None


def test_webhook_authenticates_raw_body_and_is_idempotent():
    assert 'request.headers.get("mailtrap-signature")' in ENTRY
    assert "hmac.compare_digest(presented, expected)" in ENTRY
    assert "MAILTRAP_WEBHOOK_EVENTS" in ENTRY
    assert "len(events) > 500" in ENTRY
    assert "mailtrap_webhook_events WHERE event_id=?" in ENTRY
    assert "DELETE FROM mailtrap_webhook_events WHERE received_at<?" in ENTRY


def test_email_status_contains_no_recipient_or_content_fields():
    assert "CREATE TABLE IF NOT EXISTS mailtrap_email_sends" in SCHEMA
    table = SCHEMA.split(
        "CREATE TABLE IF NOT EXISTS mailtrap_email_sends", 1)[1].split(
        '""",', 1)[0]
    for forbidden in ("email", "subject", "body", "recipient"):
        assert forbidden not in table.lower()
    assert '"lastEmailAt"' in ENTRY
    assert '"lastEmailStatus"' in ENTRY
    assert "badgeEmailLabel(identity)" in SCENE


def test_operator_template_documents_callback_events_and_secret():
    assert "MAILTRAP_WEBHOOK_SECRET=CHANGE-ME" in ENV_EXAMPLE
    assert "https://forkmesh.com/api/integrations/mailtrap/webhook" in ENV_EXAMPLE
    for event in (
        "delivery", "open", "click", "unsubscribe", "spam",
        "soft bounce", "bounce", "suspension", "reject",
    ):
        assert event in ENV_EXAMPLE
