-- ForkMesh D1 migration 0012 — relay federation.
-- Lets other relays federate with this (main) relay: their node signups custody
-- their Solana here and flow through this relay's treasury, and their online
-- nodes join this relay's disbursement split. The worker also creates these via
-- ensure_schema in src/entry.py, so applying this by hand is optional.
--
-- A relay is identified by its Ed25519 pubkey and must be approved before it can
-- custody signups or have its nodes counted:
--   UPDATE relays SET status = 'approved' WHERE label = '<relay label>';
-- Block a relay:
--   UPDATE relays SET status = 'blocked'  WHERE pubkey = '<relay pubkey>';

CREATE TABLE IF NOT EXISTS relays (
    relay_bi TEXT PRIMARY KEY,
    pubkey TEXT NOT NULL,
    label TEXT,
    base_url TEXT,
    status TEXT NOT NULL DEFAULT 'pending',
    registered_at INTEGER,
    approved_at INTEGER
);

-- Online node payout wallets reported by federated relays (ts = last report).
CREATE TABLE IF NOT EXISTS federated_presence (
    relay_bi TEXT NOT NULL,
    wallet TEXT NOT NULL,
    name TEXT,
    ts INTEGER NOT NULL,
    PRIMARY KEY (relay_bi, wallet)
);
CREATE INDEX IF NOT EXISTS idx_federated_presence_ts ON federated_presence(ts);

-- Signups proxied here from a federated relay. `data` is an encrypted blob that
-- reuses the donation_* field names so the existing sweep logic works on it.
CREATE TABLE IF NOT EXISTS federated_signup (
    reference TEXT PRIMARY KEY,
    relay_bi TEXT NOT NULL,
    data TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_federated_signup_relay ON federated_signup(relay_bi);
