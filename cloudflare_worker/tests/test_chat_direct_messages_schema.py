"""Schema and route contracts for one-to-one web direct messages."""

from pathlib import Path
import sqlite3
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import schema  # noqa: E402
import urls  # noqa: E402


def test_direct_message_migration_and_lazy_schema_match():
    migration = ROOT / "migrations" / "0079_chat_direct_messages.sql"
    assert migration.exists()
    sql = migration.read_text(encoding="utf-8")
    joined = "\n".join(schema.SCHEMA_STATEMENTS)
    for marker in (
        "CREATE TABLE IF NOT EXISTS chat_direct_conversations",
        "CREATE TABLE IF NOT EXISTS chat_direct_participants",
        "idx_chat_direct_participants_account",
    ):
        assert marker in sql
        assert marker in joined


def test_direct_message_schema_enforces_one_pair_and_two_unique_participants():
    sql = (
        ROOT / "migrations" / "0079_chat_direct_messages.sql"
    ).read_text(encoding="utf-8")
    db = sqlite3.connect(":memory:")
    db.executescript(sql)
    conversation_id = "a" * 32
    db.execute(
        "INSERT INTO chat_direct_conversations "
        "(conversation_id,pair_bi,data,created_at,updated_at,key_version) "
        "VALUES (?,?,?,?,?,1)",
        (conversation_id, "pair-bi", "sealed", 1, 1),
    )
    db.execute(
        "INSERT INTO chat_direct_participants "
        "(conversation_id,participant_bi,data,joined_at) VALUES (?,?,?,?)",
        (conversation_id, "alice-bi", "sealed-alice", 1),
    )

    try:
        db.execute(
            "INSERT INTO chat_direct_conversations "
            "(conversation_id,pair_bi,data,created_at,updated_at,key_version) "
            "VALUES (?,?,?,?,?,1)",
            ("b" * 32, "pair-bi", "sealed", 1, 1),
        )
    except sqlite3.IntegrityError:
        pass
    else:
        raise AssertionError("participant pair must be unique")

    try:
        db.execute(
            "INSERT INTO chat_direct_participants "
            "(conversation_id,participant_bi,data,joined_at) "
            "VALUES (?,?,?,?)",
            (conversation_id, "alice-bi", "sealed-alice", 1),
        )
    except sqlite3.IntegrityError:
        pass
    else:
        raise AssertionError("conversation participant must be unique")


def test_direct_message_routes_are_exact_and_opaque():
    conversation_id = "a" * 32
    assert urls.CHAT_DIRECT_MESSAGES_RE.fullmatch(
        "/api/chat/direct-messages"
    )
    assert urls.CHAT_DIRECT_MESSAGE_ROOM_ACCESS_RE.fullmatch(
        f"/api/chat/direct-messages/{conversation_id}/room-access"
    )
    assert urls.CHAT_DIRECT_MESSAGE_WS_RE.fullmatch(
        f"/api/chat/direct-messages/{conversation_id}/ws"
    )
    assert not urls.CHAT_DIRECT_MESSAGE_WS_RE.fullmatch(
        "/api/chat/direct-messages/alice/ws"
    )
