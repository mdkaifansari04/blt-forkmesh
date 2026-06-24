-- ForkMesh D1 migration 0015 — signup IP capture for duplicate detection.
-- Each signup now records the source IP so anti-abuse can tell how many accounts
-- came from the same address. Privacy is preserved by splitting the value:
--
--   * The IP itself (plus user-agent + country) is added to the encrypted `data`
--     blob at finalize — no schema change, readable only with DATA_KEY.
--   * ip_bi below is a blind index (keyed HMAC) of the IP, the same one-way
--     construction used for email_bi/name_bi. It supports a uniqueness COUNT
--     (e.g. SELECT COUNT(*) FROM accounts WHERE ip_bi = ?) without ever storing
--     or exposing a reversible address.
--
-- The column is added to the existing table for already-deployed databases;
-- fresh databases get it from ensure_schema() in src/entry.py. SQLite has no
-- "ADD COLUMN IF NOT EXISTS", and D1 runs each migration exactly once, so a plain
-- ADD COLUMN is safe here.

ALTER TABLE accounts ADD COLUMN ip_bi TEXT;
CREATE INDEX IF NOT EXISTS idx_accounts_ip ON accounts(ip_bi);
