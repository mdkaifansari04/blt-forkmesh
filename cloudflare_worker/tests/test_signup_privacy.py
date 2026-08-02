#!/usr/bin/env python3
"""Signup anti-abuse privacy contract checks (stdlib only).

A signup uses its source address transiently so anti-abuse can count how many
accounts share a network, but the raw value must never be retained:

  * Country and a generalized client category may enter the encrypted account.
  * Only a blind index (keyed HMAC) of the transient address is searchable.

These tests assert that contract as source substrings so a refactor can't quietly
start storing the raw IP in the clear or drop the uniqueness index. They don't
import the Workers-only JS runtime.
"""

import ast
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + SCHEMA.read_text(encoding="utf-8"))


def test_users_table_has_ip_blind_index_column_and_index():
    # The users table (the authoritative account store since the legacy
    # accounts table was dropped by migration 0042) carries the blind-index
    # column and its lookup index from CREATE.
    assert "ip_bi TEXT" in ENTRY_TEXT
    assert "CREATE INDEX IF NOT EXISTS idx_users_ip ON users(ip_bi)" in ENTRY_TEXT


def test_parity_d1_migration_file_exists():
    mig = ENTRY.resolve().parents[1] / "migrations" / "0015_account_signup_ip.sql"
    assert mig.exists()
    text = mig.read_text(encoding="utf-8")
    assert "ALTER TABLE accounts ADD COLUMN ip_bi TEXT" in text
    assert "CREATE INDEX IF NOT EXISTS idx_accounts_ip ON accounts(ip_bi)" in text


def test_signup_ip_comes_from_cloudflare_connecting_ip_header():
    assert 'headers.get("cf-connecting-ip")' in ENTRY_TEXT
    # X-Forwarded-For is only a fallback for non-CF paths.
    assert 'headers.get("x-forwarded-for")' in ENTRY_TEXT


def test_raw_signup_ip_and_user_agent_are_not_retained_in_account_metadata():
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
    metadata = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "_signup_metadata"
    )
    constants = {
        node.value for node in ast.walk(metadata)
        if isinstance(node, ast.Constant) and isinstance(node.value, str)
    }
    assert "country" in constants
    assert "clientCategory" in constants
    assert "ip" not in constants
    assert "ua" not in constants
    assert 'rec["signup"] = signup_meta' in ENTRY_TEXT


def test_only_a_blind_index_of_the_ip_is_indexed_not_the_raw_ip():
    # The searchable column is the one-way HMAC, derived from the stored IP, and
    # handed to _save_account as ip_bi (not the address).
    assert "ip_bi = await blind_index(env, signup_ip)" in ENTRY_TEXT
    assert "await _save_account(env, name_bi, rec, email_bi=email_bi, ip_bi=ip_bi)" \
        in ENTRY_TEXT
