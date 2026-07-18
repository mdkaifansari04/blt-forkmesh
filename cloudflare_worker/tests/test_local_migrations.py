#!/usr/bin/env python3
"""Regression tests for migrations on a fresh local D1 database."""

import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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
    # Minimal accounts/users tables + the old non-unique email indexes that
    # migration 0041 replaces, seeded with duplicate and keyless-email rows.
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
            # The first duplicate row is kept, the later one is deleted.
            kept = {
                row[0]
                for row in connection.execute(
                    f"SELECT {key} FROM {table} WHERE email_bi = 'dup@'"
                )
            }
            assert kept == {"first"}, table
            # Non-duplicate and keyless rows are all preserved.
            total = connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            assert total == 4, table  # first, other, keyless1, keyless2

            # A fresh duplicate email is now rejected by the unique index...
            try:
                connection.execute(
                    f"INSERT INTO {table} ({key}, data, email_bi) "
                    f"VALUES ('late', '{{}}', 'solo@')"
                )
                raise AssertionError(f"{table} allowed a duplicate email")
            except sqlite3.IntegrityError:
                pass
            # ...but additional keyless (NULL-email) rows are still allowed.
            connection.execute(
                f"INSERT INTO {table} ({key}, data, email_bi) "
                f"VALUES ('keyless3', '{{}}', NULL)"
            )
    finally:
        connection.close()
