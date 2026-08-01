






CREATE TABLE IF NOT EXISTS world_inactive_presence (
    account_bi TEXT PRIMARY KEY,
    data TEXT NOT NULL,
    updated_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_world_inactive_presence_expiry
ON world_inactive_presence(expires_at);
