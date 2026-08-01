#!/usr/bin/env python3
"""Regression tests for migrations on a fresh local D1 database."""

import ast
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_world_build_board_title_migration_is_safe_after_table_creation():
    create = (ROOT / "migrations" / "0091_world_build_board.sql").read_text(
        encoding="utf-8"
    )
    upgrade = (
        ROOT / "migrations" / "0092_world_build_board_issue_title.sql"
    ).read_text(encoding="utf-8")
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(create)
        connection.executescript(upgrade)
        title_columns = [
            row for row in connection.execute(
                "PRAGMA table_info(world_build_board_items)"
            ) if row[1] == "title"
        ]
    finally:
        connection.close()

    assert len(title_columns) == 1


def _literal_assignment(path, name):
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if any(isinstance(target, ast.Name) and target.id == name
               for target in node.targets):
            return ast.literal_eval(node.value)
    raise AssertionError(f"missing {name}")


def test_activitypub_thread_lifecycle_migration_upgrades_remote_replies():
    initial = (ROOT / "migrations" / "0028_activitypub.sql").read_text(
        encoding="utf-8"
    )
    upgrade = (
        ROOT / "migrations" / "0065_activitypub_thread_lifecycle.sql"
    ).read_text(encoding="utf-8")
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(initial)
        connection.execute(
            "INSERT INTO ap_comments "
            "(context_bi,remote_id_bi,data,ts) VALUES (?,?,?,?)",
            ("context", "remote", "encrypted", 1),
        )
        connection.executescript(upgrade)
        columns = {
            row[1]: row for row in connection.execute(
                "PRAGMA table_info(ap_comments)"
            )
        }
        assert "parent_remote_id_bi" in columns
        assert "lifecycle" in columns
        assert connection.execute(
            "SELECT lifecycle,data FROM ap_comments"
        ).fetchone() == ("active", "encrypted")
        indexes = {
            row[1] for row in connection.execute(
                "PRAGMA index_list(ap_comments)"
            )
        }
        assert "idx_ap_comments_parent" in indexes
        try:
            connection.execute(
                "UPDATE ap_comments SET lifecycle='native-signed'"
            )
            raise AssertionError("invalid lifecycle was accepted")
        except sqlite3.IntegrityError:
            pass
    finally:
        connection.close()


def test_lazy_schema_upgrade_adds_columns_before_dependent_indexes():
    entry_path = ROOT / "src" / "entry.py"
    schema_path = ROOT / "src" / "schema.py"
    initial = (ROOT / "migrations" / "0028_activitypub.sql").read_text(
        encoding="utf-8"
    )
    pre_alters = _literal_assignment(
        entry_path, "SCHEMA_PRE_CREATE_ALTER_STATEMENTS"
    )
    schema_statements = _literal_assignment(schema_path, "SCHEMA_STATEMENTS")
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(initial)
        for statement in pre_alters:
            if "ap_comments" not in statement and "ap_outbox" not in statement:
                continue
            connection.execute(statement)
        for statement in schema_statements:
            if "ap_comments" in statement or "ap_outbox" in statement:
                connection.execute(statement)
        columns = {
            row[1] for row in connection.execute("PRAGMA table_info(ap_comments)")
        }
        outbox_columns = {
            row[1] for row in connection.execute("PRAGMA table_info(ap_outbox)")
        }
        indexes = {
            row[1] for row in connection.execute("PRAGMA index_list(ap_comments)")
        }
    finally:
        connection.close()

    assert {"parent_remote_id_bi", "lifecycle"}.issubset(columns)
    assert "dedupe_bi" in outbox_columns
    assert "idx_ap_comments_parent" in indexes
    apply_schema = entry_path.read_text(encoding="utf-8").split(
        "async def _apply_schema(env):", 1
    )[1].split("\ndef js_nullish", 1)[0]
    assert apply_schema.index("SCHEMA_PRE_CREATE_ALTER_STATEMENTS") < (
        apply_schema.index("SCHEMA_STATEMENTS")
    )


def test_lazy_schema_upgrade_adds_encrypted_chat_member_payload_column():
    entry_path = ROOT / "src" / "entry.py"
    schema_path = ROOT / "src" / "schema.py"
    schema_statements = _literal_assignment(schema_path, "SCHEMA_STATEMENTS")
    post_alters = _literal_assignment(entry_path, "SCHEMA_ALTER_STATEMENTS")
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(
            """
            CREATE TABLE chat_channels (
                channel_id TEXT PRIMARY KEY,
                name_bi TEXT NOT NULL UNIQUE,
                data TEXT NOT NULL,
                created_by_bi TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                key_version INTEGER NOT NULL DEFAULT 1
            );
            CREATE TABLE chat_channel_members (
                channel_id TEXT NOT NULL,
                member_bi TEXT NOT NULL,
                invited_by_bi TEXT NOT NULL,
                joined_at INTEGER NOT NULL,
                PRIMARY KEY (channel_id, member_bi)
            );
            """
        )
        for statement in schema_statements:
            if "chat_channel" in statement:
                connection.execute(statement)
        for statement in post_alters:
            if "chat_channel_members" in statement:
                connection.execute(statement)
        columns = {
            row[1]
            for row in connection.execute(
                "PRAGMA table_info(chat_channel_members)"
            )
        }
    finally:
        connection.close()

    assert "data" in columns


