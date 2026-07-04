#!/usr/bin/env python3
"""Signup IP capture contract checks (stdlib only).

A signup records its source IP so anti-abuse can tell how many accounts share an
address, but the IP must stay private. The design splits the value two ways:

  * The IP (plus user-agent + country) goes into the AES-GCM-encrypted `data`
    blob — readable only with DATA_KEY, never as a plaintext column.
  * Only a blind index (keyed HMAC) of the IP is stored in a searchable column
    (ip_bi), so duplicate signups are countable but the address is not reversible.

These tests assert that contract as source substrings so a refactor can't quietly
start storing the raw IP in the clear or drop the uniqueness index. They don't
import the Workers-only JS runtime.
"""

from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# SCHEMA_STATEMENTS (D1 DDL) was extracted from entry.py into schema.py;
# concatenate it so the schema source-contract assertions below still resolve.
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + SCHEMA.read_text(encoding="utf-8"))


def test_accounts_table_has_ip_blind_index_column_and_index():
    assert "is_admin INTEGER NOT NULL DEFAULT 0, ip_bi TEXT)" in ENTRY_TEXT
    assert "CREATE INDEX IF NOT EXISTS idx_accounts_ip ON accounts(ip_bi)" in ENTRY_TEXT


def test_ip_column_is_added_idempotently_for_existing_databases():
    # ensure_schema() backfills the column on already-deployed account tables, and
    # swallows the "duplicate column name" raised on a second run.
    assert 'ALTER TABLE accounts ADD COLUMN ip_bi TEXT' in ENTRY_TEXT


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


def test_signup_ip_is_stored_inside_the_encrypted_record():
    # The metadata (IP/UA/country) is added to `rec`, which _save_account encrypts
    # via encrypt_row — so it never lands in a plaintext column.
    assert 'rec.setdefault("signup", _signup_metadata(request))' in ENTRY_TEXT


def test_only_a_blind_index_of_the_ip_is_indexed_not_the_raw_ip():
    # The searchable column is the one-way HMAC, derived from the stored IP, and
    # handed to _save_account as ip_bi (not the address).
    assert "ip_bi = await blind_index(env, signup_ip)" in ENTRY_TEXT
    assert "await _save_account(env, name_bi, rec, email_bi=email_bi, ip_bi=ip_bi)" \
        in ENTRY_TEXT
