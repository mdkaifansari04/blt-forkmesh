#!/usr/bin/env python3
"""org_invitations dual-DDL parity + shape (migration 0124 vs schema.py).

Fresh databases are built lazily from schema.SCHEMA_STATEMENTS while deployed
databases receive migrations/0124_org_invitations.sql, so the two DDL sources
must produce the identical table and indexes or the fleets diverge.
"""

import ast
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MIGRATION = ROOT / "migrations" / "0124_org_invitations.sql"
SCHEMA = ROOT / "src" / "schema.py"


def _schema_statements():
    tree = ast.parse(SCHEMA.read_text(encoding="utf-8"), filename=str(SCHEMA))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id == "SCHEMA_STATEMENTS"
            for t in node.targets
        ):
            return ast.literal_eval(node.value)
    raise AssertionError("SCHEMA_STATEMENTS not found")


def _table_shape(connection):
    columns = [
        # (name, type, notnull, default, pk) - cid dropped so ordering of
        # unrelated columns can't mask a real mismatch.
        (row[1], row[2], row[3], row[4], row[5])
        for row in connection.execute("PRAGMA table_info(org_invitations)")
    ]
    indexes = sorted(
        row[1]
        for row in connection.execute("PRAGMA index_list(org_invitations)")
        if not row[1].startswith("sqlite_autoindex")
    )
    return columns, indexes


def _connect_from_migration():
    connection = sqlite3.connect(":memory:")
    connection.executescript(MIGRATION.read_text(encoding="utf-8"))
    return connection


def _connect_from_schema_statements():
    connection = sqlite3.connect(":memory:")
    for statement in _schema_statements():
        if "org_invitations" in statement:
            connection.execute(statement)
    return connection


def test_migration_and_schema_statements_produce_identical_table():
    from_migration = _connect_from_migration()
    from_schema = _connect_from_schema_statements()
    try:
        assert _table_shape(from_migration) == _table_shape(from_schema)
        # Guard against the filter above matching nothing: the shape must be
        # a real table, not two identical empty results.
        columns, indexes = _table_shape(from_schema)
        assert [c[0] for c in columns] == [
            "id", "org_bi", "email_bi", "inviter_bi", "role", "status",
            "data", "created_at", "expires_at", "sent_at", "accepted_at",
        ]
        assert indexes == [
            "idx_org_invitations_email",
            "idx_org_invitations_org",
            "idx_org_invitations_pending",
        ]
    finally:
        from_migration.close()
        from_schema.close()


def test_one_pending_invite_per_org_and_address():
    # The partial unique index is the concurrency boundary: two racing sends
    # can't both create a pending invite, but history (accepted/revoked/
    # expired) never blocks a fresh one.
    connection = _connect_from_migration()
    try:
        def insert(invite_id, status):
            connection.execute(
                "INSERT INTO org_invitations (id, org_bi, email_bi, "
                "inviter_bi, role, status, data, created_at, expires_at) "
                "VALUES (?,?,?,?,?,?,?,?,?)",
                (invite_id, "org1", "mail1", "kaif", "member", status,
                 "enc", 1, 2),
            )

        insert("a", "accepted")
        insert("b", "revoked")
        insert("c", "pending")
        try:
            insert("d", "pending")
            raised = False
        except sqlite3.IntegrityError:
            raised = True
        assert raised, "second pending invite for same (org, email) must fail"
        # ...but a different address in the same org is fine.
        connection.execute(
            "INSERT INTO org_invitations (id, org_bi, email_bi, inviter_bi, "
            "role, status, data, created_at, expires_at) "
            "VALUES ('e','org1','mail2','kaif','member','pending','enc',1,2)")
    finally:
        connection.close()


def test_migration_is_idempotent():
    # deploy.sh re-runs are a no-op: IF NOT EXISTS everywhere, no ALTER.
    connection = _connect_from_migration()
    try:
        connection.executescript(MIGRATION.read_text(encoding="utf-8"))
    finally:
        connection.close()
