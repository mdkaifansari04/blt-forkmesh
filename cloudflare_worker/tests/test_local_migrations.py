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
