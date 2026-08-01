











CREATE TABLE IF NOT EXISTS relays (
    relay_bi TEXT PRIMARY KEY,
    pubkey TEXT NOT NULL,
    label TEXT,
    base_url TEXT,
    status TEXT NOT NULL DEFAULT 'pending',
    registered_at INTEGER,
    approved_at INTEGER
);




CREATE TABLE IF NOT EXISTS federated_presence (
    relay_bi TEXT NOT NULL,
    wallet TEXT NOT NULL,
    name TEXT,
    ts INTEGER NOT NULL,
    PRIMARY KEY (relay_bi, wallet)
);
CREATE INDEX IF NOT EXISTS idx_federated_presence_ts ON federated_presence(ts);



CREATE TABLE IF NOT EXISTS federated_signup (
    reference TEXT PRIMARY KEY,
    relay_bi TEXT NOT NULL,
    data TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_federated_signup_relay ON federated_signup(relay_bi);