def test_release_downloads_user_agent_migration_bootstraps_fresh_local_db():
    migration = (ROOT / "migrations" / "0034_release_downloads_ua.sql").read_text(
        encoding="utf-8"
    )
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(migration)
        columns = {
            row[1]
            for row in connection.execute("PRAGMA table_info(release_downloads)")
        }
    finally:
        connection.close()

    assert {"repo_bi", "sha256", "ts", "ua"}.issubset(columns)


def _unique_email_setup(connection):


    connection.executescript(
        """
        CREATE TABLE accounts (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
            email_bi TEXT, name TEXT, is_admin INTEGER NOT NULL DEFAULT 0,
            ip_bi TEXT, enable_outreach INTEGER NOT NULL DEFAULT 0);
        CREATE INDEX idx_accounts_email ON accounts(email_bi);
        CREATE TABLE users (user_bi TEXT PRIMARY KEY, data TEXT NOT NULL,
            email_bi TEXT, username TEXT, is_admin INTEGER NOT NULL DEFAULT 0,
            ip_bi TEXT, enable_outreach INTEGER NOT NULL DEFAULT 0);
        CREATE INDEX idx_users_email ON users(email_bi);
        """
    )
    for table, key in (("accounts", "name_bi"), ("users", "user_bi")):
        connection.executescript(
            f"""
            INSERT INTO {table} ({key}, data, email_bi) VALUES
                ('first',  '{{}}', 'dup@'),
                ('second', '{{}}', 'dup@'),
                ('other',  '{{}}', 'solo@'),
                ('keyless1', '{{}}', NULL),
                ('keyless2', '{{}}', NULL);
            """
        )


def test_unique_email_migration_dedupes_keeping_first_and_enforces_uniqueness():
    migration = (ROOT / "migrations" / "0041_unique_account_email.sql").read_text(
        encoding="utf-8"
    )
    connection = sqlite3.connect(":memory:")
    try:
        _unique_email_setup(connection)
        connection.executescript(migration)

        for table, key in (("accounts", "name_bi"), ("users", "user_bi")):

            kept = {
                row[0]
                for row in connection.execute(
                    f"SELECT {key} FROM {table} WHERE email_bi = 'dup@'"
                )
            }
            assert kept == {"first"}, table

            total = connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            assert total == 4, table


            try:
                connection.execute(
                    f"INSERT INTO {table} ({key}, data, email_bi) "
                    f"VALUES ('late', '{{}}', 'solo@')"
                )
                raise AssertionError(f"{table} allowed a duplicate email")
            except sqlite3.IntegrityError:
                pass

            connection.execute(
                f"INSERT INTO {table} ({key}, data, email_bi) "
                f"VALUES ('keyless3', '{{}}', NULL)"
            )
    finally:
        connection.close()


def test_drop_accounts_migration_removes_only_the_legacy_table():
    migration = (ROOT / "migrations" / "0042_drop_accounts.sql").read_text(
        encoding="utf-8"
    )
    connection = sqlite3.connect(":memory:")
    try:
        _unique_email_setup(connection)
        connection.executescript(migration)

        tables = {
            row[0]
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type='table'"
            )
        }
        assert "accounts" not in tables
        assert "users" in tables

        total = connection.execute("SELECT COUNT(*) FROM users").fetchone()[0]
        assert total == 5



        connection.executescript(migration)
    finally:
        connection.close()


def test_social_directory_migration_preserves_rows_and_widens_kinds():
    initial = (
        ROOT / "migrations" / "0052_world_fediverse_media.sql"
    ).read_text(encoding="utf-8")
    upgrade = (
        ROOT / "migrations" / "0061_world_social_directory.sql"
    ).read_text(encoding="utf-8")
    connection = sqlite3.connect(":memory:")
    try:
        connection.executescript(initial)
        connection.execute(
            "INSERT INTO world_fediverse_instances "
            "(instance_id,kind,host,url,data,created_by_bi,created_at,updated_at) "
            "VALUES (?,?,?,?,?,?,?,?)",
            ("a" * 32, "mastodon", "social.example.org",
             "https://social.example.org/", "{}", "admin-bi", 1, 1),
        )
        connection.executescript(upgrade)
        assert connection.execute(
            "SELECT kind,host FROM world_fediverse_instances"
        ).fetchall() == [("mastodon", "social.example.org")]
        connection.execute(
            "INSERT INTO world_fediverse_instances "
            "(instance_id,kind,host,url,data,created_by_bi,created_at,updated_at) "
            "VALUES (?,?,?,?,?,?,?,?)",
            ("b" * 32, "x", "x.com", "https://x.com/", "{}",
             "admin-bi", 2, 2),
        )
        try:
            connection.execute(
                "INSERT INTO world_fediverse_instances "
                "(instance_id,kind,host,url,data,created_by_bi,created_at,"
                "updated_at) VALUES (?,?,?,?,?,?,?,?)",
                ("c" * 32, "unsupported", "bad.example.org",
                 "https://bad.example.org/", "{}", "admin-bi", 3, 3),
            )
            raise AssertionError("unsupported directory kind was accepted")
        except sqlite3.IntegrityError:
            pass
    finally:
        connection.close()
